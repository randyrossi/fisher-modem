#include <stdio.h>	// ???
#include <math.h>

#include <complex.h>
#include <filters.h>
#include <equalize.h>
#include <debug.h>
#include <sinegen.h>
#include <goertzel.h>

#include "Modem.h"
#include "v34.h"

#define DEBUGMAX 19200

static fspec *fefs = mkfilter("-Bu -Lp -o 4 -a 0.03125");   /* 400 Hz lowpass */


void rxside(void *data)
  { Modem *modem = (Modem*) data;
    v34G* v34 = modem->v34;
    v34->debugbuf = new char[DEBUGMAX];
    v34->debugptr = 0;
    //atexit(tidydebug);
    v34->rxCarrier = new SineGen(2400.0);
    v34->fe_lpf = new cfilter(fefs);
    v34->eqz = new equalizer(0.25);
    modem->samplingDevice->setduplex(ZDELAY);
    v34->getranging();
    modem->samplingDevice->setduplex(ZDELAY);
    v34->getprobing();
  }
  
void v34G::initrx()
{
	rxside(modem);
}
  

void v34G::tidydebug()
  { for (int k = 0; k < debugptr; k++) putc(debugbuf[k], stderr);
    fprintf(stderr, "\r\n");
  }

void v34G::getranging()
  { bool ack = false;
    do ack = getinfo(); until (ack);		/* keep looking for an info seq with ack bit set */
    mstate++;					/* 0 to 1 (Tx starts sending B) */
    int t1 = getreversal();			/* get reversal */
    mstate++;					/* 1 to 2 (Tx sends Bbar for 10 ms, then silence) */
    int t2 = getreversal();			/* get reversal */
    int dt = t2 - t1;
    float ms = (float) (dt - 2*ZDELAY) / (float) SAMPLERATE * 1000.0f;
    char rtd[32]; sprintf(rtd, "%.1f", ms); 
    modem->infomsg("RTD = %sms (%d)", rtd, dt);
  }

bool v34G::getinfo()
  { eqz -> reset();
    uchar bits = 0xff;
    until (bits == 0x72) bits = (bits << 1) | gbit();	/* look for sync byte */
    debug("\r\n[ ");
    bool ack = false;
    for (int i = 0; i < 37; i++)
      { int b = gbit();
	if (i == 16) ack = b;
	if (i%8 == 7) debug(' ');
      }
    debug(" ]\r\n");
    return ack;
  }

int v34G::getreversal()
  { int cnt = 0;
    while (cnt < 20)
      { if (gbit()) cnt = 0; else cnt++;
      }
    until (gbit()) ;
    debug('*');
    return samplingDevice->samplecount;
  }

int v34G::gbit()
  { complex z = getsymbol();
    bool bit = (z.re > 0.0f);
    complex ez = bit ? complex(+1.0f) : complex(-1.0f);
    eqz -> update(ez - z);
    static uchar bits = 0;
    bits = (bits << 1) | bit;
    uchar tab[] = { 0, 1, 1, 0 };
    int b = tab[bits & 3];   /* diff. decode */
    debug('0'+b);
    return b;
  }

complex v34G::getsymbol()
  { for (int j = 0; j < 2; j++)
      { complex yz = gethalfsymb();
	eqz -> insert(yz);	/* half-point equalization */
      }
    return eqz -> get();
  }

complex v34G::gethalfsymb()
  { /* sample at half-symbol intervals */
    complex yz;
    for (int k = 0; k < ZSYMBLEN/2; k++)
      { float x = samplingDevice->insample();
	complex cz = rxCarrier -> cnext();
	yz = fe_lpf -> fstep(x*cz);	/* translate to baseband */
      }
    return yz;
  }

inline float hypot(complex z) { return hypot(z.im, z.re); }
inline float atan2(complex z) { return atan2(z.im, z.re); }

void v34G::getprobing()
  { for (int i = 0; i < SECS(0.18); i++) samplingDevice->insample();	/* discard 10 ms A + 160 ms L1 + 10 ms */
    goertzel **gvec = new goertzel*[25];
    for (int i = 0; i < 25; i++) gvec[i] = new goertzel((i+1)*150.0);
    for (int i = 0; i < 6000; i++)
      { float x = samplingDevice->insample();
	for (int j = 0; j < 25; j++) gvec[j] -> insert(x);
      }
    uint pm = 0x07a4402;	/* says which cosines are inverted */
    for (int i = 0; i < 25; i++)
      { complex z = gvec[i] -> result();
	float mag = hypot(z) / 6000.0, ph = atan2(z) / M_PI;
	if (pm & 1) ph += 1.0;
	while (ph < -1.0) ph += 2.0;
	while (ph >= 1.0) ph -= 2.0;
	printf("%4d   mag=%6.3f   ph=%6.3f PI\r\n", (i+1)*150, mag, ph);
	pm >>= 1;
	delete gvec[i];
      }
    delete gvec;
  }

void v34G::debug(char *s)
  { int k = 0;
    until (s[k] == '\0') debug(s[k++]);
  }

void v34G::debug(char c)
  { if (debugptr >= DEBUGMAX) exit(3);
    debugbuf[debugptr++] = c;
  }

