# windowfs as a MacFUSE file system for Mac OS X
#
# Copyright Amit Singh. All Rights Reserved.
# http://osxbook.com
#
# http://code.google.com/p/macfuse/

CXXFLAGS=-D_FILE_OFFSET_BITS=64 -D__FreeBSD__=10 -O -g -arch i386 -arch ppc -isysroot /Developer/SDKs/MacOSX10.5.sdk
LDFLAGS=-L/usr/local/lib -lfuse -framework Carbon

# Configure this depending on where you installed pcrecpp
# http://www.pcre.org
#
PCRECPP_PREFIX=/usr/local

PCRECPP_CXXFLAGS=-I$(PCRECPP_PREFIX)/include
PCRECPP_LDFLAGS=-arch i386 $(PCRECPP_PREFIX)/lib/libpcrecpp.a $(PCRECPP_PREFIX)/lib/libpcre.a

all: windowfs

GetPID.o: GetPID.c
	g++ -c -Wall $(CXXFLAGS) -o $@ $<

windowfs.o: windowfs.cc
	g++ -c -Wall $(CXXFLAGS) $(PCRECPP_CXXFLAGS) -o $@ $<

windowfs_windows.o: windowfs_windows.cc windowfs_windows.h
	g++ -c -Wall $(CXXFLAGS) -o $@ $<

windowfs: windowfs.o windowfs_windows.o GetPID.o
	g++ -Wall $(CXXFLAGS) $(PCRECPP_CXXFLAGS) -o $@ $^ $(LDFLAGS) $(PCRECPP_LDFLAGS)

install: windowfs
	sudo chown root:wheel windowfs
	sudo chmod u+s windowfs
	sudo mv windowfs /usr/local/bin/windowfs

clean:
	rm -f windowfs GetPID.o windowfs.o windowfs_windows.o
