TARGETS = hello hello_ll

CC = gcc
CFLAGS_MACFUSE = -D__FreeBSD__=10 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -I/usr/local/include/fuse
CFLAGS_EXTRA = -Wall -g
ARCHS = -arch i386 -arch ppc
LIBS = -lfuse

.c:
	$(CC) $(CFLAGS_MACFUSE) $(CFLAGS_EXTRA) $(ARCHS) -o $@ $< $(LIBS)

all: $(TARGETS)

hello: hello.c

hello_ll: hello_ll.c

clean:
	rm -f $(TARGETS) *.o
