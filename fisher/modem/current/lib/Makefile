BUILTINS =

objs=tonedec.o sinegen.o goertzel.o equalize.o filters.o debug.o co_debug.o scramble.o myaudio.o mystdio.o
hdrs=tonedec.h sinegen.h goertzel.h equalize.h filters.h debug.h complex.h scramble.h myaudio.h mystdio.h private.h

library.a:	${objs}
		rm -f library.a
		ar cr library.a ${objs}

%.o:		%.C $hdrs
		gcc -O -c -I$MYLIB $stem.C

clean:
		rm -f $objs

