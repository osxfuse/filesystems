# A "very big" file system with a "very big" file. All that you can read.
#
# Copyright 2008 Amit Singh (osxbook.com). All Rights Reserved.
#
# Source License: GNU GENERAL PUBLIC LICENSE (GPL)

TARGETS = verybigfs

CC ?= gcc
CFLAGS_OSXFUSE = -D__FreeBSD__=10 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -I/usr/local/include/osxfuse/fuse
CFLAGS_EXTRA = -Wall -g $(CFLAGS)
LIBS = -losxfuse

.c:
	$(CC) $(CFLAGS_OSXFUSE) $(CFLAGS_EXTRA) -o $@ $< $(LIBS)

all: $(TARGETS)

verybigfs: verybigfs.c

clean:
	rm -f $(TARGETS) *.o
