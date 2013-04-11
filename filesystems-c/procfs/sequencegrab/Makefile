# Sequence Grabbing Library
#
# Dave MacLachlan

CC  ?= gcc
CXX ?= g++
CFLAGS   := -arch i386 -isysroot /Developer/SDKs/MacOSX10.6.sdk $(CFLAGS)
CXXFLAGS := -arch i386 -isysroot /Developer/SDKs/MacOSX10.6.sdk $(CXXFLAGS)
LDFLAGS = -framework Cocoa -framework CoreAudioKit -framework Foundation -framework QuartzCore -framework QuickTime -framework QuartzCore

OBJECTS = CSGCamera.o CSGImage.o procfs_sequencegrab.o

all: libprocfs_sequencegrab.dylib libprocfs_sequencegrab.a

%o.%m:
	$(CC) -c $(CFLAGS) -o $@ $<

CSGCamera.o: CSGCamera.m
CSGImage.o: CSGImage.m

procfs_sequencegrab.o: procfs_sequencegrab.mm
	$(CXX) -c $(CXXFLAGS) -o $@ $<

libprocfs_sequencegrab.dylib: $(OBJECTS)
	$(CXX) -dynamiclib -install_name /tmp/libprocfs_sequencegrab.dylib $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)

libprocfs_sequencegrab.a: $(OBJECTS)
	libtool -static -o libprocfs_sequencegrab.a $(OBJECTS)
	ranlib libprocfs_sequencegrab.a

clean:
	rm -f *.o libprocfs_sequencegrab.dylib libprocfs_sequencegrab.a
