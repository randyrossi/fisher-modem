LIB	 = ../../lib/src

OBJS = main.o ModemOptions.o Modem.o progress.o \
       fsktx.o fskrx.o SlowCoder.o FastCoder.o Hayes.o cancel.o BitBuffer.o \
       v29tx.o v29rx.o v32tx.o v32rx.o v34tx.o v34rx.o fax.o doc.o

LIBS = ${LIB}/library.a ../../threadutil/src/threadutil.a

HDRS = ${LIB}/sinegen.h ${LIB}/equalize.h ${LIB}/filters.h ${LIB}/debug.h \
       ${LIB}/complex.h ${LIB}/scramble.h ${LIB}/SamplingDevice.h \
       ${LIB}/TerminalDevice.h ${LIB}/commonTypes.h \
       ${LIB}/tonedec.h ${LIB}/bitrates.h SlowCoder.h FastCoder.h fcf.h \
       cancel.h BitBuffer.h v29.h v32.h v34.h fax.h

all: modem
	cp modem ../../bin

modem: ${OBJS} ${LIBS}
	g++  ${OBJS} ${LIBS} -lm -lasound -lpthread -lbluetooth -lrt -o modem

%.o: %.C ${HDRS}
	g++ ${EXTRA_OPS} -DSAMPLERATE=${SAMPLERATE} -c -I${LIB} $*.C

progress.T: progress.F
	cat progress.F | sed "s/%SAMPLERATE%/${SAMPLERATE}/g" > progress.T

progress.C: progress.T
	../../bin/fifi $*.T $*.C

fskrx.T: fskrx.F
	cat fskrx.F | sed "s/%SAMPLERATE%/${SAMPLERATE}/g" > fskrx.T

fskrx.C: fskrx.T
	../../bin/fifi $*.T $*.C

v29tx.C: v29tx.h

v29tx.h:
	../../bin/mkshape -r `../../bin/mkshapeargs ${SAMPLERATE} 2400 .5` -x -l -t > v29tx.h

v29rx.T: v29rx.F
	cat v29rx.F | sed "s/%SAMPLERATE%/${SAMPLERATE}/g" > v29rx.T

v29rx.C: v29rx.T
	../../bin/fifi $*.T $*.C

v32tx.C: v32tx.h

v32tx.h:
	../../bin/mkshape -r `../../bin/mkshapeargs ${SAMPLERATE} 2400 .5` -x -l -t > v32tx.h

v34tx.C: v34tx.h

v34tx.h:
	../../bin/mkshape -c `../../bin/mkshapeargs ${SAMPLERATE} 600 .5`  -l -t | sed 's/SYMBLEN/ZSYMBLEN/g' > v34tx.h

v32rx.T: v32rx.F
	cat v32rx.F | sed "s/%SAMPLERATE%/${SAMPLERATE}/g" > v32rx.T

v32rx.C: v32rx.T
	../../bin/fifi $*.T $*.C

v34rx.T: v34rx.F
	cat v34rx.F | sed "s/%SAMPLERATE%/${SAMPLERATE}/g" > v34rx.T

v34rx.C: v34rx.T
	../../bin/fifi $*.T $*.C

clean:
	rm -f *.o progress.C fskrx.C v29tx.h v32tx.h v34tx.h v32rx.C v34rx.C v29rx.C modem modem.exe *.T *.stackdump
