noinst_PROGRAMS = encode_page decode_stream encode_wav decode_wav gen_baseband

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

if HAVE_PORTAUDIO
noinst_PROGRAMS += flex_tx
flex_tx_SOURCES = flex_tx.c
flex_tx_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS)
flex_tx_LDADD = $(top_builddir)/src/libflex.la -lportaudio -lm
endif

if HAVE_RTLSDR
noinst_PROGRAMS += flex_sdr
flex_sdr_SOURCES = flex_sdr.c
flex_sdr_CFLAGS = \
	-std=c99 \
	-Wall -Wextra -pedantic \
	-I$(top_srcdir)/include \
	$(AM_CFLAGS) \
	$(RTLSDR_CFLAGS)
flex_sdr_LDADD = $(top_builddir)/src/libflex.la $(RTLSDR_LIBS) -lm -lpthread
endif
