#include <stdio.h>	/* sprintf */
#include <string.h>	/* sprintf */

#include <complex.h>
#include <filters.h>
#include <scramble.h>
#include <equalize.h>
#include <debug.h>
#include <sinegen.h>

#include "Modem.h"
#include "v32.h"
#include "cancel.h"
#include "FastCoder.h"
#include "BitBuffer.h"

#define THRESHOLD 1.0f	/* sqr of radius of error circle */

static fspec *lpf_fs = mkfilter("-Bu -Lp -o 4 -a 0.125");   /* low-pass at 1200 Hz */

static char *rate_strs[] = { "R1", "R2", "R3", "E3" }; /* indexed by mstate */

static void rx2_loop(void *data) {
	Modem* modem = (Modem*) data;
	v32G* v32 = modem->v32;

	// round-trip-delay calculation
	v32->rxCarrier -> resetphase();
	v32->roundtrip();

	// train equalizer
	v32->rxCarrier -> resetphase();
	v32->rcvdata();

	// train canceler
	v32->rxCarrier -> resetphase();
	v32->traincanceller();

	// exchange data
	v32->rcvdata(); /* never returns */
}

void v32G::initrx() {

	//my_alarm(15); /* 15 sec timeout */
	rxBitBuffer = new BitBuffer(1);
	rxCarrier = new SineGen(1800.0);
	rxFeLpFilter = new cfilter(lpf_fs);
	rxEqualizer = new equalizer(0.25);
	rxGpa = new scrambler(GPA);
	rxTrainingGen = new FastTrainingGen(rxGpa);
	rxDecoder = new FastDecoder();
	// rxDecoder -> printtrellis("debugt1.txt");
	co_debug = new co_debugger(24000);
	can_debug = new debugger(1, 24000);
	dt_debug = new debugger(1, 4000);
	acq_debug = new debugger(2, 24000);

	//atexit(tidyup);
	thread_t rx = thread_create(rx2_loop, modem, "rx2_loop");
	thread_run(rx);

	getratesignals();

	rxDecoder -> setrate(modem->modemOptions->rateword); /* tell decoder what bit rate to use */

	for (int i = 0; i < 128; i++) {
		rxBitBuffer->getBit(); /* discard 128 "1" bits (wait for trellis decoder to settle) */
	}
	//my_alarm(0); /* cancel alarm */
}

void v32G::tidyup() {
	rxEqualizer -> print("debug_eqz.grap");
	rxDecoder -> printtrellis("debugt2.txt");
	co_debug -> print("debug_co.grap");
	can_debug -> print("debug_can.grap");
	dt_debug -> print("debug_dt.grap");
	acq_debug -> print("debug_acq.grap");
}

void v32G::getratesignals() {
	ushort wd = 0;
	for (int i = 0; i < 2; i++) {
		wd = getrate(); /* R1/R3 */
		reportrate(wd);
	}
	/* look for E */
	until ((wd & 0xf000) == 0xf000) {
		wd = getrwd();
	}

	unless (wd == modem->modemOptions->rateword) {
		modem->giveup("failed to detect valid E3");
	}
	reportrate(wd);
}

ushort v32G::getrate() {
	ushort wd = getrwd();
	l: until ((wd & 0xf111) == 0x0111) {
		wd = (wd << 1) | rxBitBuffer->getBit();
	}

	ushort rate = wd;

	for (int i = 0; i < 16; i++) /* look for 16 identical rate signals */
	{
		wd = getrwd();
		if (wd != rate) {
			goto l;
		}
	}
	return rate;
}

ushort v32G::getrwd() {
	ushort wd = 0;
	for (int i = 0; i < 16; i++) {
		wd = (wd << 1) | rxBitBuffer->getBit();
	}
	return wd;
}

void v32G::reportrate(ushort r) {
	//modem->infomsg("<<< %s: rates = %04x", rate_strs[getMState() - 2], r);
	modem->modemOptions->rateword &= r;
	nextMState(); /* from 2 to 3, or 4 to 5, or 5 to 6 */
}

int v32G::getasync() {
	int b = rxBitBuffer->getBit(), nb = 0;
	while (nb < 10 && b) {
		b = rxBitBuffer->getBit();
		nb++;
	}
	if (b) {
		return NOCHAR; /* no char yet */
	}

	int ch = 0;
	for (int i = 0; i < 8; i++) {
		int b = rxBitBuffer->getBit();
		ch = (ch >> 1) | (b << 7);
	}
	return ch;
}

void v32G::roundtrip() {
	samplingDevice->setduplex(64*SYMBLEN);
	rxTiming = 0;
	rxTicker = 'a';
	rxEqualizer -> reset();
	wt_tone(0, 3, 100, true); /* wait for stable AC... */// WAS 50
	int t1 = wt_reversal(0, 3); /* then reversal to CA... */
	nextMState(); /* from 0 to 1 */
	wt_tone(3, 0, 100, false); /* swallow CA... */
	int t2 = wt_reversal(3, 0); /* wait for reversal to AC... */
	nextMState(); /* from 1 to 2 */
	int dt = t2 - t1;
	samplingDevice->setduplex(TRDELAY);
	float ms = (float) (dt - 128*SYMBLEN) / (float) SAMPLERATE * 1000.0f;
	char rtd[32];
	sprintf(rtd, "%.1f", ms);
	//modem->infomsg("RTD = %sms (%d)", rtd, dt); !!! WHY DOES THIS SEGFAULT?
}

void v32G::rcvdata() {
	rxTiming = 0;
	rxTicker = 'A';
	rxEqualizer -> reset();
	wt_tone(0, 1, 230, true); /* wait for stable AB... (S) */
	wt_reversal(0, 1); /* then BA... (Sbar) */
	/* adj equalizer coeffs and symbol timing; use training sequence */
	int bc = FAST_SEG_2 + 2;
	rxGpa -> reset(); /* reset scrambler before using trn */
	rxNextAdj = samplingDevice->samplecount + 2* SAMPLERATE ;
	while (bc < FAST_SEG_3 + 1024) {
		complex z = getsymbol(); /* get equalized symbol */
		complex ez = rxTrainingGen -> get(bc++); /* update equalizer using training sequence */
		float p = power(z - ez);
		acq_debug -> insert(z.re, p);
		rxEqualizer -> update(ez - z);
		adjtiming(); /* adjust symbol timing */
	}
	acq_debug -> tick('X');
	/* adj equalizer coeffs and symbol timing; use decoded data */
	rxDecoder -> reset();
	while (getMState() == 2 || getMState() >= 4) {
		complex z = getsymbol(); /* get equalized symbol */
		int bits = rxDecoder -> decode(z); /* decode into 2 or 3 bits */
		if (rxDecoder -> rate & rb_7200) {
			rxBitBuffer->putBit(rxGpa -> rev(bits >> 2));
		}
		rxBitBuffer->putBit(rxGpa -> rev((bits >> 1) & 1));
		rxBitBuffer->putBit(rxGpa -> rev(bits & 1));
		complex ez = rxDecoder -> getez(); /* get exact (quantized) z */
		rxEqualizer -> update(ez - z); /* update equalizer from data sequence */
		adjtiming(); /* adjust symbol timing */
	}
}

void v32G::wt_tone(int k0, int k1, int max, bool chk) { /* wait for a stable tone */
	complex z0 = ztab2[k0], z1 = ztab2[k1];
	int bc = 0, cnt = 0;
	until (cnt >= max && !(bc & 1)) {
		complex z = getsymbol(); /* get equalized symbol */
		complex ez = (bc++ & 1) ? z1 : z0; /* expected z */
		float p = power(z - ez);
		acq_debug -> insert(z.re, p);
		if (p < THRESHOLD || !chk)
			cnt++;
		else
			cnt = 0;
		rxEqualizer -> short_update(ez - z); /* short update here */
	}
	acq_debug -> tick(rxTicker++);
}

int v32G::wt_reversal(int k0, int k1) { /* wait for a phase reversal */
	complex z0 = ztab2[k0], z1 = ztab2[k1];
	int bc = 0;
	int t = samplingDevice->samplecount;
	bool rev = false;
	until (rev & !(bc & 1)) {
		complex z = getsymbol(); /* get equalized symbol */
		complex ez = (bc++ & 1) ? z1 : z0; /* expected z */
		float p = power(z - ez);
		acq_debug -> insert(z.re, p);
		if (p >= THRESHOLD) {
			t = samplingDevice->samplecount;
			rev = true;
		}
		rxEqualizer -> short_update(ez - z); /* short update here */
	}
	acq_debug -> tick(rxTicker++);
	return t;
}

complex v32G::getsymbol() {
	for (int j = rxTiming; j < 2; j++) /* timing is -1, 0 or +1 */
	{
		complex yz = gethalfsymb();
		rxEqualizer -> insert(yz); /* half-point equalization */
	}
	rxTiming = 0;
	complex z = rxEqualizer -> get();
	co_debug -> insert(z);
	return z;
}

void v32G::adjtiming() {
	if (after(samplingDevice->samplecount, rxNextAdj)) {
		int dt = rxEqualizer -> getdt();
		dt_debug -> insert(dt);
		if (dt > 0) {
			rxTiming--;
			rxEqualizer -> shift(-1);
		}
		if (dt < 0) {
			rxTiming++;
			rxEqualizer -> shift(+1);
		}
		rxNextAdj = samplingDevice->samplecount + 2* SAMPLERATE ; /* adjust every 2 seconds */
	}
}

void v32G::traincanceller() { /* train canceler at half-symbol intervals */
	while (getMState() == 3) {
		complex yz = gethalfsymb();
		canceler -> update(yz);
		can_debug -> insert(power(yz));
	}
}

complex v32G::gethalfsymb() { /* sample at half-symbol intervals */
	complex yz;
	for (int k = 0; k < SYMBLEN / 2; k++) {
		float x = samplingDevice->insample();
		complex cz = rxCarrier -> cnext();
		yz = rxFeLpFilter -> fstep(x * cz); /* translate to baseband */
	}
	complex pe = canceler -> get(); /* subtract predicted echo */
	return yz - pe;
}

