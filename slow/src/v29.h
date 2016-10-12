#ifndef V29_H
#define V29_H

#include <filters.h>
#include <tonedec.h>
#include <sinegen.h>
#include <equalize.h>
#include <debug.h>
#include <scramble.h>
#include <pthread.h>
#include <sinegen.h>
#include "SlowCoder.h"
#include "Modem.h"

class v29G {
 public:
  Modem* modem;
  SamplingDevice* samplingDevice;

  // tx instance variables
  bool txInited;
  SineGen* txCarrier;
  scrambler* txScrambler;
  SlowEncoder* txEncoder;
  SlowTrainingGen* txTrainer;
  thread_t txBitLoop;
  BitBuffer* txBitBuffer;
  BitBuffer* txCntlBuffer;

  // rx instance variables
  bool rxInited;
  SineGen* rxCarrier;
  cfilter* fe_lpf;
  scrambler* rxScrambler;
  equalizer* rxEqualizer;
  SlowDecoder* rxDecoder;
  SlowTrainingGen* rxTrainer;
  co_debugger* co_debug;
  debugger* acq_debug;
  int timing;
  int nextadj;
  thread_t rxBitLoop;
  BitBuffer* rxBitBuffer;
  BitBuffer* rxCntlBuffer;

 public:
  v29G(Modem* modem) {
    this->txInited = false;
    this->rxInited = false;
    this->modem = modem;
    this->samplingDevice = modem->samplingDevice;

    this->txBitBuffer = new BitBuffer(1);
    this->rxBitBuffer = new BitBuffer(1);
    this->txCntlBuffer = new BitBuffer(1);
    this->rxCntlBuffer = new BitBuffer(1);
  }

  ~v29G() {
    delete txBitBuffer;
    delete rxBitBuffer;
    delete txCntlBuffer;
    delete rxCntlBuffer;
  }

  // tx routines
  void inittx_v29();
  void sendsymbol(complex);
  void putbit(int bit);
  void putasync(int n);

  // rx routines
  void initrx_v29();
  int getbit();
  void tidydebug();
  void wt_tone();
  void wt_reversal();
  void train_eqz();
  void dataloop();
  complex getsymbol(), gethalfsymb();
  void adjtiming();
  int getasync();
};

#endif
