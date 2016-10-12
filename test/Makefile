BUILTINS =

all: testThreadUtil testBitBuffer

test:	test.o
	g++ -o test test.o

testThreadUtil:	testThreadUtil.o 
		g++ -o testThreadUtil testThreadUtil.o ../threadutil/src/threadutil.a -lpthread

testBitBuffer:	testBitBuffer.o 
		g++ -o testBitBuffer testBitBuffer.o ../slow/src/BitBuffer.o

%.o:		%.C ${hdrs}
		g++ ${EXTRA_OPS} -D_MT -D_REENTRANT -c $*.C

clean:
		rm -f testThreadUtil testThreadUtil.exe testBitBuffer testBitBuffer.exe *.o

