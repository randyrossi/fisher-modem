/* Modem for MIPS   AJF	  January 1995
   V.29 transmit routines */

#include <stdio.h>
#include "../../threadutil/src/threadutil.h"
#include <string.h>
#include <complex.h>
#include <scramble.h>
#include <sinegen.h>

#include "Modem.h"
#include "v29.h"
#include "SlowCoder.h"
#include "BitBuffer.h"

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
#include "v29tx.h"

#define SYNCLOOP_INVOKE 0
#define SYNCLOOP_RESET 1
#define SYNCLOOP_SHUTDOWN 2

static void v29tx_bitloop(void* data) {
  Modem* modem = (Modem*)data;
  v29G* v29 = modem->v29;

  for (;;) {
    int ctl = v29->txCntlBuffer->getBit();
    if (ctl == SYNCLOOP_RESET) {
      continue;
    }
    if (ctl == SYNCLOOP_SHUTDOWN) {
      break;
    }

    v29->txTrainer->reset();
    for (int bc = SLOW_SEG_1; bc < SLOW_SEG_4; bc++) {
      v29->sendsymbol(v29->txTrainer->get(bc)); /* send training sequence */
    }
    v29->txScrambler->reset();
    v29->txEncoder->reset();
    for (;;) {
      int bits = 0;
      for (int i = 0; i < 3; i++) {
        int b = v29->txBitBuffer->getBit();
        ctl = v29->txCntlBuffer->getBit();
        if (ctl == SYNCLOOP_SHUTDOWN || ctl == SYNCLOOP_RESET) {
          break;
        }
        bits = (bits << 1) | v29->txScrambler->fwd(b);
      }
      if (ctl == SYNCLOOP_SHUTDOWN || ctl == SYNCLOOP_RESET) {
        break;
      }
      v29->sendsymbol(v29->txEncoder->encode(bits));
    }
  }
}

void v29G::inittx_v29() {
  unless(txInited) { /* perform once-only initialization */
    txCarrier = new SineGen(1700.0);
    txScrambler = new scrambler(GPC);
    txEncoder = new SlowEncoder();
    txTrainer = new SlowTrainingGen();

    txBitLoop = thread_create(v29tx_bitloop, modem, "bitco");
    thread_run(txBitLoop);

    txInited = true;
  }
  txCntlBuffer->putBit(SYNCLOOP_RESET);
  txBitBuffer->clear();

  for (int i = 0; i < 144; i++) {
    putbit(1); /* scrambled 1s */
  }
}

void v29G::putasync(int n) /* asynchronous output */
{
  uint un = (n >= 0) ? (n << 1) | 0x200 : /* add start bit, 1 stop bit */
                0x3ff;                    /* send mark bits while idle */
  until(un == 0) {
    putbit(un & 1);
    un >>= 1;
  }
}

void v29G::putbit(int bit) /* V.29 bit output (7200 bit/s) */
{
  txCntlBuffer->putBit(SYNCLOOP_INVOKE);
  txBitBuffer->putBit(bit);
}

void v29G::sendsymbol(complex z) {
  static complex a0 = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
  for (int k = 0; k < SYMBLEN; k++) { /* baseband pulse shaping */
    complex s = shapetab[SYMBLEN + k] * a0 + shapetab[k] * a1 +
                shapetab[SYMBLEN - k] * a2 + shapetab[2 * SYMBLEN - k] * a3;
    /* modulate onto carrier */
    complex cz = txCarrier->cnext();
    samplingDevice->outsample(0.2 * (s.re * cz.re + s.im * cz.im));
  }
  a0 = a1;
  a1 = a2;
  a2 = a3;
  a3 = z;
}
