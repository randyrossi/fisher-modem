/* Modem for MIPS   AJF	  March 1998
   V.29 receive routines */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../threadutil/src/threadutil.h"
#include <complex.h>
#include <filters.h>
#include <scramble.h>
#include <equalize.h>
#include <debug.h>
#include <sinegen.h>

#include "Modem.h"
#include "v29.h"
#include "SlowCoder.h"

//#define THRESHOLD 1.0f	/* sqr of radius of error circle */
//#define STABLE_AB_LEN 64	/* how many AB's to look for for wt_tone */

#define STABLE_AB_LEN 32	// was 64 but was too short to talk to itself...?
#define THRESHOLD 2.5f		// was 1.0 but never detected AB from itself...why?

#define SYNCLOOP_INVOKE 0
#define SYNCLOOP_RESET 1
#define SYNCLOOP_SHUTDOWN 2

static fspec *lpf_fs = mkfilter("-Bu -Lp -o 4 -R %SAMPLERATE% -A 1200");   /* low-pass at 1200 Hz */

static void v29rx_bitloop(void *data) {
	Modem* modem = (Modem*) data;
	v29G* v29 = (v29G*) modem->v29;

	for (;;) {
		int ctl = v29->rxCntlBuffer->getBit(); 
		if (ctl == SYNCLOOP_RESET) { continue; }
		if (ctl == SYNCLOOP_SHUTDOWN) { break; }
	
		printf("wt_tone\n");
		v29->wt_tone(); /* look for stable ABAB... */
		printf("wt_reversal\n");
		v29->wt_reversal(); /* look for CDCD... */
		printf("wt_eqz\n");
		v29->train_eqz();
		printf("dataloop\n");
		v29->dataloop();
	}
}

void v29G::initrx_v29() {
	if (rxInited == false) { /* perform once-only initialization */
		rxCarrier = new SineGen(1700.0);
		fe_lpf = new cfilter(lpf_fs);
		rxScrambler = new scrambler(GPC);
		rxEqualizer = new equalizer(0.5); /* was 0.25; V.29 has short training sequence so needs fast equalization */
		rxDecoder = new SlowDecoder();
		rxTrainer = new SlowTrainingGen;
		co_debug = new co_debugger(24000);
		acq_debug = new debugger(2, 4000);
		//atexit(tidydebug);
		rxInited = true;
		rxBitLoop = thread_create(v29rx_bitloop, modem, "bitloop");
		thread_run(rxBitLoop);
	}

	rxCntlBuffer->putBit(SYNCLOOP_RESET);
	rxBitBuffer->clear();
	
	for (int i = 0; i < 144; i++)
		getbit(); /* scrambled 1s */
}

void v29G::tidydebug() {
	co_debug -> print("debug_co.grap");
	acq_debug -> print("debug_acq.grap");
	rxEqualizer -> print("debug_eqz.grap");
}

int v29G::getasync()
{
	int b = getbit(), nb = 0;
	while (nb < 10 && b) {
		b = getbit();
		nb++;
	}
	if (b) {
		return NOCHAR; /* no char yet */
	}
	int ch = 0;
	for (int i = 0; i < 8; i++) {
		int b = getbit();
		ch = (ch >> 1) | (b << 7);
	}
	return ch;
}

int v29G::getbit() /* bit input */
{
	rxCntlBuffer->putBit(SYNCLOOP_INVOKE);
	return rxBitBuffer->getBit();
}

void v29G::wt_tone() { /* wait for a stable ABAB... */
	timing = 0;
	rxEqualizer -> reset();
	complex z0 = complex(-3.0f, 0.0f), /* A */
	z1 = complex(1.0f, -1.0f); /* B */
	int bc = 0, cnt = 0;
	until (cnt >= STABLE_AB_LEN && !(bc & 1)) {
	
		complex z = getsymbol(); /* get equalized symbol */
		complex ez = (bc++ & 1) ? z1 : z0; /* expected z */
		float p = power(z - ez);
		acq_debug -> insert(z.re, p);
		if (p < THRESHOLD)
			cnt++;
		else
			cnt = 0;
		rxEqualizer -> short_update(ez - z); /* short update here */
	}
	acq_debug -> tick('A');
}

void v29G::wt_reversal() { /* wait for a phase reversal */
	complex z0 = complex(-3.0f, 0.0f), /* A */
	z1 = complex(1.0f, -1.0f); /* B */
	int bc = 0;
	bool rev = false;
	until (rev & !(bc & 1)) {
		complex z = getsymbol(); /* get equalized symbol */
		complex ez = (bc++ & 1) ? z1 : z0; /* expected z */
		float p = power(z - ez);
		acq_debug -> insert(z.re, p);
		if (p >= THRESHOLD)
			rev = true;
		rxEqualizer -> short_update(ez - z); /* short update here */
	}
	acq_debug -> tick('B');
}

void v29G::train_eqz() { /* adj equalizer coeffs and symbol timing; use training sequence */
	nextadj = samplingDevice->samplecount + 2* SAMPLERATE ;
	int bc = SLOW_SEG_3; /* need to start here because of scrambler in training gen! */
	rxTrainer -> reset();
	rxTrainer -> get(bc++);
	rxTrainer -> get(bc++); /* but we've already read the first 2 symbols */
	while (bc < SLOW_SEG_4) {
		complex z = getsymbol(); /* get equalized symbol */
		complex ez = rxTrainer -> get(bc++); /* update equalizer using training sequence */
		float p = power(z - ez);
		acq_debug -> insert(z.re, p);
		rxEqualizer -> update(ez - z);
		adjtiming(); /* adjust symbol timing */
	}
	acq_debug -> tick('C');
}

void v29G::dataloop() { /* adj equalizer coeffs and symbol timing; use decoded data */
    int ctl = 0;
	rxScrambler -> reset();
	rxDecoder -> reset();
	for (;;) {
		complex z = getsymbol(); /* get equalized symbol */
		int bits = rxDecoder -> decode(z); /* get 3 bits */
		for (int i = 0; i < 3; i++) {
			int b = rxScrambler -> rev((bits >> 2) & 1); /* unscramble */
			rxBitBuffer->putBit(b); /* return the bit */
			ctl = rxCntlBuffer->getBit(); if (ctl == SYNCLOOP_RESET || ctl == SYNCLOOP_SHUTDOWN) { break; }
			bits <<= 1;
		}
		if (ctl == SYNCLOOP_RESET || ctl == SYNCLOOP_SHUTDOWN) { break; }
		complex ez = rxDecoder -> getez(); /* get exact (quantized) z */
		rxEqualizer -> update(ez - z); /* update equalizer from data sequence */
		adjtiming(); /* adjust symbol timing */
	}
}

complex v29G::getsymbol() {
	for (int j = timing; j < 2; j++) /* timing is -1, 0 or +1 */
	{
		complex yz = gethalfsymb();
		rxEqualizer -> insert(yz); /* half-point equalization */
	}
	timing = 0;
	complex z = rxEqualizer -> get();
	co_debug -> insert(z);
	return z;
}

complex v29G::gethalfsymb() { /* sample at half-symbol intervals */
	complex yz;
	for (int k = 0; k < SYMBLEN / 2; k++) {
		float x = samplingDevice->insample();
		complex cz = rxCarrier -> cnext();
		yz = fe_lpf -> fstep(x * cz); /* translate to baseband */
	}
	return yz;
}

void v29G::adjtiming() {
	if (after(samplingDevice->samplecount, nextadj)) {
		int dt = rxEqualizer -> getdt();
		if (dt > 0) {
			timing--;
			rxEqualizer -> shift(-1);
		}
		if (dt < 0) {
			timing++;
			rxEqualizer -> shift(+1);
		}
		nextadj = samplingDevice->samplecount + 2* SAMPLERATE ; /* adjust every 2 secs */
	}
}
