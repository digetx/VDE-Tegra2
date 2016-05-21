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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "decoder.h"

void show_frames_list(frame_data **frames, int list_sz, int delim_id)
{
	int i;

	for (i = 0; i < list_sz; i++) {
		if (i == delim_id) {
			DECODER_DPRINT("DPB:\t----------------------\n");
		}

		if (frames[i]->empty) {
			DECODER_DPRINT("DPB:\tframe[%d]: empty\n", i);
			continue;
		}

		DECODER_DPRINT("DPB:\tframe[%d]: paddr = 0x%08X "  \
				"frame_num = %d frame_dec_num = %d " \
				"is_B_frame = %d frame_num_wrap = %d " \
				"pic_order_cnt = %d\n",
				i,
				frames[i]->paddr,
				frames[i]->frame_num,
				frames[i]->frame_dec_num,
				frames[i]->is_B_frame,
				frames[i]->frame_num_wrap,
				frames[i]->pic_order_cnt);
	}
}

static void clear_frame(frame_data *frame)
{
	assert(!frame->empty);

	frame->marked_for_removal = 0;
	frame->frame_num_wrap = 0;
	frame->empty = 1;
}

void clear_DPB(decoder_context *decoder)
{
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	int i;

	for (i = 1; i <= decoder->DPB_frames_array.size; i++) {
		clear_frame(DPB_frames[i]);
	}

	decoder->DPB_frames_array.size = 0;

	DECODER_DPRINT("DPB: Cleared\n");
}

int get_frame_id_with_least_pic_order_cnt(frame_data **frames,
					int list_size, int least_pic_order_cnt,
					int start_stop_pic_order_cnt, int after)
{
	int pic_order_cnt;
	int frame_id = -1;
	int i;

	DECODER_DPRINT("%s: list_size %d least_pic_order_cnt %d " \
					"start_stop_pic_order_cnt %d after %d\n",
			__func__, list_size, least_pic_order_cnt,
			start_stop_pic_order_cnt, after);

	for (i = 0; i < list_size; i++) {
		if (frames[i]->empty) {
			DECODER_DPRINT("%s: frame %d empty\n", __func__, i);
			continue;
		}

		pic_order_cnt = frames[i]->pic_order_cnt;

		if (!after && pic_order_cnt >= start_stop_pic_order_cnt) {
			DECODER_DPRINT("%s: skipped frame %d pic_order_cnt %u\n",
				       __func__, i, pic_order_cnt);
			continue;
		}

		if (after && pic_order_cnt <= start_stop_pic_order_cnt) {
			DECODER_DPRINT("%s: skipped frame %d pic_order_cnt %u\n",
				       __func__, i, pic_order_cnt);
			continue;
		}

		if (pic_order_cnt < least_pic_order_cnt) {
			DECODER_DPRINT("%s: set frame %d pic_order_cnt %u\n",
				       __func__, i, pic_order_cnt);
			least_pic_order_cnt = pic_order_cnt;
			frame_id = i;
		} else {
			DECODER_DPRINT("%s: skipped frame %d pic_order_cnt %u\n",
				       __func__, i, pic_order_cnt);
		}
	}

	return frame_id;
}

int get_frame_id_with_most_pic_order_cnt(frame_data **frames,
					int list_size, int most_pic_order_cnt,
					int start_stop_pic_order_cnt, int after)
{
	int pic_order_cnt;
	int frame_id = -1;
	int i;

	DECODER_DPRINT("%s: list_size %d most_pic_order_cnt %d " \
					"start_stop_pic_order_cnt %d after %d\n",
			__func__, list_size, most_pic_order_cnt,
			start_stop_pic_order_cnt, after);

	for (i = 0; i < list_size; i++) {
		if (frames[i]->empty) {
			DECODER_DPRINT("%s: frame %d empty\n", __func__, i);
			continue;
		}

		pic_order_cnt = frames[i]->pic_order_cnt;

		if (!after && pic_order_cnt >= start_stop_pic_order_cnt) {
			DECODER_DPRINT("%s: skipped frame %d pic_order_cnt %u\n",
				       __func__, i, pic_order_cnt);
			continue;
		}

		if (after && pic_order_cnt <= start_stop_pic_order_cnt) {
			DECODER_DPRINT("%s: skipped frame %d pic_order_cnt %u\n",
				       __func__, i, pic_order_cnt);
			continue;
		}

		if (pic_order_cnt > most_pic_order_cnt) {
			DECODER_DPRINT("%s: set frame %d pic_order_cnt %u\n",
				       __func__, i, pic_order_cnt);
			most_pic_order_cnt = pic_order_cnt;
			frame_id = i;
		} else {
			DECODER_DPRINT("%s: skipped frame %d pic_order_cnt %u\n",
				       __func__, i, pic_order_cnt);
		}
	}

	return frame_id;
}

void swap_frames(frame_data **frames, int id1, int id2)
{
	frame_data *tmp_frame = frames[id1];

	frames[id1] = frames[id2];
	frames[id2] = tmp_frame;

	DECODER_DPRINT("DPB: Swapped frames %d <-> %d\n", id1, id2);
}

void move_frame(frame_data **frames, int idx, int to_idx)
{
	for (; idx != to_idx; (to_idx < idx) ? idx-- : idx++) {
		swap_frames(frames, idx, idx + ((to_idx < idx) ? -1 : 1));
	}
}

void purge_unused_ref_frames(decoder_context *decoder)
{
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	int i;

	for (i = 1; i <= decoder->DPB_frames_array.size; i++) {
		if (!DPB_frames[i]->marked_for_removal) {
			continue;
		}

		DECODER_DPRINT("DPB: Purged ref frame! " \
				"frame_num = %d pic_order_cnt = %d\n",
			      DPB_frames[i]->frame_num,
			      DPB_frames[i]->pic_order_cnt);

		clear_frame(DPB_frames[i]);
		move_frame(DPB_frames, i, decoder->DPB_frames_array.size);
		decoder->DPB_frames_array.size--;
		i--;
	}
}

void slide_frames(decoder_context *decoder)
{
	int DPB_size = decoder->active_sps->max_num_ref_frames;
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	frame_data *last_frame = NULL;
	int i;

	DECODER_DPRINT("DPB[%d]: Sliding frames\n", DPB_size);

	switch (decoder->active_sps->pic_order_cnt_type) {
	case 0:
	case 2:
		last_frame = DPB_frames[DPB_size];

		for (i = DPB_size; i > 0; i--) {
			DPB_frames[i] = DPB_frames[i - 1];
		}

		DPB_frames[0] = last_frame;

		if (last_frame->empty) {
			last_frame = NULL;
		} else {
			clear_frame(last_frame);
		}
		break;
	default:
		DECODER_ERR("Shouldn't be here\n");
	}

	if (decoder->DPB_frames_array.size < DPB_size) {
		decoder->DPB_frames_array.size++;
	}

	if (last_frame != NULL) {
		DECODER_DPRINT("DPB: dropped frame_num = %d pic_order_cnt = %d\n",
			       last_frame->frame_num, last_frame->pic_order_cnt);
	}
}

void form_P_frame_ref_list_l0(decoder_context *decoder)
{
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	frame_data **REF_frames = decoder->ref_frames_P_list0.frames;
	int REF_list_size = decoder->DPB_frames_array.size;
	int start_stop_pic_order_cnt = INT_MAX;
	int frame_id;
	int i;

	if (decoder->active_sps->pic_order_cnt_type == 2) {
		decoder->ref_frames_P_list0.size = REF_list_size;

		for (i = 0; i < REF_list_size; i++) {
			REF_frames[i] = DPB_frames[i + 1];
		}

		goto end;
	}

	for (i = 0; i < REF_list_size; i++) {
		frame_id = get_frame_id_with_most_pic_order_cnt(
				DPB_frames + 1, REF_list_size,
				-1, start_stop_pic_order_cnt, 0);
		if (frame_id < 0) {
			break;
		}

		REF_frames[i] = DPB_frames[frame_id + 1];
		start_stop_pic_order_cnt = REF_frames[i]->pic_order_cnt;
	}

	decoder->ref_frames_P_list0.size = i;
end:
	DECODER_DPRINT("REF list 0:\n");
	show_frames_list(REF_frames, decoder->ref_frames_P_list0.size,
			 decoder->sh.num_ref_idx_l0_active_minus1 + 1);

	if (i < decoder->sh.num_ref_idx_l0_active_minus1 + 1) {
		DECODER_ERR("Shouldn't happen: ref_frames_P_list0.size=%d "
						"num_ref_idx_l0_active=%d\n",
			    decoder->ref_frames_P_list0.size,
			    decoder->sh.num_ref_idx_l0_active_minus1 + 1);
	}

	decoder->ref_frames_P_list0.size =
				decoder->sh.num_ref_idx_l0_active_minus1 + 1;
}

void form_B_frame_ref_list_l0(decoder_context *decoder)
{
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	frame_data **REF_frames = decoder->ref_frames_B_list0.frames;
	int REF_list_size = decoder->DPB_frames_array.size;
	int start_stop_pic_order_cnt = DPB_frames[0]->pic_order_cnt;
	int frame_id;
	int i = 0;

	DECODER_DPRINT("B L0 REF_list_size %d\n", REF_list_size);

	for (;; i++) {
		if (i == REF_list_size) {
			DECODER_ERR("Shouldn't happen\n");
		}

		frame_id = get_frame_id_with_most_pic_order_cnt(
				DPB_frames + 1, REF_list_size,
				-1, start_stop_pic_order_cnt, 0);
		if (frame_id < 0) {
			break;
		}

		DECODER_DPRINT("B L0 before %d\n", frame_id);

		REF_frames[i] = DPB_frames[frame_id + 1];
		start_stop_pic_order_cnt = REF_frames[i]->pic_order_cnt;
	}

	start_stop_pic_order_cnt = DPB_frames[0]->pic_order_cnt;

	for (;; i++) {
		frame_id = get_frame_id_with_least_pic_order_cnt(
				DPB_frames + 1, REF_list_size,
				INT_MAX, start_stop_pic_order_cnt, 1);
		if (frame_id < 0) {
			break;
		}

		DECODER_DPRINT("B L0 after %d\n", frame_id);

		if (i == REF_list_size) {
			DECODER_ERR("Shouldn't happen\n");
		}

		REF_frames[i] = DPB_frames[frame_id + 1];
		start_stop_pic_order_cnt = REF_frames[i]->pic_order_cnt;
	}

	decoder->ref_frames_B_list0.size = i;

	DECODER_DPRINT("REF list 0:\n");
	show_frames_list(REF_frames, decoder->ref_frames_B_list0.size,
			 decoder->sh.num_ref_idx_l0_active_minus1 + 1);

	if (i < decoder->sh.num_ref_idx_l0_active_minus1 + 1) {
		DECODER_ERR("Shouldn't happen: ref_frames_B_list0.size=%d "
						"num_ref_idx_l0_active=%d\n",
			    decoder->ref_frames_B_list0.size,
			    decoder->sh.num_ref_idx_l0_active_minus1 + 1);
	}

	decoder->ref_frames_B_list0.size =
				decoder->sh.num_ref_idx_l0_active_minus1 + 1;
}

void form_B_frame_ref_list_l1(decoder_context *decoder)
{
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	frame_data **REF_frames = decoder->ref_frames_B_list1.frames;
	int REF_list_size = decoder->DPB_frames_array.size;
	int start_stop_pic_order_cnt = DPB_frames[0]->pic_order_cnt;
	int frame_id;
	int i = 0;

	for (;; i++) {
		if (i == REF_list_size) {
			DECODER_ERR("Shouldn't happen\n");
		}

		frame_id = get_frame_id_with_least_pic_order_cnt(
				DPB_frames + 1, REF_list_size, INT_MAX,
				start_stop_pic_order_cnt, 1);
		if (frame_id < 0) {
			break;
		}

		DECODER_DPRINT("B L1 after %d\n", frame_id);

		REF_frames[i] = DPB_frames[frame_id + 1];
		start_stop_pic_order_cnt = REF_frames[i]->pic_order_cnt;
	}

	start_stop_pic_order_cnt = DPB_frames[0]->pic_order_cnt;

	for (;; i++) {
		frame_id = get_frame_id_with_most_pic_order_cnt(
				DPB_frames + 1, REF_list_size, -1,
				start_stop_pic_order_cnt, 0);
		if (frame_id < 0) {
			break;
		}

		DECODER_DPRINT("B L1 before %d\n", frame_id);

		if (i == REF_list_size) {
			DECODER_ERR("Shouldn't happen\n");
		}

		REF_frames[i] = DPB_frames[frame_id + 1];
		start_stop_pic_order_cnt = REF_frames[i]->pic_order_cnt;
	}

	decoder->ref_frames_B_list1.size = i;

	DECODER_DPRINT("REF list 1:\n");
	show_frames_list(REF_frames, decoder->ref_frames_B_list1.size,
			 decoder->sh.num_ref_idx_l1_active_minus1 + 1);

	if (i < decoder->sh.num_ref_idx_l1_active_minus1 + 1) {
		DECODER_ERR("Shouldn't happen: ref_frames_B_list1.size %d "
						"num_ref_idx_l1_active %d\n",
			    decoder->ref_frames_B_list1.size,
			    decoder->sh.num_ref_idx_l1_active_minus1 + 1);
	}

	decoder->ref_frames_B_list1.size =
				decoder->sh.num_ref_idx_l1_active_minus1 + 1;
}
