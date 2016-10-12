/* Modem for MIPS   AJF	  January 1995
   FSK receive routines */

#include <stdio.h>
#include <string.h>
#include <complex.h>
#include <filters.h>
#include <tonedec.h>

#include "FSK.h"
#include "Modem.h"

#define SYNCLOOP_INVOKE 0
#define SYNCLOOP_RESET 1
#define SYNCLOOP_SHUTDOWN 2

static fspec *fefs = mkfilter("-Hp -Bu -o 2 -R %SAMPLERATE% -A 300");       /* 300 Hz hpf */

/* Bandpass filter coeffs constructed by:
   mkfilter -Bu -Bp -o 2 -a (A1) (A2)
   where A1 = (F0 - bps/2) / SAMPLERATE, A2 = (F0 + bps/2) / SAMPLERATE */

static fspec *bpfspecs[] =
  { mkfilter("-Bu -Bp -o 4 -R %SAMPLERATE% -A 345 435"),    /*  345 ..  435 Hz, centre  390 Hz    [0] */
    mkfilter("-Bu -Bp -o 4 -R %SAMPLERATE% -A 405 495"),    /*  405 ..  495 Hz, centre  450 Hz    [1] */
    mkfilter("-Bu -Bp -o 4 -R %SAMPLERATE% -A 830 1130"),    /*  830 .. 1130 Hz, centre  980 Hz    [2] */
    mkfilter("-Bu -Bp -o 4 -R %SAMPLERATE% -A 1030 1330"),    /* 1030 .. 1330 Hz, centre 1180 Hz    [3] */
    mkfilter("-Bu -Bp -o 4 -R %SAMPLERATE% -A 700 1900"),    /*  700 .. 1900 Hz, centre 1300 Hz    [4] */
    mkfilter("-Bu -Bp -o 4 -R %SAMPLERATE% -A 1500 1800"),    /* 1500 .. 1800 Hz, centre 1650 Hz    [5] */
    mkfilter("-Bu -Bp -o 4 -R %SAMPLERATE% -A 1700 2000"),    /* 1700 .. 2000 Hz, centre 1850 Hz    [6] */
    mkfilter("-Bu -Bp -o 4 -R %SAMPLERATE% -A 1500 2700"),    /* 1500 .. 2700 Hz, centre 2100 Hz    [7] */
  };

/* Lpwpass filter coeffs constructed by:
   mkfilter -Bu -Lp -o 2 -a (A1)
   where A1 = (bps/2) / SAMPLERATE */

static fspec *lpfspecs[] =
  { mkfilter("-Bu -Lp -o 4 -R %SAMPLERATE% -A 37.5"),    /*  37.5 Hz   [0] */
    mkfilter("-Bu -Lp -o 4 -R %SAMPLERATE% -A 150"),     /* 150   Hz   [1] */
    mkfilter("-Bu -Lp -o 4 -R %SAMPLERATE% -A 600"),     /* 600   Hz   [2] */
  };

struct info
  { int bitlen;		    /* bit length (num. samples)	    */
    fspec *lpfs;	    /* low-pass filter spec		    */
    fspec *bpfs0, *bpfs1;   /* bandpass filter specs for 0, 1 tones */
  };

static const int infoTabLength = 6;
static info infotab[] =
  {
    /* V21o	  300 bps */
    { SAMPLERATE/300, lpfspecs[1], bpfspecs[6], bpfspecs[5] },
    /* V21a	  300 bps */
    { SAMPLERATE/300, lpfspecs[1], bpfspecs[3], bpfspecs[2] },
    /* V23o	 1200 bps */
    { SAMPLERATE/1200, lpfspecs[2], bpfspecs[7], bpfspecs[4] },
    /* V23a	   75 bps */
    { SAMPLERATE/75, lpfspecs[0], bpfspecs[1], bpfspecs[0] },
    /* E01o	 1200 bps */
    { SAMPLERATE/1200, lpfspecs[2], bpfspecs[7], bpfspecs[4] },
    /* E01a	 1200 bps */
    { SAMPLERATE/1200, lpfspecs[2], bpfspecs[7], bpfspecs[4] },
  };

static void syncprocess(void *data) {
	FSK* fsk = (FSK*) data;
	for (;;) {
		int cntl = fsk->syncprocess2();
		if (cntl == SYNCLOOP_SHUTDOWN) { break; }
	}
}

void FSK::initrx_fsk(ModemOptions::vmode mode) {
	if (inited) { delete td0; delete td1; }
	unless (inited)     /* once-only initialization */
  	{
		rxSyncProcessor = thread_create(syncprocess, this, "syncprocess");
		thread_run(rxSyncProcessor);
		inited = true;
  	}
  	
	if (mode < 0 || mode >= infoTabLength) {
		modem->giveup("Bug! bad mode %d in fsk rx init", mode);
	}

	info *inf = &infotab[mode];
	td0 = new tone_detector(fefs, inf -> bpfs0, inf -> lpfs, false);
	td1 = new tone_detector(fefs, inf -> bpfs1, inf -> lpfs, false);
	rxbitlen = inf -> bitlen; /* num. samples in a bit */
	syncPutBuffer->putBit(SYNCLOOP_RESET);
	syncReturnBuffer->clear();
}

/* asynchronous input */
int FSK::getasync() {
	int i, j;
	uchar n = 0;
	int b = getsample(), nb = 0;
	while (nb < 10* rxbitlen && b) {
		b = getsample();
		nb++;
	}
	if (b)
		return NOCHAR; /* no char yet */
	for (j = 0; j < (3* rxbitlen ) / 2; j++)
		b = getsample(); /* position to centre of first data bit */
	for (i = 0; i < 8; i++) {
		n = (n >> 1) | (b << 7);
		for (j = 0; j < rxbitlen; j++)
			b = getsample();
	}
	return n;
}

/* synchronous input */
int FSK::getsync() {
	syncPutBuffer->putBit(SYNCLOOP_INVOKE); // wake up syncprocess loop
	return syncReturnBuffer->getBit(); // grab returned value from syncprocess loop
}

int FSK::syncprocess2() {
	uchar valid = 0, framing = 0x55, bitcount = 0;
	uchar bits = 0, byte = 0;

	int cntl = syncPutBuffer->getBit(); if (cntl == SYNCLOOP_RESET || cntl == SYNCLOOP_SHUTDOWN) { return cntl; }

	for (;;) {
		int j = 0;
		int bit = 0;
		while (j < rxbitlen) {
			bit = getsample();
			framing = (framing << 1) | bit;
			j = (framing == 0xf0 || framing == 0x0f) ? (rxbitlen / 2) + 4 : j + 1;
		}
		bits = (bits << 1) | bit;
		valid = (valid << 1) | 1;
		switch (bits) {
		case 0x7c:
		case 0x7d:
			valid &= ~2; /* delete bit-stuffing */
			break;
		case 0x7e:
			syncReturnBuffer->putBit(HDLC_FLAG);
			cntl = syncPutBuffer->getBit(); if (cntl == SYNCLOOP_RESET || cntl == SYNCLOOP_SHUTDOWN) { return cntl; }
			valid = bitcount = 0;
			break;
		case 0x7f:
			syncReturnBuffer->putBit(HDLC_ABORT);
			cntl = syncPutBuffer->getBit(); if (cntl == SYNCLOOP_RESET || cntl == SYNCLOOP_SHUTDOWN) { return cntl; }
			valid = bitcount = 0;
			break;
		}
		if (valid & 0x80) {
			byte = (byte << 1) | (bits >> 7);
			if (++bitcount == 8) {
				syncReturnBuffer->putBit(byte);
				cntl = syncPutBuffer->getBit(); if (cntl == SYNCLOOP_RESET || cntl == SYNCLOOP_SHUTDOWN) { return cntl; }
				bitcount = 0;
			}
		}
	}
}

inline int FSK::getsample() {
	float x = samplingDevice->insample();
	td0 -> insert(x);
	td1 -> insert(x);
	return (td1 -> pow) > (td0 -> pow);
}

