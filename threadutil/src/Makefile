
all: threadutil.a

threadutil.a:	threadutil.o timeout.o
		rm -f threadutil.a
		ar cr threadutil.a threadutil.o timeout.o

%.o:	%.C threadutil.h timeout.h
	g++ ${EXTRA_OPS} -D_MT -D_REENTRANT -c $*.C

clean:
	rm -f *.o threadutil.a

