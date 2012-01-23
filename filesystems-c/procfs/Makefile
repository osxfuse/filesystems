# procfs as a OSXFUSE file system for Mac OS X
#
# Copyright Amit Singh. All Rights Reserved.
# http://osxbook.com
#
# http://code.google.com/p/macfuse/

# Build using the architecture of the build machine. If you want to build a universal binary, change ARCH to:
# ARCH=-arch x86_64 -arch i386
# If you do, make sure you also have a universal build of PCRE installed.
ARCHITECTURE=$(shell uname -m)
ARCH=-arch $(ARCHITECTURE)

ifneq ($(ARCHITECTURE), x86_64)
# The sequence grabber uses the QuickTime framework, which is not available under 64-bit. The sequence grabber
# is used to implement a filesystem interface to the camera (built-in iSight).
SEQUENCEGRABDEF=-DUSE_CAMERA=1
SEQUENCEGRABLIB=sequencegrab/libprocfs_sequencegrab.a
SEQUENCEGRAB_LDFLAGS=-framework AudioUnit -framework Cocoa -framework CoreAudioKit -framework Foundation -framework QuartzCore -framework QuickTime -framework QuartzCore
endif

CXXFLAGS=-D_FILE_OFFSET_BITS=64 -D__FreeBSD__=10 $(SEQUENCEGRABDEF) -O -g -I/usr/local/include/osxfuse $(ARCH) -isysroot /Developer/SDKs/MacOSX10.6.sdk
LDFLAGS=-L/usr/local/lib -losxfuse -framework Carbon -framework IOKit -framework ApplicationServices -framework Accelerate -framework OpenGL -weak-lproc

# If you have PCRE installed in a location other than /usr/local or /opt/local, set CUSTOM_PCRE_PREFIX.
# CUSTOM_PCRE_PREFIX=/your/custom/pcre/location
ifdef CUSTOM_PCRE_PREFIX
PCRECPP_PREFIX=$(CUSTOM_PCRE_PREFIX)
else
PCRECPP_PREFIX=$(shell cat pcrelocation)
endif

PCRECPP_CXXFLAGS=-I$(PCRECPP_PREFIX)/include
PCRECPP_LDFLAGS=$(ARCH) $(PCRECPP_PREFIX)/lib/libpcrecpp.a $(PCRECPP_PREFIX)/lib/libpcre.a

all: pcrelocation procfs

# pcrelocation tries to find PCRE using, in the following order:
# 1. The CUSTOM_PCRE_PREFIX specified in this makefile, if one is given
# 2. pcre-config --prefix, if pcre-config is in the PATH
# 3. /opt/local
# 4. /usr/local
# Otherwise, an error message is printed and the build fails.
pcrelocation:
	( [ ! -z "$(CUSTOM_PCRE_PREFIX)" ] ) \
	|| ( which pcre-config && pcre-config --prefix > pcrelocation ) \
	|| ( [ -f "/opt/local/include/pcrecpp.h" ] && echo "/opt/local" > pcrelocation ) \
	|| ( [ -f "/usr/local/include/pcrecpp.h" ] && echo "/usr/local" > pcrelocation ) \
	|| ( echo "ERROR: PCRE not found. Get it from http://www.pcre.org." && exit 1 )

procfs.o: procfs.cc
	g++ -c -Wall $(CXXFLAGS) $(PCRECPP_CXXFLAGS) -o $@ $<

procfs_displays.o: procfs_displays.cc procfs_displays.h
	g++ -c -Wall $(CXXFLAGS) -o $@ $<

procfs_proc_info.o: procfs_proc_info.cc procfs_proc_info.h
	g++ -c -Wall $(CXXFLAGS) -o $@ $<

procfs_tpm.o: procfs_tpm.cc procfs_tpm.h
	g++ -c -Wall $(CXXFLAGS) -o $@ $<

procfs_windows.o: procfs_windows.cc procfs_windows.h
	g++ -c -Wall $(CXXFLAGS) -o $@ $<

procfs: procfs.o procfs_displays.o procfs_proc_info.o procfs_tpm.o procfs_windows.o $(SEQUENCEGRABLIB)
	g++ -Wall $(CXXFLAGS) $(PCRECPP_CXXFLAGS) -o $@ $^ $(LDFLAGS) $(PCRECPP_LDFLAGS) $(SEQUENCEGRABLIB) $(SEQUENCEGRAB_LDFLAGS)

sequencegrab/libprocfs_sequencegrab.a:
	(cd sequencegrab && make)

install: procfs
	sudo chown root:wheel procfs
	sudo chmod u+s procfs
	sudo mv procfs /usr/local/bin/procfs

clean:
	rm -f procfs procfs.o procfs_displays.o procfs_proc_info.o procfs_tpm.o procfs_windows.o
	(cd sequencegrab && make clean)
