SUBDIRS = mkfilter fifi threadutil lib slow test

EXTRA_OPS=-g -O3 -Wall -Wno-write-strings
export EXTRA_OPS

SAMPLERATE = 9600

export SAMPLERATE

all:
	mkdir -p bin
	@for dir in ${SUBDIRS} ; do ( cd $$dir ; ${MAKE} --no-print-directory all ) ; done

clean:
	rm -rf bin
	@for dir in ${SUBDIRS} ; do ( cd $$dir ; ${MAKE} --no-print-directory clean ) ; done
