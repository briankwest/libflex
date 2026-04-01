ACLOCAL_AMFLAGS = -I m4

GIT_HASH := $(shell git -C $(srcdir) rev-parse --short HEAD 2>/dev/null || echo "unknown")
AM_CFLAGS = -DLIBFLEX_GIT_HASH=\"$(GIT_HASH)\"

SUBDIRS = src include tests examples

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libflex.pc

EXTRA_DIST = autogen.sh
