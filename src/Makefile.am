lib_LTLIBRARIES = libflex.la

libflex_la_SOURCES = \
	error.c \
	bch.c \
	codeword.c \
	interleave.c \
	sync.c \
	fiw.c \
	biw.c \
	numeric.c \
	alpha.c \
	binary.c \
	phase.c \
	encoder.c \
	decoder.c \
	modem.c

noinst_HEADERS = flex_internal.h

libflex_la_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include

libflex_la_LDFLAGS = -version-info 0:0:0
libflex_la_LIBADD = -lm
