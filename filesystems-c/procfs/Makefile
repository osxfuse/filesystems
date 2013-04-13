# procfs as a OSXFUSE file system for Mac OS X
#
# Copyright 2007 Amit Singh (osxbook.com). All Rights Reserved.

CC  ?= gcc
CXX ?= g++

CFLAGS_OSXFUSE=-D_FILE_OFFSET_BITS=64 -O -g -I/usr/local/include/osxfuse -arch i386 -isysroot /Developer/SDKs/MacOSX10.6.sdk -Wall
CFLAGS  :=$(CFLAGS_OSXFUSE) $(CFLAGS)
CXXFLAGS:=$(CFLAGS_OSXFUSE) $(CXXFLAGS)
LDFLAGS=-L/usr/local/lib -losxfuse -framework Carbon -framework IOKit -framework ApplicationServices -framework Accelerate -framework OpenGL -weak-lproc
SEQUENCEGRAB_LDFLAGS=-framework AudioUnit -framework Cocoa -framework CoreAudioKit -framework Foundation -framework QuartzCore -framework QuickTime -framework QuartzCore

# Configure this depending on where you installed pcrecpp
# http://www.pcre.org
#
PCRECPP_PREFIX=$(shell pcre-config --prefix)

PCRECPP_CXXFLAGS=-I$(PCRECPP_PREFIX)/include
PCRECPP_LDFLAGS=-arch i386 $(PCRECPP_PREFIX)/lib/libpcrecpp.a $(PCRECPP_PREFIX)/lib/libpcre.a

all: procfs

procfs.o: procfs.cc
	$(CXX) -c $(CXXFLAGS) $(PCRECPP_CXXFLAGS) -o $@ $<

procfs_displays.o: procfs_displays.cc procfs_displays.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

procfs_proc_info.o: procfs_proc_info.cc procfs_proc_info.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

procfs_tpm.o: procfs_tpm.cc procfs_tpm.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

procfs_windows.o: procfs_windows.cc procfs_windows.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

procfs: procfs.o procfs_displays.o procfs_proc_info.o procfs_tpm.o procfs_windows.o sequencegrab/libprocfs_sequencegrab.a
	$(CXX) $(CXXFLAGS) $(PCRECPP_CXXFLAGS) -o $@ $^ $(LDFLAGS) $(PCRECPP_LDFLAGS) $(SEQUENCEGRAB_LDFLAGS)

sequencegrab/libprocfs_sequencegrab.a:
	$(MAKE) -C sequencegrab

install: procfs
	sudo chown root:wheel procfs
	sudo chmod u+s procfs
	sudo mv procfs /usr/local/bin/procfs

clean:
	rm -f procfs procfs.o procfs_displays.o procfs_proc_info.o procfs_tpm.o procfs_windows.o
	$(MAKE) -C sequencegrab clean
