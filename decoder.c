/*
 * Copyright (c) 2016 Dmitry Osipenko <digetx@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "decoder.h"
#include "syntax_parse.h"

#define FOREACH_BIT_SET(val, itr, size)			\
	if (val != 0)					\
		for (itr = 0; itr < size; itr++)	\
			if ((val >> itr) & 1)

#define __ALIGN_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#define ALIGN(x, a)		__ALIGN_MASK(x, (typeof(x))(a) - 1)

#define TIMEOUT_SEC	3

#define FPS()	(decoder->frames_decoded / max(decoder->dec_time_acc, 1))

#define DATA_BUF_SIZE		0x00080000

#define DRAM_PHYS_BASE		0x2F600000
#define DRAM_PHYS_END		(DRAM_PHYS_BASE + MEM_SZ)
#define MEM_SZ			0x08000000

#define IRAM_BASE_ADDR		0x40000400
#define IRAM_END_ADDR		0x40040000

#define CLK_RST_CONTROLLER_CLK_ENB_H_SET_0	0x328
#define CLK_RST_CONTROLLER_RST_DEV_H_SET_0	0x308
#define CLK_RST_CONTROLLER_RST_DEV_H_CLR_0	0x30C

#define CAR_VDE	(1 << 29)

#define ICMDQUE_WR		0x00
#define CMDQUE_CONTROL		0x08
#define INTR_STATUS		0x18
#define BSE_CONFIG		0x44

#define PRI_ICTLR_IRQ_LATCHED	0x010

#define INT_VDE_SYNC_TOKEN	9
#define INT_VDE_SXE		12

#define SXE_INT_ENB		(1 << 2)
#define SYNC_TOKEN_INT_ENB	(1 << 0)

#define NAL_START_CODE_SZ	ARRAY_SIZE(nal_start_code)

#define SXE(offt)	(0xA000 + (offt))
#define BSEV(offt)	(0xB000 + (offt))
#define MBE(offt)	(0xC000 + (offt))
#define PPE(offt)	(0xC200 + (offt))
#define MCE(offt)	(0xC400 + (offt))
#define TFE(offt)	(0xC600 + (offt))
#define VDMA(offt)	(0xCA00 + (offt))
#define FRAMEID(offt)	(0xD800 + (offt))

static void *VDE_io_mem_virt;
static void *CAR_io_mem_virt;
static void *ICTLR_io_mem_virt;
static void *dram_virt;
static void *iram_virt;

static pthread_mutex_t irq_upd_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t decode_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sync_token_cond = PTHREAD_COND_INITIALIZER;
static pthread_t irq_poll_thread;
static uint32_t irqs_to_watch[4];
static uint32_t irqs_status[4];

static const char nal_start_code[] = { 0x00, 0x00, 0x01 };

unsigned frame_luma_size(decoder_context *decoder)
{
	unsigned pic_width_in_mbs = decoder->active_sps->pic_width_in_mbs_minus1 + 1;
	unsigned pic_height_in_mbs = decoder->active_sps->pic_height_in_map_units_minus1 + 1;
	unsigned img_width = pic_width_in_mbs * 16;
	unsigned img_height = pic_height_in_mbs * 16;
	unsigned img_size = img_width * img_height;

	return img_size;
}

unsigned frame_chroma_size(decoder_context *decoder)
{
	return frame_luma_size(decoder) / 4;
}

static void map_mem(void **mem_virt, off_t phys_address, off_t size)
{
	static int mem_dev = -1;
	off_t PageOffset, PageAddress;
	size_t PagesSize;

	if (mem_dev == -1) {
		mem_dev = open("/dev/mem", O_RDWR | O_SYNC);
		assert(mem_dev != -1);
	}

	PageOffset  = phys_address % getpagesize();
	PageAddress = phys_address - PageOffset;
	PagesSize   = ((size / getpagesize()) + 1) * getpagesize();

	*mem_virt = mmap(NULL, PagesSize, PROT_READ | PROT_WRITE,
			 MAP_SHARED, mem_dev, PageAddress);

	assert(*mem_virt != MAP_FAILED);

	*mem_virt += PageOffset;
}

static uint32_t mem_read(void *mem_virt, uint32_t offset, int size)
{
	switch (size) {
	case 8:
		return *(volatile uint8_t*)(mem_virt + offset);
	case 16:
		return *(volatile uint16_t*)(mem_virt + offset);
	case 32:
		return *(volatile uint32_t*)(mem_virt + offset);
	default:
		abort();
	}
}

static void mem_write(void *mem_virt, uint32_t offset, uint32_t value, int size)
{

	switch (size) {
	case 8:
		*(volatile uint8_t*)(mem_virt + offset) = value;
		break;
	case 16:
		*(volatile uint16_t*)(mem_virt + offset) = value;
		break;
	case 32:
		*(volatile uint32_t*)(mem_virt + offset) = value;
		break;
	default:
		abort();
	}

	if (mem_virt == dram_virt) {
		offset += DRAM_PHYS_BASE;
	}
	if (mem_virt == iram_virt) {
		offset += IRAM_BASE_ADDR;
	}
	if (mem_virt == VDE_io_mem_virt) {
		offset += 0x60010000;
	}
	if (mem_virt == CAR_io_mem_virt) {
		offset += 0x60006000;
	}
	if (mem_virt == ICTLR_io_mem_virt) {
		offset += 0x60004000;
	}

	DECODER_DPRINT("%d: [0x%08X] = 0x%08X\n", size, offset, value);
}

static uint32_t reg_read(void *mem_virt, uint32_t offset)
{
	return mem_read(mem_virt, offset, 32);
}

static void reg_write(void *mem_virt, uint32_t offset, uint32_t value)
{
	mem_write(mem_virt, offset, value, 32);
}

static uint32_t tegra_VDE_read(uint32_t offset)
{
	uint32_t ret = reg_read(VDE_io_mem_virt, offset);

	DECODER_DPRINT("[0x%08X] = 0x%08X\n", 0x60010000 + offset, ret);

	return ret;
}

static void tegra_VDE_write(uint32_t offset, uint32_t value)
{
	reg_write(VDE_io_mem_virt, offset, value);
}

static void tegra_VDE_set_bits(uint32_t offset, uint32_t mask)
{
	reg_write(VDE_io_mem_virt, offset,
		  reg_read(VDE_io_mem_virt, offset) | mask);
}

static void handle_IRQ(decoder_context *decoder, int irq_nb)
{
	pthread_mutex_lock(&decode_mutex);

	if (!decoder->running) {
		goto out;
	}

	switch (irq_nb) {
	case INT_VDE_SYNC_TOKEN:
		tegra_VDE_set_bits(FRAMEID(0x208), 0);
		pthread_cond_signal(&sync_token_cond);
		break;
	case INT_VDE_SXE:
		tegra_VDE_set_bits(SXE(0x0C), 0);
		break;
	default:
		abort();
	}
out:
	pthread_mutex_unlock(&decode_mutex);
}

static void irq_sts_poll(decoder_context *decoder, int lock)
{
	uint32_t new_sts;
	uint32_t upd_sts;
	int bank;
	int i;

	if (!lock && pthread_mutex_trylock(&irq_upd_mutex) != 0) {
		return;
	}

	if (lock) {
		 pthread_mutex_lock(&irq_upd_mutex);
	}

	for (bank = 0; bank < 4; bank++) {
		if (irqs_to_watch[bank] == 0) {
			continue;
		}
repeat:
		new_sts = reg_read(ICTLR_io_mem_virt,
				   PRI_ICTLR_IRQ_LATCHED + bank * 0x100);
		new_sts = new_sts & irqs_to_watch[bank];
		upd_sts = irqs_status[bank] ^ new_sts;

// 		DECODER_DPRINT("IRQ STS irqs_status %X upd_sts %X new_sts %X\n",
// 			      irqs_status[bank], upd_sts, new_sts);

		if (upd_sts == 0) {
			continue;
		}

		FOREACH_BIT_SET(upd_sts, i, 32) {
			int irq_nb  = bank * 32 + i;
			int irq_sts = !!(new_sts & (1 << i));

			DECODER_DPRINT("IRQ %d update %d\n", irq_nb, irq_sts);

			if (irq_sts) {
				handle_IRQ(decoder, irq_nb);
				goto repeat;
			}
		}

		irqs_status[bank] = new_sts;
	}

	pthread_mutex_unlock(&irq_upd_mutex);
}

static void * irq_watcher(void *arg)
{
	decoder_context *decoder = arg;

	for (;;) {
		irq_sts_poll(decoder, 0);
		usleep(1000);
	}

	return NULL;
}

static void enable_IRQ_watch(int irq_nb)
{
	int bank = irq_nb >> 5;

	assert(bank < 4);

	irqs_to_watch[bank] |= 1 << (irq_nb & 0x1F);
}

static uint32_t reserve_mem_phys(size_t size, unsigned align)
{
	static uint32_t dram_next_reserve_pofft = DRAM_PHYS_BASE;
	uint32_t reserve_pofft = ALIGN(dram_next_reserve_pofft, align);

	assert(align != 0);

	dram_next_reserve_pofft = reserve_pofft + size;
	assert(dram_next_reserve_pofft < DRAM_PHYS_BASE + MEM_SZ);

	return reserve_pofft;
}

static uint32_t reserve_iram_phys(size_t size, unsigned align)
{
	static uint32_t iram_next_reserve_pofft = IRAM_BASE_ADDR;
	uint32_t reserve_pofft = ALIGN(iram_next_reserve_pofft, align);

	assert(align != 0);

	iram_next_reserve_pofft = reserve_pofft + size;
	assert(iram_next_reserve_pofft < IRAM_END_ADDR);

	return reserve_pofft;
}

void * p2v(uint32_t paddr)
{
	if (paddr >= DRAM_PHYS_BASE && paddr < DRAM_PHYS_END) {
		return dram_virt + (paddr - DRAM_PHYS_BASE);
	}

	assert(paddr >= IRAM_BASE_ADDR);
	assert(paddr < IRAM_END_ADDR);

	return iram_virt + (paddr - IRAM_BASE_ADDR);
}

static void tegra_VDE_reset(decoder_context *decoder)
{
	decoder->running = 0;

	reg_write(CAR_io_mem_virt,
		  CLK_RST_CONTROLLER_RST_DEV_H_SET_0, CAR_VDE);

	reg_write(CAR_io_mem_virt,
		  CLK_RST_CONTROLLER_CLK_ENB_H_SET_0, CAR_VDE);

	usleep(1000);

	reg_write(CAR_io_mem_virt,
		  CLK_RST_CONTROLLER_RST_DEV_H_CLR_0, CAR_VDE);

	tegra_VDE_set_bits(SXE(0xF0), 0xA);
	tegra_VDE_set_bits(BSEV(CMDQUE_CONTROL), 0xA00);
	tegra_VDE_set_bits(MBE(0x50), 0x8002);
	tegra_VDE_set_bits(MBE(0xA0), 0xA);
	tegra_VDE_set_bits(PPE(0x14), 0xA);
	tegra_VDE_set_bits(PPE(0x28), 0xA);
	tegra_VDE_set_bits(MCE(0x08), 0xA00);
	tegra_VDE_set_bits(TFE(0x00), 0xA);
	tegra_VDE_set_bits(VDMA(0x04), 0x5);
	tegra_VDE_write(VDMA(0x1C), 0x00000000);
	tegra_VDE_write(VDMA(0x00), 0x00000000);
	tegra_VDE_write(VDMA(0x04), 0x00000007);
	tegra_VDE_write(FRAMEID(0x200), 0x00000006 | SYNC_TOKEN_INT_ENB);
	tegra_VDE_write(TFE(0x04), 0x00000005);
	tegra_VDE_write(MBE(0x84), 0x00000000);
	tegra_VDE_write(SXE(0x08), 0x00000010 /*| SXE_INT_ENB*/);
	tegra_VDE_write(SXE(0x54), 0x00000150);
	tegra_VDE_write(SXE(0x58), 0x0000054C);
	tegra_VDE_write(SXE(0x5C), 0x00000E34);
	tegra_VDE_write(MCE(0x10), 0x063C063C);
	tegra_VDE_write(BSEV(INTR_STATUS), 0x0003FC00);
	tegra_VDE_write(BSEV(0x40), 0x00000100);
	tegra_VDE_write(BSEV(0x98), 0x00000000);
	tegra_VDE_write(BSEV(0x9C), 0x00000060);
}

static void tegra_VDE_init(decoder_context *decoder)
{
	map_mem(&VDE_io_mem_virt, 0x60010000, 0xDB00);
	map_mem(&CAR_io_mem_virt, 0x60006000, 0x1000);
	map_mem(&ICTLR_io_mem_virt, 0x60004000, 0x340);
	map_mem(&dram_virt, DRAM_PHYS_BASE, MEM_SZ);
	map_mem(&iram_virt, IRAM_BASE_ADDR, 0x3FC00);

	tegra_VDE_reset(decoder);

	enable_IRQ_watch(INT_VDE_SYNC_TOKEN);
	enable_IRQ_watch(INT_VDE_SXE);

	assert(pthread_create(&irq_poll_thread, NULL, irq_watcher, decoder) == 0);
}

static void tegra_VDE_stuck(decoder_context *decoder)
{
	tegra_VDE_reset(decoder);
	DECODER_ERR("resetting; frame #%d\n", decoder->frames_decoded);
}

static void tegra_VDE_poll_bit(decoder_context *decoder,
			       uint32_t offt, unsigned bit_nb)
{
	int retries = 100;
	uint32_t val;

	do {
		val = tegra_VDE_read(offt) & (1 << bit_nb);

		if (val) {
			usleep(1);
		}
	} while (val && retries--);

	if (val) {
		tegra_VDE_stuck(decoder);
	}
}

static void tegra_VDE_BSEV_push_ICMDQUEUE(decoder_context *decoder,
					  uint32_t value)
{
	tegra_VDE_write(BSEV(ICMDQUE_WR), value);
	tegra_VDE_poll_bit(decoder, BSEV(INTR_STATUS), 2);
}

static void tegra_VDE_MBE_wait(void)
{
	int retries = 100;
	uint32_t val;

	do {
		val = tegra_VDE_read(MBE(0x8C)) & 0x1F;

		if (val < 0x10) {
			usleep(1);
		}
	} while ((val < 0x10) && retries--);

	if (val < 0x10) {
		DECODER_ERR("MBE poll timeout!\n");
	}
}

static int tegra_VDE_level_idc(decoder_context *decoder)
{
	int level = decoder->active_sps->level_idc;

	switch (level) {
	case 11: return 2;
	case 12: return 3;
	case 13: return 4;
	case 20: return 5;
	case 21: return 6;
	case 22: return 7;
	case 30: return 8;
	case 31: return 9;
	case 32: return 10;
	case 40: return 11;
	case 41: return 12;
	case 42: return 13;
	case 50: return 14;
	case 51: return 15;
	}

	return 0;
}

static void tegra_setup_MBE_refs(uint32_t *frame_ids_enb, int chunk_nb, int l1)
{
	tegra_VDE_write(MBE(0x80), 0xC0000000 |
			(l1 << 26) | (chunk_nb << 24) | *frame_ids_enb);
	*frame_ids_enb = 0;

	tegra_VDE_MBE_wait();
}

static void tegra_setup_MBE_ref_list(frames_list *list, unsigned pic_order_cnt,
				     int B_frame)
{
	frame_data **REF_frames = list->frames;
	frame_data *frame;
	uint32_t frame_ids_enb = 0;
	int list_sz = list->size;
	int l1 = 0;
	int i;

	for (i = 0; i < list_sz; i++) {
		frame = REF_frames[i];

		if (B_frame && !l1 && frame->pic_order_cnt > pic_order_cnt) {
			tegra_setup_MBE_refs(&frame_ids_enb, i >> 2, l1);
			l1 = 1;
		}

		tegra_VDE_write(MBE(0x80), 0xD0000000 |
				(frame->frame_idx << 23) |
				frame->pic_order_cnt);

		tegra_VDE_write(MBE(0x80), 0xD0200000 |
				(frame->frame_idx << 23));

		frame_ids_enb |= frame->frame_idx << (6 * (i % 4));

		if (i % 4 == 3 || i == list_sz - 1) {
			tegra_setup_MBE_refs(&frame_ids_enb, i >> 2, l1);
		}
	}
}

static void tegra_setup_FRAMEID(decoder_context *decoder, frame_data *frame,
				int frameid)
{
	unsigned pic_width_in_mbs = decoder->active_sps->pic_width_in_mbs_minus1 + 1;
	unsigned pic_height_in_mbs = decoder->active_sps->pic_height_in_map_units_minus1 + 1;
	unsigned dont_untile_16x16 = 0;

	DECODER_DPRINT("Setting up FRAMEID %d\n", frameid);

	assert(frameid < 17);
	assert(!frame->empty);

	frame->frame_idx = frameid;

	tegra_VDE_write(FRAMEID(0x000 + frameid * 4),
			(dont_untile_16x16 << 31) | frame->Y_paddr >> 8);
	tegra_VDE_write(FRAMEID(0x100 + frameid * 4), frame->U_paddr >> 8);
	tegra_VDE_write(FRAMEID(0x180 + frameid * 4), frame->V_paddr >> 8);
	tegra_VDE_write(FRAMEID(0x080 + frameid * 4),
			(pic_width_in_mbs << 16) | pic_height_in_mbs);

	tegra_VDE_write(FRAMEID(0x280 + frameid * 4),
			(((pic_width_in_mbs + 1) >> 1) << 6) | 1);
}

static void tegra_VDE_MBE_set_0xA_reg(int reg, uint32_t val)
{
	tegra_VDE_write(MBE(0x80),
			0xA0000000 | ((reg) << 24) | (val & 0xFFFF));

	tegra_VDE_write(MBE(0x80),
			0xA0000000 | ((reg + 1) << 24) | (val >> 16));
}

static void tegra_VDE_setup_IRAM_list(void *lists, frames_list *frame_list,
				      int table, int max_frame_num, int is_DPB)
{
	int32_t frame_num;
	uint32_t data;
	int i;

	for (i = 0; i < frame_list->size; i++) {
		frame_data *frame = frame_list->frames[i + (is_DPB ? 1 : 0)];

		assert(frame->frame_idx != 0);
		assert(!frame->empty);

		frame_num = frame->frame_num;

		if (frame->frame_num_wrap) {
			frame_num -= max_frame_num;
		}

		data  = frame->frame_idx << 26;
		data |= !frame->is_B_frame << 25;
		data |= 1 << 24;
		data |= frame_num & 0x7FFFFF;

		mem_write(lists, 0x80 * table + i * 8, data, 32);
		mem_write(lists, 0x80 * table + i * 8 + 4,
			  frame->aux_data_paddr, 32);
	}
}

static void tegra_VDE_setup_IRAM_lists(decoder_context *decoder)
{
	int max_frame_num;
	void *vaddr;

	if (decoder->sh.slice_type == I) {
		return;
	}

	vaddr = p2v(decoder->iram_lists_paddress);

	max_frame_num = 1 << (decoder->active_sps->log2_max_frame_num_minus4 + 4);

	tegra_VDE_setup_IRAM_list(vaddr, &decoder->DPB_frames_array, 0,
				  max_frame_num, 1);
	tegra_VDE_setup_IRAM_list(vaddr, &decoder->DPB_frames_array, 3,
				  max_frame_num, 1);

	if (decoder->sh.slice_type == P) {
		tegra_VDE_setup_IRAM_list(vaddr, &decoder->ref_frames_P_list0, 1,
					  max_frame_num, 0);
		tegra_VDE_setup_IRAM_list(vaddr, &decoder->DPB_frames_array, 2,
					  max_frame_num, 1);
	} else {
		tegra_VDE_setup_IRAM_list(vaddr, &decoder->ref_frames_B_list0, 1,
					  max_frame_num, 0);
		tegra_VDE_setup_IRAM_list(vaddr, &decoder->ref_frames_B_list1, 2,
					  max_frame_num, 0);
	}
}

static void tegra_VDE_decoder_init_mem(decoder_context *decoder,
				       unsigned total_mbs_nb)
{
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	unsigned baseline_profile = (decoder->active_sps->profile_idc == 66);
	int i;

	decoder->parse_start_paddress = reserve_mem_phys(DATA_BUF_SIZE, 1);
	decoder->parse_limit_paddress = reserve_mem_phys(0x0, 0x400);

	// Prepend NAL_START_CODE to the syntax data
	memcpy(p2v(decoder->parse_start_paddress),
	       nal_start_code, NAL_START_CODE_SZ);

	for (i = 0; i < ARRAY_SIZE(decoder->DPB_frames_array.frames); i++) {
		DPB_frames[i]->Y_paddr = reserve_mem_phys(
					frame_luma_size(decoder), 0x100);
		DPB_frames[i]->U_paddr = reserve_mem_phys(
					frame_chroma_size(decoder), 0x100);
		DPB_frames[i]->V_paddr = reserve_mem_phys(
					frame_chroma_size(decoder), 0x100);
		if (!baseline_profile) {
			DPB_frames[i]->aux_data_paddr =
					reserve_mem_phys(total_mbs_nb * 64, 4);
		} else {
			DPB_frames[i]->aux_data_paddr = 0xF4DEAD00;
		}
		if (i > 0) {
			DPB_frames[i]->empty = 1;
		}
	}

	decoder->iram_lists_paddress = reserve_iram_phys(512, 4);
	decoder->iram_unk_paddress   = reserve_iram_phys(total_mbs_nb / 2, 4);
	bzero(p2v(decoder->iram_unk_paddress), total_mbs_nb / 2);
}

static int tegra_VDE_decode_trigger(decoder_context *decoder,
				    unsigned total_mbs_nb)
{
	struct timespec time;
	int ret;

	pthread_mutex_lock(&decode_mutex);

	decoder->running = 1;

	tegra_VDE_write(BSEV(0x8C), 0x00000001);
	tegra_VDE_write(SXE(0x00), 0x20000000 | (total_mbs_nb - 1));

	clock_gettime(CLOCK_REALTIME, &time);
	decoder->dec_time_acc -= time.tv_sec;
	time.tv_sec += TIMEOUT_SEC;

	ret = pthread_cond_timedwait(&sync_token_cond, &decode_mutex, &time);

	clock_gettime(CLOCK_REALTIME, &time);
	decoder->dec_time_acc += time.tv_sec;

	pthread_mutex_unlock(&decode_mutex);

	return ret;
}

void tegra_VDE_decode_frame(decoder_context *decoder)
{
	bitstream_reader *reader = &decoder->reader;
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	int DPB_frames_array_size = decoder->DPB_frames_array.size;
	unsigned pic_width_in_mbs = decoder->active_sps->pic_width_in_mbs_minus1 + 1;
	unsigned pic_height_in_mbs = decoder->active_sps->pic_height_in_map_units_minus1 + 1;
	unsigned total_mbs_nb = pic_width_in_mbs * pic_height_in_mbs;
	unsigned baseline_profile = (decoder->active_sps->profile_idc == 66);
	unsigned is_B_frame = (decoder->sh.slice_type == B);
	unsigned is_ref_frame = (decoder->nal.ref_idc != 0);
	uint32_t data_start = reader->NAL_offset;
	uint32_t data_end, data_size, SXE_parsed;
	int i, ret;

	if (decoder->frames_decoded == 0) {
		tegra_VDE_decoder_init_mem(decoder, total_mbs_nb);
	}

	data_end = min(data_start + DATA_BUF_SIZE,
		       reader->bitstream_end + NAL_START_CODE_SZ);
	data_size = data_end - data_start;

	DECODER_DPRINT("++++++++++++++++\n" \
		       "Decoding frame %d at 0x%X size 0x%X @0x%08X\n",
		       decoder->frames_decoded, data_start, data_size,
		       decoder->parse_start_paddress);

	memcpy(p2v(decoder->parse_start_paddress + NAL_START_CODE_SZ),
	       reader->data_ptr + reader->NAL_offset,
	       data_size - NAL_START_CODE_SZ);

	for (i = 0; i <= DPB_frames_array_size; i++) {
		tegra_setup_FRAMEID(decoder, DPB_frames[i], i);
	}

	tegra_VDE_setup_IRAM_lists(decoder);

	tegra_VDE_write(BSEV(0x8C), 0x00000000);
	tegra_VDE_write(BSEV(CMDQUE_CONTROL),
			tegra_VDE_read(BSEV(CMDQUE_CONTROL)) | 0xF);
	tegra_VDE_write(BSEV(BSE_CONFIG),
			(tegra_VDE_read(BSEV(BSE_CONFIG)) & ~0x1F) | 0xB);
	tegra_VDE_write(BSEV(0x54), decoder->parse_limit_paddress);
	tegra_VDE_write(BSEV(0x88),
			(pic_width_in_mbs  << 11) |
			(pic_height_in_mbs << 3));

	tegra_VDE_BSEV_push_ICMDQUEUE(decoder, 0x800003FC);
	tegra_VDE_BSEV_push_ICMDQUEUE(decoder, 0x01500000 |
			((decoder->iram_unk_paddress >> 2) & 0xFFFF));
	tegra_VDE_BSEV_push_ICMDQUEUE(decoder, 0x840F054C);
	tegra_VDE_BSEV_push_ICMDQUEUE(decoder, 0x80000080);
	tegra_VDE_BSEV_push_ICMDQUEUE(decoder, 0x0E340000 |
			((decoder->iram_lists_paddress >> 2) & 0xFFFF));

	tegra_VDE_write(SXE(0x10),
			(1 << 23) |
			(pic_width_in_mbs  << 11) |
			(pic_height_in_mbs << 3) |
			0x5);
	tegra_VDE_write(SXE(0x40),
			(!baseline_profile << 17) |
			(tegra_VDE_level_idc(decoder) << 13) |
			((decoder->active_sps->log2_max_pic_order_cnt_lsb_minus4 + 4) << 7) |
			(decoder->active_sps->pic_order_cnt_type << 5) |
			(decoder->active_sps->log2_max_frame_num_minus4 + 4));
	tegra_VDE_write(SXE(0x44),
			((decoder->active_pps->pic_init_qp_minus26 + 26) << 25) |
			(decoder->active_pps->deblocking_filter_control_present_flag << 2) |
			decoder->active_pps->bottom_field_pic_order_in_frame_present_flag);
	tegra_VDE_write(SXE(0x48),
			(decoder->active_pps->constrained_intra_pred_flag << 15) |
			(decoder->sh.num_ref_idx_l1_active_minus1 << 10) |
			(decoder->sh.num_ref_idx_l0_active_minus1 << 5) |
			(decoder->active_pps->chroma_qp_index_offset & 0x1F));
	tegra_VDE_write(SXE(0x4C), 0x0C000000 | (is_B_frame << 24));
	tegra_VDE_write(SXE(0x68), (0x7 << 23) | data_size);
	tegra_VDE_write(SXE(0x6C), decoder->parse_start_paddress);

	tegra_VDE_write(MBE(0x80),
			(1 << 28) |
			(pic_width_in_mbs << 11) |
			(pic_height_in_mbs << 3) |
			0x5);

	tegra_VDE_write(MBE(0x80),
			(1 << 29) |
			(1 << 26) |
			(1 << 25) |
			(1 << 23) |
			(tegra_VDE_level_idc(decoder) << 4) |
			(!baseline_profile << 1) |
			decoder->active_sps->direct_8x8_inference_flag);

	tegra_VDE_write(MBE(0x80), 0xF4000001);
	tegra_VDE_write(MBE(0x80), 0x20000000);
	tegra_VDE_write(MBE(0x80), 0xF4000101);
	tegra_VDE_write(MBE(0x80), 0x20000000 |
		((decoder->active_pps->chroma_qp_index_offset & 0x1F) << 8));

	tegra_VDE_write(MBE(0x80), 0xD0000000 |
			(DPB_frames[0]->frame_idx << 23) |
			DPB_frames[0]->pic_order_cnt);
	tegra_VDE_write(MBE(0x80), 0xD0200000 |
			(DPB_frames[0]->frame_idx << 23));

	tegra_VDE_MBE_wait();

	if (decoder->active_sps->pic_order_cnt_type == 0) {
		switch (decoder->sh.slice_type) {
		case P:
			tegra_setup_MBE_ref_list(&decoder->ref_frames_P_list0,
						 0, 0);
			break;
		case B:
			tegra_setup_MBE_ref_list(&decoder->ref_frames_B_list0,
						 DPB_frames[0]->pic_order_cnt, 1);
			break;
		}
	}

	tegra_VDE_MBE_set_0xA_reg(0, 0x000009FC);
	tegra_VDE_MBE_set_0xA_reg(2, 0xF1DEAD00);
	tegra_VDE_MBE_set_0xA_reg(4, 0xF2DEAD00);
	tegra_VDE_MBE_set_0xA_reg(6, 0xF3DEAD00);
	tegra_VDE_MBE_set_0xA_reg(8, DPB_frames[0]->aux_data_paddr);

	tegra_VDE_write(MBE(0x80), 0x30000000 |
			(is_B_frame << 25) |
			(decoder->sh.disable_deblocking_filter_idc << 15) |
			((is_B_frame ? 0xB : 0) << 0) |
			((decoder->sh.slice_type == P) << 1) |
			((decoder->sh.slice_type == I) << 0));

	tegra_VDE_write(MBE(0x80), 0xFC000000 |
			(is_B_frame << 2) |
			((is_ref_frame && !baseline_profile) << 1));

	tegra_VDE_MBE_wait();

	ret = tegra_VDE_decode_trigger(decoder, total_mbs_nb);

	if (ret == 0) {
		decoder->frames_decoded++;
	}

	SXE_parsed = tegra_VDE_read(BSEV(0x10)) - decoder->parse_start_paddress;

	DECODER_IPRINT("Decoding %s! Total frames decoded %d, " \
		       "SXE parsed 0x%X bytes : %d macroblocks,\t" \
		       "Average ideal FPS %ld\n",
		       ret ? "failed" : "succeed",
		       decoder->frames_decoded, SXE_parsed,
		       tegra_VDE_read(SXE(0xC8)) & 0x1FFF, FPS());

	if (ret != 0) {
		tegra_VDE_stuck(decoder);
	}

	decoder->frame_decoded_notify(decoder, DPB_frames[0]);
	reader->data_offset = data_start + SXE_parsed - NAL_START_CODE_SZ;

	purge_unused_ref_frames(decoder);

	if (is_ref_frame) {
		slide_frames(decoder);
	} else {
		DECODER_DPRINT("DPB: NOT sliding frames\n");
	}
}

void decoder_init(decoder_context *decoder, void *data, uint32_t size)
{
	int i;

	bzero(decoder, sizeof(*decoder));

	bitstream_reader_selftest();
	bitstream_init(&decoder->reader, data, size);

	for (i = 0; i < ARRAY_SIZE(decoder->DPB_frames_array.frames); i++) {
		decoder->DPB_frames_array.frames[i] = malloc(sizeof(frame_data));
		assert(decoder->DPB_frames_array.frames[i] != NULL);
	}

	tegra_VDE_init(decoder);
}

void decoder_set_notify(decoder_context *decoder,
			void (*frame_decoded_notify)(decoder_context*, frame_data*),
			void *opaque)
{
	decoder->frame_decoded_notify = frame_decoded_notify;
	decoder->opaque = opaque;
}

void decoder_reset_SPS(decoder_context_sps *sps)
{
	free(sps->offset_for_ref_frame);
	bzero(sps, sizeof(*sps));
}

void decoder_reset_PPS(decoder_context_pps *pps)
{
	free(pps->run_length_minus1);
	free(pps->top_left);
	free(pps->bottom_right);
	free(pps->slice_group_id);
	bzero(pps, sizeof(*pps));
}

void decoder_reset_SH(decoder_context *decoder)
{
	free(decoder->sh.pred_weight_l0);
	free(decoder->sh.pred_weight_l1);
	bzero(&decoder->sh, sizeof(decoder->sh));
}
