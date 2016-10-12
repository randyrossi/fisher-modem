#include <stdio.h>
#include <complex.h>
#include <scramble.h>
#include <sinegen.h>
#include <string.h>

#include "Modem.h"
#include "v32.h"
#include "cancel.h"
#include "FastCoder.h"
#include "BitBuffer.h"

#define TX_TRNLEN 8000 /* length of training sequence transmitted (WAS 4096) \
                          */

// shapetable is only the 2nd (right) half of a raised cosine pulse
// beta = .5
// alpha = 1200
// samplerate = 9600
// length = 17 (for whole shape but only take last 9 values) given 9600
// samplerate
// with sqrt func
// with compensation x/sinx
// i.e. mkshape -r 1.2500000000e-01 5.0000000000e-01 17 -x -l

// shapetable is dynamically generated based on samplerate from toplevel
// Makefile
#include "v32tx.h"

static void tx2_loop(void* data) {
  Modem* modem = (Modem*)data;
  v32G* v32 = modem->v32;

  // round-trip-delay estimation
  while (v32->getMState() < 2) {
    complex z = (v32->getMState() == 0) ? ztab2[0] : ztab2[3]; /* A or C */
    v32->sendsymbol(z);
    v32->sendsymbol(z); /* AA or CC */
  }

  // train equalizer
  while (v32->getMState() == 2) {
    v32->sendsymbol(0.0);
  }

  // train canceller
  v32->txCarrier->resetphase();
  v32->txGpc->reset(); /* reset scrambler before using trn */
  for (int bc = FAST_SEG_1; bc < FAST_SEG_3 + TX_TRNLEN; bc++)
    v32->sendsymbol(v32->txTrainingGen->get(bc));
  v32->nextMState(); /* from 3 to 4 */

  // exchange data
  v32->txEncoder->reset();
  for (;;) {
    int bits = 0;
    bits = (bits << 1) | v32->txGpc->fwd(v32->txBitBuffer->getBit());
    bits = (bits << 1) | v32->txGpc->fwd(v32->txBitBuffer->getBit());
    if (v32->txEncoder->rate & rb_7200) {
      bits = (bits << 1) | v32->txGpc->fwd(v32->txBitBuffer->getBit());
    }
    v32->sendsymbol(v32->txEncoder->encode(bits));
  }
}

void v32G::inittx() {
  modem->infomsg("init\n");

  txCarrier = new SineGen(1800.0);
  txGpc = new scrambler(GPC);
  txTrainingGen = new FastTrainingGen(txGpc);
  txEncoder = new FastEncoder();
  txBitBuffer = new BitBuffer(1);

  thread_t tx = thread_create(tx2_loop, modem, "tx2_loop");
  thread_run(tx);

  txBitBuffer->putBit(1); /* sync to symbol boundary */

  ushort r2 = modem->modemOptions->rateword;

  while (getMState() < 5) {
    sendrate(r2); /* send R2 */
  }
  /* now rateword == R2 & R3 */
  modem->modemOptions->rateword |= 0xf000;
  /* pick highest common bit rate, leave just one rate bit set */
  if (modem->modemOptions->rateword & rb_14400) {
    modem->modemOptions->rateword &= ~(rb_12000 | rb_9600 | rb_7200 | rb_4800);
  } else if (modem->modemOptions->rateword & rb_12000) {
    modem->modemOptions->rateword &= ~(rb_9600 | rb_7200 | rb_4800);
  } else if (modem->modemOptions->rateword & rb_9600) {
    modem->modemOptions->rateword &= ~(rb_7200 | rb_4800);
  } else if (modem->modemOptions->rateword & rb_7200) {
    modem->modemOptions->rateword &= ~rb_4800;
  } else if (modem->modemOptions->rateword & rb_4800) {
    //
  } else {
    modem->giveup("can't agree on a speed!");
  }
  modem->infomsg(">>> E2: rates = %04x",
                 modem->modemOptions->rateword); /* send E2 */
  sendrate(modem->modemOptions->rateword);
  txEncoder->setrate(
      modem->modemOptions->rateword); /* tell encoder what bit rate to use */

  while (getMState() < 6) {
    txBitBuffer->putBit(1); /* sync to Rx side */
  }

  for (int i = 0; i < 128; i++) {
    txBitBuffer->putBit(1); /* followed by 128 "1" bits */
  }
}

void v32G::sendrate(ushort wd) {
  for (int i = 0; i < 16; i++) {
    txBitBuffer->putBit(wd >> 15);
    wd <<= 1;
  }
}

void v32G::putasync(int n) /* asynchronous output */
{
  uint un = (n >= 0) ? (n << 1) | 0x200 : /* add start bit, 1 stop bit */
                0x3ff;                    /* send mark bits while idle */
  until(un == 0) {
    txBitBuffer->putBit(un & 1);
    un >>= 1;
  }
}

void v32G::sendsymbol(complex z) {
  static complex a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
  for (int k = 0; k < SYMBLEN; k++) { /* baseband pulse shaping */
    complex s = shapetab[SYMBLEN + k] * a0 + shapetab[k] * a1 +
                shapetab[SYMBLEN - k] * a2 + shapetab[2 * SYMBLEN - k] * a3;
    /* insert baseband sample into canceller */
    canceler->insert(s);
    /* modulate onto carrier */
    complex cz = txCarrier->cnext();
    samplingDevice->outsample(0.2f * (s.re * cz.re + s.im * cz.im));
  }
  a0 = a1;
  a1 = a2;
  a2 = a3;
  a3 = z;
}
