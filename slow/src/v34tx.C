#include <complex.h>
#include <sinegen.h>

#include "Modem.h"
#include "v34.h"

static uchar info0c[] = {0xf7, 0x2f, 0xf8, 0x00, 0x76, 0x8f, 0x80};

// This is only the 2nd (right) half of a raised cosine pulse
// beta = .5
// alpha = 300
// samplerate = 9600
// length = 65 (for whole shape but only take last 33 values)
// no sqrt
// no compensation
// i.e. mkshape -c 3.1250000000e-02 5.0000000000e-01 65 -l
// shapetable is dynamically generated based on samplerate from toplevel
// Makefile
#include "v34tx.h"

/* THIS IS UNFINISHED */
/* HOW TO GENERATE THIS FROM SAMPLE RATE + BAUD RATE ???? */
/*
static float probing[4*ZSYMBLEN] =
  {  0.2380952,	 0.2393710,  0.1284067, -0.1539139, -0.0523930,
0.0680675, -0.2297064,	 0.0045923,
     0.0812908, -0.1809436,  0.1310969, -0.0137466, -0.2081474,	 0.1719744,
0.1724568,	 0.1854495,
     0.0952381, -0.1779333,  0.0502188,	 0.2156533,  0.1802528, -0.0099911,
-0.2293379,	 0.0361867,
     0.0139473, -0.2433915,  0.0585733,	 0.1934423, -0.1101886, -0.2021118,
-0.0817082, -0.1327051,
    -0.2380952, -0.1327051, -0.0817082, -0.2021118, -0.1101886,	 0.1934423,
0.0585733, -0.2433915,
     0.0139473,	 0.0361867, -0.2293379, -0.0099911,  0.1802528,	 0.2156533,
0.0502188, -0.1779333,
     0.0952381,	 0.1854495,  0.1724568,	 0.1719744, -0.2081474, -0.0137466,
0.1310969, -0.1809436,
     0.0812908,	 0.0045923, -0.2297064,	 0.0680675, -0.0523930, -0.1539139,
0.1284067,	 0.2393710,
  };
*/

static void txside(void* data) {
  Modem* modem = (Modem*)data;
  v34G* v34 = modem->v34;
  v34->txCarrier = new SineGen(1200.0);
  v34->pbit(0); /* the "point of arbitrary phase" */
  while (v34->mstate == 0)
    v34->sendinfo(info0c, 45);
  while (v34->mstate == 1)
    v34->pbit(0); /* B */
  v34->pbit(1);   /* phase reversal */
  for (int i = 0; i < 38; i++)
    v34->pbit(0); /* Bbar */
  for (;;)
    v34->outsymbol(0.0f); /* silence */
}

void v34G::inittx() {
  txside(modem);
}

void v34G::sendinfo(uchar* info, int nb) {
  int p = 0;
  uchar w = 0;
  for (int i = 0; i < nb; i++) {
    if (i % 8 == 0)
      w = info[p++];
    pbit(w >> 7);
    w <<= 1;
  }
}

void v34G::pbit(int b) {
  static float x = 1.0;
  if (b)
    x = -x; /* diff. encode */
  outsymbol(x);
}

void v34G::outsymbol(float x) {
  static float a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
  a0 = a1;
  a1 = a2;
  a2 = a3;
  a3 = x;
  for (int k = 0; k < ZSYMBLEN; k++) { /* baseband pulse shaping */
    float s = shapetab[ZSYMBLEN + k] * a0 + shapetab[k] * a1 +
              shapetab[ZSYMBLEN - k] * a2 + shapetab[2 * ZSYMBLEN - k] * a3;
    /* modulate onto carrier */
    float cx = txCarrier->fnext();
    samplingDevice->outsample(s * cx);
  }
}
