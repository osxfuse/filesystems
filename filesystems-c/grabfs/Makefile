# windowfs file system for Mac OS X
#
# Copyright 2012 Benjamin Fleischer. All Rights Reserved.
# Copyright 2007-2008 Amit Singh (osxbook.com). All Rights Reserved.

# Root for OSXFUSE includes and libraries
OSXFUSE_ROOT = /usr/local
#OSXFUSE_ROOT = /opt/local

INCLUDE_DIR = $(OSXFUSE_ROOT)/include/osxfuse/fuse
LIBRARY_DIR = $(OSXFUSE_ROOT)/lib

CC  ?= gcc
CXX ?= g++

OSXFUSE_CFLAGS = -I$(INCLUDE_DIR)
OSXFUSE_CFLAGS += -D_FILE_OFFSET_BITS=64
OSXFUSE_CFLAGS += -D_DARWIN_USE_64_BIT_INODE
OSXFUSE_CFLAGS += -O -g -Wall

CFLAGS  := $(OSXFUSE_CFLAGS) $(CFLAGS)
CXXFLAGS:= $(OSXFUSE_CFLAGS) $(CXXFLAGS)
LDFLAGS=-L$(LIBRARY_DIR) -losxfuse -framework Carbon

# Configure this depending on where you installed pcrecpp
# http://www.pcre.org

PCRECPP_PREFIX=$(shell pcre-config --prefix)

PCRECPP_CXXFLAGS=-I$(PCRECPP_PREFIX)/include
PCRECPP_LDFLAGS=$(PCRECPP_PREFIX)/lib/libpcrecpp.a $(PCRECPP_PREFIX)/lib/libpcre.a

all: windowfs

GetPID.o: GetPID.c
	$(CC) -c $(CFLAGS) -o $@ $<

windowfs.o: windowfs.cc
	$(CXX) -c $(CXXFLAGS) $(PCRECPP_CXXFLAGS) -fwritable-strings -o $@ $<

windowfs_windows.o: windowfs_windows.cc windowfs_windows.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

windowfs: windowfs.o windowfs_windows.o GetPID.o
	$(CXX) $(CXXFLAGS) $(PCRECPP_CXXFLAGS) -o $@ $^ $(LDFLAGS) $(PCRECPP_LDFLAGS)

install: windowfs
	sudo chown root:wheel windowfs
	sudo chmod u+s windowfs
	sudo mv windowfs /usr/local/bin/windowfs

clean:
	rm -f windowfs GetPID.o windowfs.o windowfs_windows.o
