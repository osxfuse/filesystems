#
# UFS for MacFUSE
# Amit Singh
# http://osxbook.com

TARGETS = ufs

COMMON=../common
UNIXFS=$(COMMON)/unixfs
LINUX=$(COMMON)/linux
LINUX_KERNEL=$(LINUX)/kernel

CC = gcc
CFLAGS_MACFUSE = -D__FreeBSD__=10 -D__DARWIN_64_BIT_INO_T=1 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=27 -I/usr/local/include/fuse -I. -I$(LINUX) -I$(LINUX_KERNEL)/include -I$(LINUX_KERNEL)/fs -I$(UNIXFS)
CFLAGS_EXTRA = -Wall -Werror -g
ARCHS = -arch i386 -arch ppc
LIBS = -lfuse_ino64

all: $(TARGETS)

OBJS = unixfs_ufs.o ufs_mainx.o ufs.o
OBJS_COMMON = $(UNIXFS)/unixfs.o $(UNIXFS)/unixfs_internal.o $(LINUX)/linux.o $(LINUX_KERNEL)/lib/parser.o

ufs: $(OBJS) $(OBJS_COMMON)
	$(CC) $(CFLAGS_MACFUSE) $(CFLAGS_EXTRA) $(ARCHS) -o $@ $^ $(LIBS)

-include $(OBJS:.o=.d)

%.o: %.c
	$(CC) $(CFLAGS_MACFUSE) $(CFLAGS_EXTRA) $(ARCHS) $*.c -c -o $*.o
	$(CC) $(CFLAGS_MACFUSE) $(CFLAGS_EXTRA) -MM $*.c > $*.d
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' < $*.d.tmp > $*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

clean:
	rm -f $(TARGETS) *.o *.d $(UNIXFS)/*.o $(UNIXFS)/*.d $(LINUX)/*.o $(LINUX)/*.d $(LINUX_KERNEL)/lib/*.o $(LINUX_KERNEL)/lib/*.d
