
all: mkfilter mkshape mkaverage gencode mkshapeargs #genplot
	cp mkfilter mkshape mkaverage mkshapeargs gencode ../../bin

mkfilter:	complex.o mkfilter.o
		g++ ${EXTRA_OPS} mkfilter.o complex.o -lm -o mkfilter

mkshape:	complex.o mkshape.o
		g++ ${EXTRA_OPS} mkshape.o complex.o -lm -o mkshape

mkshapeargs:	mkshapeargs.o
		g++ ${EXTRA_OPS} mkshapeargs.o -lm -o mkshapeargs

mkaverage:	complex.o mkaverage.o
		g++ ${EXTRA_OPS} mkaverage.o complex.o -lm -o mkaverage

gencode:	gencode.o complex.o readdata.o
		g++ ${EXTRA_OPS} gencode.o complex.o readdata.o -lm -o gencode

#genplot:	genplot.o complex.o readdata.o
#		g++ ${EXTRA_OPS} genplot.o complex.o readdata.o -lm -o genplot

%.o:	%.C
	g++ ${EXTRA_OPS} -O -c $*.C -o $*.o

clean:
	rm -rf *.o mkfilter mkshape mkshapeargs mkaverage gencode genplot mkfilter.exe mkshape.exe mkshapeargs.exe mkaverage.exe gencode.exe genplot.exe
