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

#include "common.h"

#define Extended_SAR	255

#define SYNTAX_VPRINT(f, ...)	printf(f, ## __VA_ARGS__)

static void hrd_parameters(decoder_context *decoder)
{
	bitstream_reader *reader = &decoder->reader;
	unsigned cpb_cnt_minus1 = bitstream_read_ue(reader);
	unsigned SchedSelIdx;

	SYNTAX_VPRINT("cpb_cnt_minus1 = %u\n", cpb_cnt_minus1);
	SYNTAX_VPRINT("bit_rate_scale = %u\n", bitstream_read_u(reader, 4));
	SYNTAX_VPRINT("cpb_size_scale = %u\n", bitstream_read_u(reader, 4));

	for (SchedSelIdx = 0; SchedSelIdx <= cpb_cnt_minus1; SchedSelIdx++) {
		SYNTAX_VPRINT("bit_rate_value_minus1[%u] = %u\n",
			      SchedSelIdx, bitstream_read_ue(reader));
		SYNTAX_VPRINT("cpb_size_value_minus1[%u] = %u\n",
			      SchedSelIdx, bitstream_read_ue(reader));
		SYNTAX_VPRINT("cbr_flag[%u] = %u\n",
			      SchedSelIdx, bitstream_read_u(reader, 1));
	}

	SYNTAX_VPRINT("initial_cpb_removal_delay_length_minus1 = %u\n",
		      bitstream_read_u(reader, 5));
	SYNTAX_VPRINT("cpb_removal_delay_length_minus1 = %u\n",
		      bitstream_read_u(reader, 5));
	SYNTAX_VPRINT("dpb_output_delay_length_minus1 = %u\n",
		      bitstream_read_u(reader, 5));
	SYNTAX_VPRINT("time_offset_length = %u\n",
		      bitstream_read_u(reader, 5));
}

void SPS_vui_parameters(decoder_context *decoder)
{
	bitstream_reader *reader = &decoder->reader;
	unsigned aspect_ratio_info_present_flag;
	unsigned aspect_ratio_idc;
	unsigned overscan_info_present_flag;
	unsigned video_signal_type_present_flag;
	unsigned colour_description_present_flag;
	unsigned chroma_loc_info_present_flag;
	unsigned timing_info_present_flag;
	unsigned nal_hrd_parameters_present_flag;
	unsigned vcl_hrd_parameters_present_flag;
	unsigned bitstream_restriction_flag;

	aspect_ratio_info_present_flag = bitstream_read_u(reader, 1);

	SYNTAX_VPRINT("aspect_ratio_info_present_flag = %u\n",
		      aspect_ratio_info_present_flag);

	if (aspect_ratio_info_present_flag) {
		aspect_ratio_idc = bitstream_read_u(reader, 8);

		SYNTAX_VPRINT("aspect_ratio_idc = %u\n", aspect_ratio_idc);

		if (aspect_ratio_idc == Extended_SAR) {
			SYNTAX_VPRINT("sar_width = %u\n",
				      bitstream_read_u(reader, 16));
			SYNTAX_VPRINT("sar_height = %u\n",
				      bitstream_read_u(reader, 16));
		}
	}

	overscan_info_present_flag = bitstream_read_u(reader, 1);

	SYNTAX_VPRINT("overscan_info_present_flag = %u\n",
		      overscan_info_present_flag);

	if (overscan_info_present_flag) {
		SYNTAX_VPRINT("overscan_appropriate_flag = %u\n",
			      bitstream_read_u(reader, 1));
	}

	video_signal_type_present_flag = bitstream_read_u(reader, 1);

	SYNTAX_VPRINT("video_signal_type_present_flag = %u\n",
		      video_signal_type_present_flag);

	if (video_signal_type_present_flag) {
		SYNTAX_VPRINT("video_format = %u\n",
			      bitstream_read_u(reader, 3));
		SYNTAX_VPRINT("video_full_range_flag = %u\n",
			      bitstream_read_u(reader, 1));

		colour_description_present_flag = bitstream_read_u(reader, 1);

		SYNTAX_VPRINT("colour_description_present_flag = %u\n",
			      colour_description_present_flag);

		if (colour_description_present_flag) {
			SYNTAX_VPRINT("colour_primaries = %u\n",
				      bitstream_read_u(reader, 8));
			SYNTAX_VPRINT("transfer_characteristics = %u\n",
				      bitstream_read_u(reader, 8));
			SYNTAX_VPRINT("matrix_coefficients = %u\n",
				      bitstream_read_u(reader, 8));
		}
	}

	chroma_loc_info_present_flag = bitstream_read_u(reader, 1);

	SYNTAX_VPRINT("chroma_loc_info_present_flag = %u\n",
		      chroma_loc_info_present_flag);

	if (chroma_loc_info_present_flag) {
		SYNTAX_VPRINT("chroma_sample_loc_type_top_field = %u\n",
			      bitstream_read_ue(reader));
		SYNTAX_VPRINT("chroma_sample_loc_type_bottom_field = %u\n",
			      bitstream_read_ue(reader));
	}

	timing_info_present_flag = bitstream_read_u(reader, 1);

	SYNTAX_VPRINT("timing_info_present_flag = %u\n",
		      timing_info_present_flag);

	if (timing_info_present_flag) {
		SYNTAX_VPRINT("num_units_in_tick = %u\n",
			      bitstream_read_u(reader, 32));
		SYNTAX_VPRINT("time_scale = %u\n",
			      bitstream_read_u(reader, 32));
		SYNTAX_VPRINT("fixed_frame_rate_flag = %u\n",
			      bitstream_read_u(reader, 1));
	}

	nal_hrd_parameters_present_flag = bitstream_read_u(reader, 1);

	SYNTAX_VPRINT("nal_hrd_parameters_present_flag = %u\n",
		      nal_hrd_parameters_present_flag);

	if (nal_hrd_parameters_present_flag) {
		hrd_parameters(decoder);
	}

	vcl_hrd_parameters_present_flag = bitstream_read_u(reader, 1);

	SYNTAX_VPRINT("vcl_hrd_parameters_present_flag = %u\n",
		      vcl_hrd_parameters_present_flag);

	if (vcl_hrd_parameters_present_flag) {
		hrd_parameters(decoder);
	}

	if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag) {
		SYNTAX_VPRINT("low_delay_hrd_flag = %u\n",
			      bitstream_read_u(reader, 1));
	}

	SYNTAX_VPRINT("pic_struct_present_flag = %u\n",
		      bitstream_read_u(reader, 1));

	bitstream_restriction_flag = bitstream_read_u(reader, 1);

	SYNTAX_VPRINT("bitstream_restriction_flag = %u\n",
		      bitstream_restriction_flag);

	if (bitstream_restriction_flag) {
		SYNTAX_VPRINT("motion_vectors_over_pic_boundaries_flag = %u\n",
			      bitstream_read_u(reader, 1));
		SYNTAX_VPRINT("max_bytes_per_pic_denom = %u\n",
			      bitstream_read_ue(reader));
		SYNTAX_VPRINT("max_bits_per_mb_denom = %u\n",
			      bitstream_read_ue(reader));
		SYNTAX_VPRINT("log2_max_mv_length_horizontal = %u\n",
			      bitstream_read_ue(reader));
		SYNTAX_VPRINT("log2_max_mv_length_vertical = %u\n",
			      bitstream_read_ue(reader));
		SYNTAX_VPRINT("max_num_reorder_frames = %u\n",
			      bitstream_read_ue(reader));
		SYNTAX_VPRINT("max_dec_frame_buffering = %u\n",
			      bitstream_read_ue(reader));
	}
}
