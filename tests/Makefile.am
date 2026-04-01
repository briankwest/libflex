check_PROGRAMS = test_libflex

test_libflex_SOURCES = \
	test_main.c \
	test_bch.c \
	test_interleave.c \
	test_sync.c \
	test_fiw.c \
	test_biw.c \
	test_numeric.c \
	test_alpha.c \
	test_binary.c \
	test_codeword.c \
	test_phase.c \
	test_encoder.c \
	test_decoder.c \
	test_roundtrip.c \
	test_modem.c

noinst_HEADERS = test.h

test_libflex_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS)

test_libflex_LDADD = $(top_builddir)/src/libflex.la -lm

TESTS = test_libflex
