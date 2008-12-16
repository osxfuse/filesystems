#
# Ancient Unix File Systems for MacFUSE
# Amit Singh
# http://osxbook.com

TARGETS = ancientfs

COMMON=../common
UNIXFS=$(COMMON)/unixfs

CC = gcc
CFLAGS_MACFUSE = -D__FreeBSD__=10 -D__DARWIN_64_BIT_INO_T=1 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=27 -I/usr/local/include/fuse -I$(UNIXFS)
CFLAGS_EXTRA = -Wall -Werror -g
ARCHS = -arch i386 -arch ppc
LIBS = -lfuse_ino64

all: $(TARGETS)

OBJS = ancientfs_tap.o ancientfs_tp.o ancientfs_itp.o ancientfs_dtp.o ancientfs_dump.o ancientfs_dump1024.o ancientfs_dumpvn.o ancientfs_dumpvn1024.o ancientfs_voar.o ancientfs_oar.o ancientfs_ar.o ancientfs_bcpio.o ancientfs_cpio_odc.o ancientfs_cpio_newc.o ancientfs_tar.o ancientfs_v1,2,3.o ancientfs_v4,5,6.o ancientfs_v7.o ancientfs_v10.o ancientfs_32v.o ancientfs_2.9bsd.o ancientfs_2.11bsd.o ancientfs_mainx.o
OBJS_COMMON = $(UNIXFS)/unixfs.o $(UNIXFS)/unixfs_internal.o

ancientfs: $(OBJS) $(OBJS_COMMON)
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
	rm -f $(TARGETS) *.o *.d $(UNIXFS)/*.o $(UNIXFS)/*.d
