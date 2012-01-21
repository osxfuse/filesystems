# windowfs as a OSXFUSE file system for Mac OS X
#
# Copyright Amit Singh. All Rights Reserved.
# http://osxbook.com
#
# http://code.google.com/p/macfuse/

# Build using the architecture of the build machine. If you want to build a universal binary, change ARCH to:
# ARCH=-arch x86_64 -arch i386
# If you do, make sure you also have a universal build of PCRE installed.
ARCH=-arch $(shell uname -m)

CXXFLAGS=-D_FILE_OFFSET_BITS=64 -D__FreeBSD__=10 -O -g $(ARCH) -isysroot /Developer/SDKs/MacOSX10.6.sdk
LDFLAGS=-L/usr/local/lib -losxfuse -framework Carbon

# If you have PCRE installed in a location other than /usr/local or /opt/local, set CUSTOM_PCRE_PREFIX.
# CUSTOM_PCRE_PREFIX=/your/custom/pcre/location
ifdef CUSTOM_PCRE_PREFIX
PCRECPP_PREFIX=$(CUSTOM_PCRE_PREFIX)
else
PCRECPP_PREFIX=$(shell cat pcrelocation)
endif

PCRECPP_CXXFLAGS=-I$(PCRECPP_PREFIX)/include
PCRECPP_LDFLAGS=$(ARCH) $(PCRECPP_PREFIX)/lib/libpcrecpp.a $(PCRECPP_PREFIX)/lib/libpcre.a

all: pcrelocation windowfs

# pcrelocation looks for PCRE in common locations and prints an error message if none is found.
# If one is found, it writes the location a file named pcrelocation, which is used by the PCRECPP_PREFIX
# makefile variable.
pcrelocation:
	( [ ! -z "$(CUSTOM_PCRE_PREFIX)" ] ) \
	|| ( [ -f "/opt/local/include/pcrecpp.h" ] && echo "/opt/local" > pcrelocation ) \
	|| ( [ -f "/usr/local/include/pcrecpp.h" ] && echo "/usr/local" > pcrelocation ) \
	|| ( echo "ERROR: PCRE not found. Install with: sudo port install pcre" && exit 1 )

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
	rm -f windowfs GetPID.o windowfs.o windowfs_windows.o pcrelocation
