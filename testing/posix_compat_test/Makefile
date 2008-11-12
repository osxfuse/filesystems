CC_COMPILE = g++ -g -O0

OBJECTS = \
	posix_compat_test.o

all: posix_compat_test

posix_compat_test: $(OBJECTS)
	g++ -g -O0 -o $@ $(OBJECTS)

clean:
	rm -f posix_compat_test *.o

%.o :: %.cc
	$(CC_COMPILE) -c -o $@ $<
