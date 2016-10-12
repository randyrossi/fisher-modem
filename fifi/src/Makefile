all: fifi
	cp fifi ../../bin

fifi:	fifi.h fifi.o
	g++ -g fifi.o -lm -o fifi

fifi.h:
	echo "#define TOOLSDIR \"../../bin\"" > fifi.h

%.o:	%.C
	g++ ${EXTRA_OPS} -c $*.C -o $*.o

clean:
	rm -rf *.o fifi fifi.h fifi.exe
