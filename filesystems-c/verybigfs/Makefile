# Makefile
# A "very big" file system with a "very big" file. All that you can read.
#
# Copyright Amit Singh. All Rights Reserved.
# http://osxbook.com
#
# http://code.google.com/p/macfuse/
#
# Source License: GNU GENERAL PUBLIC LICENSE (GPL)
#

TARGETS = verybigfs

CC = gcc
CFLAGS_MACFUSE = -D__FreeBSD__=10 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -I/usr/local/include/fuse
CFLAGS_EXTRA = -Wall -g
ARCHS = -arch i386 -arch ppc
LIBS = -lfuse

.c:
	$(CC) $(CFLAGS_MACFUSE) $(CFLAGS_EXTRA) $(ARCHS) -o $@ $< $(LIBS)

all: $(TARGETS)

verybigfs: verybigfs.c

clean:
	rm -f $(TARGETS) *.o
