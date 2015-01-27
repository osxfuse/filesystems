# Minix File System Famiy for Mac OS X
#
# Copyright 2008 Amit Singh (osxbook.com). All Rights Reserved.

TARGETS = minixfs

# Root for OSXFUSE includes and libraries
OSXFUSE_ROOT = /usr/local
#OSXFUSE_ROOT = /opt/local

INCLUDE_DIR = $(OSXFUSE_ROOT)/include/osxfuse
LIBRARY_DIR = $(OSXFUSE_ROOT)/lib

COMMON=../common
UNIXFS=$(COMMON)/unixfs
LINUX=$(COMMON)/linux
LINUX_KERNEL=$(LINUX)/kernel

CC ?= gcc

CFLAGS_OSXFUSE = -I$(INCLUDE_DIR)
CFLAGS_OSXFUSE += -D_FILE_OFFSET_BITS=64
CFLAGS_OSXFUSE += -D_DARWIN_USE_64_BIT_INODE
CFLAGS_OSXFUSE += -DFUSE_USE_VERSION=27
CFLAGS_OSXFUSE += -I$(UNIXFS) -I$(LINUX) -I$(LINUX_KERNEL)/include

CFLAGS_EXTRA = -Wall -Werror -g $(CFLAGS)

LIBS = -losxfuse

all: $(TARGETS)

OBJS = unixfs_minixfs.o minixfs.o minixfs_mainx.o itree_v1.o itree_v2.o
OBJS_COMMON = $(UNIXFS)/unixfs.o $(UNIXFS)/unixfs_internal.o $(LINUX)/linux.o

minixfs: $(OBJS) $(OBJS_COMMON)
	$(CC) $(CFLAGS_OSXFUSE) $(CFLAGS_EXTRA) -o $@ $^ -L$(LIBRARY_DIR) $(LIBS)

-include $(OBJS:.o=.d)

%.o: %.c
	$(CC) $(CFLAGS_OSXFUSE) $(CFLAGS_EXTRA) $*.c -c -o $*.o
	$(CC) $(CFLAGS_OSXFUSE) $(CFLAGS_EXTRA) -MM $*.c > $*.d
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' < $*.d.tmp > $*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

clean:
	rm -f $(TARGETS) *.o *.d $(UNIXFS)/*.o $(UNIXFS)/*.d $(LINUX)/*.o $(LINUX)/*.d
