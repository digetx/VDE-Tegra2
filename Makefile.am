AUTOMAKE_OPTIONS = subdir-objects

AM_CFLAGS  = -I$(top_srcdir)/include $(PTHREAD_CFLAGS) -static -Wall
AM_LDFLAGS = $(PTHREAD_LIBS)
AM_CC      = $(PTHREAD_CC)

noinst_PROGRAMS = h264_tegra_decode

h264_tegra_decode_SOURCES =				\
	syntax_parse/ANNEX_B.c				\
	syntax_parse/NAL.c				\
	syntax_parse/SPS.c				\
	syntax_parse/PPS.c				\
	syntax_parse/MP4.c				\
	syntax_parse/VUI.c				\
	syntax_parse/slice_header.c			\
	bitstream/bitstream.c				\
	decoder.c					\
	DPB_routines.c					\
	main.c
