
OBJS=tonedec.o sinegen.o goertzel.o equalize.o filters.o debug.o co_debug.o scramble.o dsp.o memPipe.o bt.o SamplingDevice.o TerminalDevice.o exceptions.o TxRxHelper.o

HDRS=tonedec.h sinegen.h goertzel.h equalize.h filters.h debug.h complex.h scramble.h dsp.h memPipe.h bt.h SamplingDevice.h TerminalDevice.h private.h exceptions.h commonTypes.h TxRxHelper.h

all: library.a

library.a:	${OBJS}
		rm -f library.a
		ar cr library.a ${OBJS}

%.o:		%.C ${HDRS}
		g++ ${EXTRA_OPS} -DSAMPLERATE=${SAMPLERATE} -c $*.C

clean:
		rm -f *.o library.a

