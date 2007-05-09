# procfs as a MacFUSE file system for Mac OS X
#
# Copyright Amit Singh. All Rights Reserved.
# http://osxbook.com
#
# http://code.google.com/p/macfuse/

CPPFLAGS=-D_FILE_OFFSET_BITS=64 -D__FreeBSD__=10 -O -g -arch i386 -arch ppc -isysroot /Developer/SDKs/MacOSX10.4u.sdk
LDFLAGS=-L/usr/local/lib -lfuse -framework Carbon -framework IOKit

# Configure this depending on where you installed pcrecpp
# http://www.pcre.org
#
PCRECPP_PREFIX=/usr/local

PCRECPP_CPPFLAGS=-I$(PCRECPP_PREFIX)/include
PCRECPP_LDFLAGS=-arch i386 -arch ppc $(PCRECPP_PREFIX)/lib/libpcrecpp.a $(PCRECPP_PREFIX)/lib/libpcre.a

procfs: procfs.cc
	g++ -Wall $(CPPFLAGS) $(PCRECPP_CPPFLAGS) -o $@ $< $(LDFLAGS) $(PCRECPP_LDFLAGS)

all: procfs

clean:
	rm -f procfs procfs.o procfs_tpm.o
