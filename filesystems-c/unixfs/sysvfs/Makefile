#
# SYSV File System Famiy for OSXFUSE
# Amit Singh
# http://osxbook.com
#

TARGETS = sysvfs

COMMON=../common
UNIXFS=$(COMMON)/unixfs
LINUX=$(COMMON)/linux
LINUX_KERNEL=$(LINUX)/kernel

CC = gcc
CFLAGS_OSXFUSE = -D__FreeBSD__=10 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=27 -I/usr/local/include/osxfuse/fuse -I$(UNIXFS) -I$(LINUX)
CFLAGS_EXTRA = -Wall -Werror -g
ARCHS = -arch i386 -arch x86_64
LIBS = -losxfuse

all: $(TARGETS)

OBJS = unixfs_sysvfs.o sysvfs.o sysvfs_mainx.o
OBJS_COMMON = $(UNIXFS)/unixfs.o $(UNIXFS)/unixfs_internal.o $(LINUX)/linux.o

sysvfs: $(OBJS) $(OBJS_COMMON)
	$(CC) $(CFLAGS_OSXFUSE) $(CFLAGS_EXTRA) $(ARCHS) -o $@ $^ $(LIBS)

-include $(OBJS:.o=.d)

%.o: %.c
	$(CC) $(CFLAGS_OSXFUSE) $(CFLAGS_EXTRA) $(ARCHS) $*.c -c -o $*.o
	$(CC) $(CFLAGS_OSXFUSE) $(CFLAGS_EXTRA) -MM $*.c > $*.d
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' < $*.d.tmp > $*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

clean:
	rm -f $(TARGETS) *.o *.d $(UNIXFS)/*.o $(UNIXFS)/*.d $(LINUX)/*.o $(LINUX)/*.d
