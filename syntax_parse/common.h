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

#ifndef SYNTAX_COMMON_H
#define SYNTAX_COMMON_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitstream.h"
#include "decoder.h"
#include "syntax_parse.h"

#define SYNTAX_WARN(f, ...)				\
{							\
	fprintf(stderr, "%s:%d:\n", __FILE__, __LINE__);\
	fprintf(stderr, "error! syntax_parse: %s: "	\
		f, __func__, ## __VA_ARGS__);		\
}

#define SYNTAX_ERR(f, ...)				\
{							\
	SYNTAX_WARN(f, ## __VA_ARGS__)			\
	exit(EXIT_FAILURE);				\
}

// #define SYNTAX_IPRINT(f, ...)	printf("@%d " f, reader->data_offset*8 + reader->bit_shift, ## __VA_ARGS__)
// #define SYNTAX_DPRINT(f, ...)	printf("@%d " f, reader->data_offset*8 + reader->bit_shift, ## __VA_ARGS__)

#define SYNTAX_IPRINT(f, ...)	printf(f, ## __VA_ARGS__)
#define SYNTAX_DPRINT(f, ...)	printf(f, ## __VA_ARGS__)

// #define SYNTAX_IPRINT(f, ...) {}
// #define SYNTAX_DPRINT(f, ...) {}

#define ChromaArrayType()	\
	(sps->separate_colour_plane_flag ? 0 : sps->chroma_format_idc)

#define CABAC_MODE	pps->entropy_coding_mode_flag

#ifndef clz
#define clz	__builtin_clz
#endif

#define I_NxN	0
#define I_PCM	25

int seek_to_NAL_start(bitstream_reader *reader);

int is_NAL_start_code(bitstream_reader *reader);

void parse_NAL(decoder_context *decoder);

void scaling_list(bitstream_reader *reader, int8_t *scalingList,
		  unsigned sizeOfScalingList,
		  unsigned *useDefaultScalingMatrixFlag);

void parse_SPS(decoder_context *decoder);

void parse_PPS(decoder_context *decoder);

void parse_slice_header(decoder_context *decoder);

int more_rbsp_data(decoder_context *decoder);

void SPS_vui_parameters(decoder_context *decoder);

void decoder_reset_SPS(decoder_context_sps *sps);

void decoder_reset_PPS(decoder_context_pps *pps);

void decoder_reset_SH(decoder_context *decoder);

#endif // SYNTAX_COMMON_H
