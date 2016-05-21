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

#ifndef SYNTAX_PARSE_H
#define SYNTAX_PARSE_H

#include <stdint.h>

#include "decoder.h"

#define Intra_4x4	0
#define Intra_8x8	1
#define Intra_16x16	2

#define MB_UNAVAILABLE	-1

void parse_annex_b(decoder_context *decoder);

int parse_mp4(decoder_context *decoder);

#endif // SYNTAX_PARSE_H
