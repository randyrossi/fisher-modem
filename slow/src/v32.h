#ifndef V32_H
#define V32_H

#include <filters.h>
#include <tonedec.h>
#include <sinegen.h>
#include <equalize.h>
#include <debug.h>
#include <scramble.h>
#include <pthread.h>
#include "Modem.h"

class v32G {
 public:
  Modem* modem;
  SamplingDevice* samplingDevice;
  int mstate;
  canceller* canceler;
  pthread_mutex_t lock;

  // tx instance variables
  SineGen* txCarrier;
  scrambler* txGpc;
  FastTrainingGen* txTrainingGen;
  FastEncoder* txEncoder;
  BitBuffer* txBitBuffer;

  // rx instance variables
  SineGen* rxCarrier;
  cfilter* rxFeLpFilter;
  equalizer* rxEqualizer;
  scrambler* rxGpa;
  FastDecoder* rxDecoder;
  FastTrainingGen* rxTrainingGen;
  co_debugger* co_debug;
  debugger *can_debug, *dt_debug, *acq_debug;
  int rxTiming, rxNextAdj;
  char rxTicker;
  BitBuffer* rxBitBuffer;

 public:
  v32G(Modem* modem) {
    this->modem = modem;
    this->samplingDevice = modem->samplingDevice;
    this->mstate = 0;
    pthread_mutex_init(&lock, NULL);
  }

  ~v32G() { pthread_mutex_destroy(&lock); }

  // shared routines
  void resetMState() {
    pthread_mutex_lock(&lock);
    mstate = 0;
    pthread_mutex_unlock(&lock);
  }

  void nextMState() {
    pthread_mutex_lock(&lock);
    mstate++;
    pthread_mutex_unlock(&lock);
  }

  int getMState() {
    pthread_mutex_lock(&lock);
    int v = mstate;
    pthread_mutex_unlock(&lock);
    return v;
  }

  // tx routines
  void inittx();
  void sendrate(ushort);
  void sendsymbol(complex);
  void putasync(int n);

  // rx routines
  void initrx();
  int getasync();
  void tidyup();
  void getratesignals();
  ushort getrate();
  ushort getrwd();
  void reportrate(ushort);
  void roundtrip();
  void rcvdata();
  void wt_tone(int, int, int, bool);
  int wt_reversal(int, int);
  complex getsymbol();
  void adjtiming();
  void traincanceller();
  complex gethalfsymb();
};

#endif
