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

static const char * SLICE_TYPE(int type)
{
	switch (type) {
	case P:		return "P";
	case B:		return "B";
	case I:		return "I";
	case SP:	return "SP";
	case SI:	return "SI";
	case P_ONLY:	return "P_ONLY";
	case B_ONLY:	return "B_ONLY";
	case I_ONLY:	return "I_ONLY";
	case SP_ONLY:	return "SP_ONLY";
	case SI_ONLY:	return "SI_ONLY";
	default:
		break;
	}

	SYNTAX_ERR("slice_type is malformed\n");

	return "Bad value";
}

void parse_slice_header(decoder_context *decoder)
{
	frame_data **DPB_frames = decoder->DPB_frames_array.frames;
	frames_list *REF_frames_list;
	bitstream_reader *reader = &decoder->reader;
	decoder_context_sps *sps;
	decoder_context_pps *pps;
	unsigned wrapped_frame_num;
	unsigned pps_id;
	int max_frame_num;
	int log2_max_pic_order_cnt_lsb = 0;
	int pic_order_cnt_lsb = 0;
	int PicOrderCntMsb = 0;
	int MaxPicOrderCntLsb;
	int slice_type;
	int i;

	decoder_reset_SH(decoder);

	decoder->sh.first_mb_in_slice = bitstream_read_ue(reader);
	decoder->sh.slice_type = bitstream_read_ue(reader);
	pps_id = bitstream_read_ue(reader);

	SYNTAX_IPRINT("first_mb_in_slice = %u\n", decoder->sh.first_mb_in_slice);
	SYNTAX_IPRINT("slice_type %u = \"%s\"\n",
		      decoder->sh.slice_type, SLICE_TYPE(decoder->sh.slice_type));
	SYNTAX_IPRINT("pic_parameter_set_id = %u\n", pps_id);

	if (pps_id > 255) {
		SYNTAX_ERR("Slice header is malformed, pps_id overflow\n");
	}

	if (!decoder->pps[pps_id].valid) {
		SYNTAX_ERR("Cannot parse slice while PPS is invalid\n");
	}

	decoder->active_pps = &decoder->pps[pps_id];
	pps = decoder->active_pps;

	decoder->active_sps = &decoder->sps[pps->seq_parameter_set_id];
	sps = decoder->active_sps;

	max_frame_num = 1 << (sps->log2_max_frame_num_minus4 + 4);

	slice_type = decoder->sh.slice_type;
	decoder->sh.slice_type %= 5;

	if (sps->separate_colour_plane_flag) {
		decoder->sh.colour_plane_id = bitstream_read_u(reader, 2);

		SYNTAX_IPRINT("colour_plane_id = %u\n", decoder->sh.colour_plane_id);
	}

	decoder->sh.frame_num =
		bitstream_read_u(reader, sps->log2_max_frame_num_minus4 + 4);

	SYNTAX_IPRINT("frame_num = %u\n", decoder->sh.frame_num);

	if (!sps->frame_mbs_only_flag) {
		decoder->sh.field_pic_flag = bitstream_read_u(reader, 1);

		SYNTAX_IPRINT("field_pic_flag = %u\n", decoder->sh.field_pic_flag);

		if (decoder->sh.field_pic_flag) {
			decoder->sh.bottom_field_flag = bitstream_read_u(reader, 1);

			SYNTAX_IPRINT("bottom_field_flag = %u\n",
				      decoder->sh.bottom_field_flag);
		}
	}

	if (IdrPicFlag) {
		decoder->sh.idr_pic_id = bitstream_read_ue(reader);

		SYNTAX_IPRINT("idr_pic_id = %u\n", decoder->sh.idr_pic_id);

		clear_DPB(decoder);
	}

	if (sps->pic_order_cnt_type == 0) {
		log2_max_pic_order_cnt_lsb = sps->log2_max_pic_order_cnt_lsb_minus4 + 4;

		decoder->sh.pic_order_cnt_lsb =
			bitstream_read_u(reader, log2_max_pic_order_cnt_lsb);

		SYNTAX_IPRINT("pic_order_cnt_lsb = %u\n",
			      decoder->sh.pic_order_cnt_lsb);

		if (pps->bottom_field_pic_order_in_frame_present_flag &&
			!decoder->sh.field_pic_flag)
		{
			decoder->sh.delta_pic_order_cnt_bottom = bitstream_read_se(reader);

			SYNTAX_IPRINT("delta_pic_order_cnt_bottom = %d\n",
				      decoder->sh.delta_pic_order_cnt_bottom);
		}

		if (IdrPicFlag) {
			decoder->prevPicOrderCntLsb = 0;
			decoder->prevPicOrderCntMsb = 0;
		}

		pic_order_cnt_lsb = decoder->sh.pic_order_cnt_lsb;

		MaxPicOrderCntLsb = (1 << log2_max_pic_order_cnt_lsb);

		if ((pic_order_cnt_lsb < decoder->prevPicOrderCntLsb) &&
			((decoder->prevPicOrderCntLsb - pic_order_cnt_lsb) >= (MaxPicOrderCntLsb / 2)))
		{
			PicOrderCntMsb = decoder->prevPicOrderCntMsb + MaxPicOrderCntLsb;
		} else if ((pic_order_cnt_lsb > decoder->prevPicOrderCntLsb) &&
			((pic_order_cnt_lsb - decoder->prevPicOrderCntLsb) > (MaxPicOrderCntLsb / 2)))
		{
			PicOrderCntMsb = decoder->prevPicOrderCntMsb - MaxPicOrderCntLsb;
		} else {
			PicOrderCntMsb = decoder->prevPicOrderCntMsb;
		}

		decoder->prevPicOrderCntLsb = pic_order_cnt_lsb;
		decoder->prevPicOrderCntMsb = PicOrderCntMsb;
	}

	if (sps->pic_order_cnt_type == 1 &&
		!sps->delta_pic_order_always_zero_flag)
	{
		decoder->sh.delta_pic_order_cnt[0] = bitstream_read_se(reader);

		SYNTAX_IPRINT("delta_pic_order_cnt[0] = %d\n",
			      decoder->sh.delta_pic_order_cnt[0]);

		if (pps->bottom_field_pic_order_in_frame_present_flag &&
			!decoder->sh.field_pic_flag)
		{
			decoder->sh.delta_pic_order_cnt[1] = bitstream_read_se(reader);

			SYNTAX_IPRINT("delta_pic_order_cnt[1] = %d\n",
				      decoder->sh.delta_pic_order_cnt[1]);
		}
	}

	if (pps->redundant_pic_cnt_present_flag) {
		decoder->sh.redundant_pic_cnt = bitstream_read_ue(reader);

		SYNTAX_IPRINT("redundant_pic_cnt = %u\n",
			      decoder->sh.redundant_pic_cnt);

		SYNTAX_ERR("redundant_pic_cnt skip unimplemented!\n");
	}

	switch (decoder->sh.slice_type) {
	case SI:
	case SP:
		SYNTAX_ERR("%s slice unsupported!\n",
			   SLICE_TYPE(decoder->sh.slice_type));
		break;
	case B:
		decoder->sh.direct_spatial_mv_pred_flag =
						bitstream_read_u(reader, 1);

		SYNTAX_IPRINT("direct_spatial_mv_pred_flag = %u\n",
			      decoder->sh.direct_spatial_mv_pred_flag);
	case P:
		decoder->sh.num_ref_idx_active_override_flag =
						bitstream_read_u(reader, 1);

		SYNTAX_IPRINT("num_ref_idx_active_override_flag = %u\n",
			      decoder->sh.num_ref_idx_active_override_flag);

		if (!decoder->sh.num_ref_idx_active_override_flag) {
			decoder->sh.num_ref_idx_l0_active_minus1 =
					pps->num_ref_idx_l0_default_active_minus1;
			break;
		}

		decoder->sh.num_ref_idx_l0_active_minus1 =
						bitstream_read_ue(reader);

		SYNTAX_IPRINT("num_ref_idx_l0_active_minus1 = %u\n",
			      decoder->sh.num_ref_idx_l0_active_minus1);

		if (decoder->sh.slice_type != B) {
			break;
		}

		decoder->sh.num_ref_idx_l1_active_minus1 =
						bitstream_read_ue(reader);

		SYNTAX_IPRINT("num_ref_idx_l1_active_minus1 = %u\n",
			      decoder->sh.num_ref_idx_l1_active_minus1);
		break;
	default:
		break;
	}

	if (!sps->gaps_in_frame_num_value_allowed_flag && !IdrPicFlag) {
		if (decoder->prev_frame_num != decoder->sh.frame_num) {
			wrapped_frame_num = (decoder->prev_frame_num + 1) % max_frame_num;

			if (abs(decoder->sh.frame_num - wrapped_frame_num) > 1) {
				SYNTAX_IPRINT("prev_frame_num = %d frame_num = %d\n",
					decoder->prev_frame_num,
					decoder->sh.frame_num);
				abort();
			}
		}
	}

	decoder->prev_frame_num = decoder->sh.frame_num;

	if (decoder->sh.frame_num == 0) {
		for (i = 1; i <= decoder->DPB_frames_array.size; i++) {
			DPB_frames[i]->frame_num_wrap = 1;
		}
	}

	DPB_frames[0]->frame_dec_num = decoder->frames_decoded;
	DPB_frames[0]->frame_num = decoder->sh.frame_num;
	DPB_frames[0]->pic_order_cnt = PicOrderCntMsb | pic_order_cnt_lsb;
	DPB_frames[0]->is_B_frame = (slice_type == B); // Not B_ONLY!
	DPB_frames[0]->sps = sps;
	DPB_frames[0]->empty = 0;

	DECODER_DPRINT("DPB:\n");
	show_frames_list(DPB_frames,
			 ARRAY_SIZE(decoder->DPB_frames_array.frames),
			 decoder->active_sps->max_num_ref_frames + 1);

	switch (decoder->sh.slice_type) {
	case P:
		form_P_frame_ref_list_l0(decoder);
		break;
	case B:
		form_B_frame_ref_list_l0(decoder);
		form_B_frame_ref_list_l1(decoder);
		break;
	default:
		break;
	}

	// ref_pic_list_modification( )
	{
		unsigned ref_pic_list_modification_flag_l0;
		unsigned ref_pic_list_modification_flag_l1;
		uint32_t modification_of_pic_nums_idc;
		uint32_t long_term_pic_num;
		int32_t abs_diff_pic_num;
		int predicted_picture = decoder->sh.frame_num;
		int remapped_picture;
		int refIdxL = 0;
		int l1 = 0;

		switch (decoder->sh.slice_type) {
		case P:
		case B:
		case SP:
			ref_pic_list_modification_flag_l0 = bitstream_read_u(reader, 1);

			SYNTAX_IPRINT("ref_pic_list_modification_flag_l0 = %u\n",
				      ref_pic_list_modification_flag_l0);

			if (!ref_pic_list_modification_flag_l0) {
				goto flag_l1;
			}
pics_num:
			modification_of_pic_nums_idc = bitstream_read_ue(reader);

			SYNTAX_IPRINT("modification_of_pic_nums_idc = %u\n",
				      modification_of_pic_nums_idc);

			switch (decoder->sh.slice_type) {
			case P:
				REF_frames_list = &decoder->ref_frames_P_list0;
				break;
			case B:
				if (!l1) {
					REF_frames_list = &decoder->ref_frames_B_list0;
				} else {
					REF_frames_list = &decoder->ref_frames_B_list1;
				}
				break;
			default:
				break;
			}

			switch (modification_of_pic_nums_idc) {
			case 0 ... 1:
				abs_diff_pic_num = bitstream_read_ue(reader) + 1;

				if (modification_of_pic_nums_idc == 0) {
					abs_diff_pic_num = -abs_diff_pic_num;
				}

				SYNTAX_IPRINT("abs_diff_pic_num = %d\n",
					      abs_diff_pic_num);

				remapped_picture = predicted_picture + abs_diff_pic_num;
				remapped_picture &= max_frame_num - 1;

				SYNTAX_IPRINT("refIdxL%d[%d] <- %d\n",
					      l1, refIdxL, remapped_picture);

				predicted_picture = remapped_picture;

				for (i = 0; i < REF_frames_list->size; i++) {
					if (REF_frames_list->frames[i]->frame_num != predicted_picture) {
						continue;
					}

					assert(remapped_picture != -1);
					remapped_picture = -1;

					move_frame(REF_frames_list->frames,
						   i, refIdxL++);
				}

				assert(remapped_picture == -1);

				DECODER_DPRINT("modified REF list %d:\n", l1);
				show_frames_list(REF_frames_list->frames,
						 REF_frames_list->size, -1);

				goto pics_num;
			case 2:
				long_term_pic_num = bitstream_read_ue(reader);

				SYNTAX_ERR("list_modification unimplemented!\n");

				SYNTAX_IPRINT("long_term_pic_num = %u\n",
					      long_term_pic_num);
				goto pics_num;
flag_l1:		case 3:
				if (l1 || decoder->sh.slice_type == P) {
					break;
				}

				ref_pic_list_modification_flag_l1 =
						bitstream_read_u(reader, 1);

				SYNTAX_IPRINT("ref_pic_list_modification_flag_l1 = %u\n",
						ref_pic_list_modification_flag_l1);

				if (ref_pic_list_modification_flag_l1) {
					refIdxL = 0;
					l1 = 1;
					goto pics_num;
				}
				break;
			default:
				SYNTAX_ERR("slice header is malformed\n");
			}
			break;
		default:
			break;
		}
	}

	switch (decoder->sh.slice_type) {
	case P:
	case SP:
		if (!pps->weighted_pred_flag) {
			break;
		}
		goto pred_weight_table;
	case B:
		if (!pps->weighted_bipred_idc) {
			break;
		}
pred_weight_table:
{
		pred_weight **pw;
		int sz, l = 0;

		SYNTAX_ERR("pred_weight_table unimplemented!\n");

		decoder->sh.luma_log2_weight_denom = bitstream_read_ue(reader);

		SYNTAX_IPRINT("luma_log2_weight_denom = %u\n",
			      decoder->sh.luma_log2_weight_denom);

		if (ChromaArrayType() != 0) {
			decoder->sh.chroma_log2_weight_denom = bitstream_read_ue(reader);

			SYNTAX_IPRINT("chroma_log2_weight_denom = %u\n",
				      decoder->sh.chroma_log2_weight_denom);
		}
table_fill:
		pw = l ? &decoder->sh.pred_weight_l1 : &decoder->sh.pred_weight_l0;
		sz = (l ? decoder->sh.num_ref_idx_l1_active_minus1 : decoder->sh.num_ref_idx_l0_active_minus1) + 1;
		*pw = realloc(*pw, sizeof(pred_weight) * sz);

		assert(*pw != NULL);

		for (i = 0; i < sz; i++) {
			(*pw)[i].luma_weight_l_flag = bitstream_read_u(reader, 1);

			SYNTAX_IPRINT("luma_weight_l%d_flag[%d] = %u\n",
				      i, l, (*pw)[i].luma_weight_l_flag);

			if ((*pw)[i].luma_weight_l_flag) {
				(*pw)[i].luma_weight_l = bitstream_read_se(reader);

				SYNTAX_IPRINT("luma_weight_l%d[%d] = %u\n",
					      i, l, (*pw)[i].luma_weight_l);
			}
		}

		if (decoder->sh.slice_type != B || l == 1) {
			break;
		}

		l = 1;
		goto table_fill;
}
	default:
		break;
	}

	if (decoder->nal.ref_idc != 0) {
		if (IdrPicFlag) {
			decoder->sh.no_output_of_prior_pics_flag =
						bitstream_read_u(reader, 1);
			decoder->sh.long_term_reference_flag =
						bitstream_read_u(reader, 1);

			SYNTAX_IPRINT("no_output_of_prior_pics_flag = %u\n",
				      decoder->sh.no_output_of_prior_pics_flag);
			SYNTAX_IPRINT("long_term_reference_flag = %u\n",
				      decoder->sh.long_term_reference_flag);

			if (decoder->sh.long_term_reference_flag) {
				SYNTAX_ERR("long_term_reference unimplemented!\n");
			}
		} else {
			unsigned adaptive_ref_pic_marking_mode_flag;
			uint32_t memory_management_control_operation;
			uint32_t difference_of_pic_nums_minus1;
			uint32_t long_term_pic_num;
			uint32_t long_term_frame_idx;
			uint32_t max_long_term_frame_idx_plus1;
			int marked;
			int picNumX;

			adaptive_ref_pic_marking_mode_flag =
						bitstream_read_u(reader, 1);

			SYNTAX_IPRINT("adaptive_ref_pic_marking_mode_flag = %u\n",
				      adaptive_ref_pic_marking_mode_flag);

			if (adaptive_ref_pic_marking_mode_flag) {
				do {
					memory_management_control_operation =
								bitstream_read_ue(reader);

					SYNTAX_IPRINT("memory_management_control_operation = %u\n",
						      memory_management_control_operation);

					switch (memory_management_control_operation) {
					case 1:
					case 3:
						difference_of_pic_nums_minus1 =
								bitstream_read_ue(reader);

						SYNTAX_IPRINT("difference_of_pic_nums_minus1 = %u\n",
							      difference_of_pic_nums_minus1);

						if (memory_management_control_operation == 3) {
							SYNTAX_ERR("memory_management unimplemented!\n");
							goto long_term_frame_idx__;
						}

						picNumX = decoder->sh.frame_num - (difference_of_pic_nums_minus1 + 1);
						picNumX &= max_frame_num - 1;

						SYNTAX_IPRINT("picNumX = %u\n", picNumX);

						for (i = 1, marked = 0; i <= decoder->DPB_frames_array.size; i++) {
							if (DPB_frames[i]->frame_num != picNumX) {
								continue;
							}

							assert(!DPB_frames[i]->empty);
							DPB_frames[i]->marked_for_removal = 1;
							marked++;

							DECODER_DPRINT("DPB:\tframe[%d]: marked for removal\n", i);
						}

						assert(marked == 1);
						break;
					case 2:
						long_term_pic_num =
								bitstream_read_ue(reader);

						SYNTAX_IPRINT("long_term_pic_num = %u\n",
							      long_term_pic_num);

						SYNTAX_ERR("memory_management unimplemented!\n");
						break;
long_term_frame_idx__:			case 6:
						long_term_frame_idx =
								bitstream_read_ue(reader);

						SYNTAX_IPRINT("long_term_frame_idx = %u\n",
							      long_term_frame_idx);

						SYNTAX_ERR("memory_management unimplemented!\n");
						break;
					case 4:
						max_long_term_frame_idx_plus1 =
								bitstream_read_ue(reader);

						SYNTAX_IPRINT("max_long_term_frame_idx_plus1 = %u\n",
							      max_long_term_frame_idx_plus1);

						SYNTAX_ERR("memory_management unimplemented!\n");
						break;
					case 0:
						break;
					default:
						SYNTAX_ERR("memory_management_control_operation is malformed\n");
						break;
					}
				} while (memory_management_control_operation != 0);
			}
		}
	}

	switch (decoder->sh.slice_type) {
	case I:
	case SI:
		break;
	default:
		if (!CABAC_MODE) {
			break;
		}

		decoder->sh.cabac_init_idc = bitstream_read_ue(reader);

		SYNTAX_IPRINT("cabac_init_idc = %u\n", decoder->sh.cabac_init_idc);
	}

	decoder->sh.slice_qp_delta = bitstream_read_se(reader);

	SYNTAX_IPRINT("slice_qp_delta = %d\n", decoder->sh.slice_qp_delta);

	switch (decoder->sh.slice_type) {
	case SP:
		decoder->sh.sp_for_switch_flag = bitstream_read_u(reader, 1);

		SYNTAX_IPRINT("sp_for_switch_flag = %u\n",
			      decoder->sh.sp_for_switch_flag);
	case SI:
		decoder->sh.slice_qs_delta = bitstream_read_se(reader);

		SYNTAX_IPRINT("slice_qs_delta = %d\n",
			      decoder->sh.slice_qs_delta);
		break;
	default:
		break;
	}

	if (pps->deblocking_filter_control_present_flag) {
		decoder->sh.disable_deblocking_filter_idc = bitstream_read_ue(reader);

		SYNTAX_IPRINT("disable_deblocking_filter_idc = %u\n",
			      decoder->sh.disable_deblocking_filter_idc);

		if (decoder->sh.disable_deblocking_filter_idc != 1) {
			decoder->sh.slice_alpha_c0_offset_div2 =
						bitstream_read_se(reader);
			decoder->sh.slice_beta_offset_div2 =
						bitstream_read_se(reader);

			SYNTAX_IPRINT("slice_alpha_c0_offset_div2 = %d\n",
				      decoder->sh.slice_alpha_c0_offset_div2);
			SYNTAX_IPRINT("slice_beta_offset_div2 = %d\n",
				      decoder->sh.slice_beta_offset_div2);
		}
	}

	if (pps->num_slice_groups_minus1 > 0 &&
		pps->slice_group_map_type >= 3 && pps->slice_group_map_type <= 5)
	{
		int bits_nb = 32 - clz(pps->num_slice_groups_minus1 + 1);

		decoder->sh.slice_group_change_cycle =
						bitstream_read_u(reader, bits_nb);

		SYNTAX_IPRINT("slice_group_change_cycle = %u\n",
			decoder->sh.slice_group_change_cycle);
	}
}
