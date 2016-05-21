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

#ifndef DECODER_H
#define DECODER_H

#include <assert.h>
#include <stdint.h>
#include <sys/types.h>

#include "bitstream.h"

#define min(a, b) ((a < b) ? a : b)
#define max(a, b) ((a > b) ? a : b)

#define P	0
#define B	1
#define I	2
#define SP	3
#define SI	4
#define P_ONLY	5
#define B_ONLY	6
#define I_ONLY	7
#define SP_ONLY	8
#define SI_ONLY	9

#define IdrPicFlag	(decoder->nal.unit_type == 5)

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(*(x)))

#define DECODER_IPRINT(f, ...)	printf(f, ## __VA_ARGS__)
#define DECODER_DPRINT(f, ...)	printf(f, ## __VA_ARGS__)

// #define DECODER_IPRINT(f, ...) {}
// #define DECODER_DPRINT(f, ...) {}

#define DECODER_ERR(f, ...)				\
{							\
	fprintf(stderr, "%s:%d:\n", __FILE__, __LINE__);\
	fprintf(stderr, "error! decode: %s: "		\
		f, __func__, ## __VA_ARGS__);		\
	abort();					\
}

typedef struct decoder_context_sps {
	unsigned valid:1;

	unsigned profile_idc:8;
	unsigned constraint_set0_flag:1;
	unsigned constraint_set1_flag:1;
	unsigned constraint_set2_flag:1;
	unsigned constraint_set3_flag:1;
	unsigned constraint_set4_flag:1;
	unsigned constraint_set5_flag:1;
	unsigned level_idc:8;
	uint32_t seq_parameter_set_id;
	uint32_t chroma_format_idc;
	unsigned separate_colour_plane_flag:1;
	uint32_t bit_depth_luma_minus8;
	uint32_t bit_depth_chroma_minus8;
	unsigned qpprime_y_zero_transform_bypass_flag:1;
	unsigned seq_scaling_matrix_present_flag:1;
	unsigned seq_scaling_list_present_flag:12;
	uint32_t log2_max_frame_num_minus4;
	uint32_t pic_order_cnt_type;
	uint32_t log2_max_pic_order_cnt_lsb_minus4;
	unsigned delta_pic_order_always_zero_flag:1;
	int32_t  offset_for_non_ref_pic;
	int32_t  offset_for_top_to_bottom_field;
	uint32_t num_ref_frames_in_pic_order_cnt_cycle;
	int32_t  *offset_for_ref_frame;
	uint32_t max_num_ref_frames;
	unsigned gaps_in_frame_num_value_allowed_flag:1;
	uint32_t pic_width_in_mbs_minus1;
	uint32_t pic_height_in_map_units_minus1;
	unsigned frame_mbs_only_flag:1;
	unsigned mb_adaptive_frame_field_flag:1;
	unsigned direct_8x8_inference_flag:1;
	unsigned frame_cropping_flag:1;
	uint32_t frame_crop_left_offset;
	uint32_t frame_crop_right_offset;
	uint32_t frame_crop_top_offset;
	uint32_t frame_crop_bottom_offset;
	unsigned vui_parameters_present_flag:1;
	unsigned UseDefaultScalingMatrix4x4Flag[6];
	unsigned UseDefaultScalingMatrix8x8Flag[6];
	int8_t scalingList_4x4[6][16];
	int8_t scalingList_8x8[6][64];
} decoder_context_sps;

typedef struct decoder_context_pps {
	unsigned valid:1;

	uint32_t pic_parameter_set_id;
	uint32_t seq_parameter_set_id;
	unsigned entropy_coding_mode_flag:1;
	unsigned bottom_field_pic_order_in_frame_present_flag:1;
	uint32_t num_slice_groups_minus1;
	uint32_t slice_group_map_type;
	uint32_t *run_length_minus1;
	uint32_t *top_left;
	uint32_t *bottom_right;
	unsigned slice_group_change_direction_flag:1;
	uint32_t slice_group_change_rate_minus1;
	uint32_t pic_size_in_map_units_minus1;
	uint32_t *slice_group_id;
	uint32_t num_ref_idx_l0_default_active_minus1;
	uint32_t num_ref_idx_l1_default_active_minus1;
	unsigned weighted_pred_flag:1;
	unsigned weighted_bipred_idc:2;
	int32_t  pic_init_qp_minus26;
	int32_t  pic_init_qs_minus26;
	int32_t  chroma_qp_index_offset;
	unsigned deblocking_filter_control_present_flag:1;
	unsigned constrained_intra_pred_flag:1;
	unsigned redundant_pic_cnt_present_flag:1;
	unsigned transform_8x8_mode_flag:1;
	unsigned pic_scaling_matrix_present_flag:1;
	unsigned UseDefaultScalingMatrix4x4Flag[6];
	unsigned UseDefaultScalingMatrix8x8Flag[6];
	int8_t   scalingList_4x4[6][16];
	int8_t   scalingList_8x8[6][64];
	int32_t  second_chroma_qp_index_offset;
} decoder_context_pps;

typedef struct pred_weight {
	unsigned luma_weight_l_flag:1;
	int32_t  luma_weight_l;
	int32_t  luma_offset_l;
	unsigned chroma_weight_l_flag:1;
	int32_t  chroma_weight_l[2];
	int32_t  chroma_offset_l[2];
} pred_weight;

typedef struct slice_header {
	uint32_t first_mb_in_slice;
	uint32_t slice_type;
	unsigned colour_plane_id:2;
	uint32_t frame_num;
	unsigned field_pic_flag:1;
	unsigned bottom_field_flag:1;
	uint32_t idr_pic_id;
	uint32_t pic_order_cnt_lsb;
	int32_t  delta_pic_order_cnt_bottom;
	int32_t  delta_pic_order_cnt[2];
	uint32_t redundant_pic_cnt;
	unsigned direct_spatial_mv_pred_flag:1;
	unsigned num_ref_idx_active_override_flag:1;
	uint32_t num_ref_idx_l0_active_minus1;
	uint32_t num_ref_idx_l1_active_minus1;
	uint32_t cabac_init_idc;
	int32_t  slice_qp_delta;
	unsigned sp_for_switch_flag:1;
	int32_t  slice_qs_delta;
	uint32_t disable_deblocking_filter_idc;
	int32_t  slice_alpha_c0_offset_div2;
	int32_t  slice_beta_offset_div2;
	uint32_t slice_group_change_cycle;
	uint32_t luma_log2_weight_denom;
	uint32_t chroma_log2_weight_denom;
	pred_weight *pred_weight_l0;
	pred_weight *pred_weight_l1;
	unsigned no_output_of_prior_pics_flag:1;
	unsigned long_term_reference_flag:1;
} slice_header;

typedef struct nal_header {
	unsigned ref_idc:2;
	unsigned unit_type:5;
} nal_header;

typedef struct frame_data {
	int frame_dec_num;
	int frame_num;
	int frame_idx;
	int pic_order_cnt;
	uint32_t paddr;
	uint32_t aux_data_paddr;
	unsigned empty:1;
	unsigned marked_for_removal:1;
	unsigned is_B_frame:1;
	unsigned dirty:1;
	unsigned frame_num_wrap:1;
	decoder_context_sps *sps;
} frame_data;

typedef struct frames_list {
	frame_data *frames[1 + 16];
	unsigned size;
} frames_list;

typedef struct decoder_context {
	bitstream_reader reader;

	void (*frame_decoded_notify)(struct decoder_context *decoder,
				     frame_data *frame);
	void *opaque;

	decoder_context_sps sps[32];
	decoder_context_pps pps[256];

	decoder_context_sps *active_sps;
	decoder_context_pps *active_pps;

	nal_header   nal;
	slice_header sh;

	frames_list DPB_frames_array;
	frames_list ref_frames_P_list0;
	frames_list ref_frames_B_list0;
	frames_list ref_frames_B_list1;

	int NAL_start_delim;
	int frames_decoded;
	int prev_frame_num;
	int prevPicOrderCntMsb;
	int prevPicOrderCntLsb;
	int running;

	uint32_t parse_limit_paddress;
	uint32_t parse_start_paddress;

	uint32_t iram_lists_paddress;
	uint32_t iram_unk_paddress;

	time_t dec_time_acc;
} decoder_context;

void decoder_init(decoder_context *decoder, void *data, uint32_t size);

void decoder_set_notify(decoder_context *decoder,
			void (*frame_decoded_notify)(decoder_context*,
						     frame_data*),
			void *opaque);

void decode_current_slice(decoder_context *decoder, unsigned last_mb_id);

size_t decoder_image_frame_size(decoder_context *decoder);

void tegra_VDE_decode_frame(decoder_context *decoder);

void show_frames_list(frame_data **frames, int list_sz, int delim_id);

void clear_DPB(decoder_context *decoder);

int get_frame_id_with_least_pic_order_cnt(frame_data **frames,
					int list_size, int least_pic_order_cnt,
					int start_stop_pic_order_cnt, int after);

int get_frame_id_with_most_pic_order_cnt(frame_data **frames,
					int list_size, int most_pic_order_cnt,
					int start_stop_pic_order_cnt, int after);

void swap_frames(frame_data **frames, int id1, int id2);

void slide_frames(decoder_context *decoder);

void form_P_frame_ref_list_l0(decoder_context *decoder);

void form_B_frame_ref_list_l0(decoder_context *decoder);

void form_B_frame_ref_list_l1(decoder_context *decoder);

void move_frame(frame_data **frames, int idx, int to_idx);

void purge_unused_ref_frames(decoder_context *decoder);

void * p2v(uint32_t paddr);

#endif // DECODER_H
