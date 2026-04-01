noinst_PROGRAMS = encode_page decode_stream encode_wav decode_wav gen_baseband flex_sdr

encode_page_SOURCES = encode_page.c
encode_page_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS)
encode_page_LDADD = $(top_builddir)/src/libflex.la

decode_stream_SOURCES = decode_stream.c
decode_stream_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS)
decode_stream_LDADD = $(top_builddir)/src/libflex.la

encode_wav_SOURCES = encode_wav.c
encode_wav_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS)
encode_wav_LDADD = $(top_builddir)/src/libflex.la

decode_wav_SOURCES = decode_wav.c
decode_wav_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS)
decode_wav_LDADD = $(top_builddir)/src/libflex.la

gen_baseband_SOURCES = gen_baseband.c
gen_baseband_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS)
gen_baseband_LDADD = $(top_builddir)/src/libflex.la

flex_sdr_SOURCES = flex_sdr.c
flex_sdr_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS)
flex_sdr_LDADD = $(top_builddir)/src/libflex.la -lrtlsdr -lm
