# windowfs file system for Mac OS X
#
# Copyright 2012 Benjamin Fleischer. All Rights Reserved.
# Copyright 2007-2008 Amit Singh (osxbook.com). All Rights Reserved.

CXXFLAGS=-D_FILE_OFFSET_BITS=64 -D__FreeBSD__=10 -I/usr/local/include/osxfuse -O -g
LDFLAGS=-L/usr/local/lib -losxfuse -framework Carbon

# Configure this depending on where you installed pcrecpp
# http://www.pcre.org

PCRECPP_PREFIX=$(shell pcre-config --prefix)

PCRECPP_CXXFLAGS=-I$(PCRECPP_PREFIX)/include
PCRECPP_LDFLAGS=$(PCRECPP_PREFIX)/lib/libpcrecpp.a $(PCRECPP_PREFIX)/lib/libpcre.a

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
