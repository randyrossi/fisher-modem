SUBDIRS = src 

all:
	@for dir in ${SUBDIRS} ; do ( cd $$dir ; ${MAKE} --no-print-directory all ) ; done

clean:
	@for dir in ${SUBDIRS} ; do ( cd $$dir ; ${MAKE} --no-print-directory clean ) ; done

