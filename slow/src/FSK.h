#ifndef FSK_H
#define FSK_H

#include <filters.h>
#include <tonedec.h>
#include <sinegen.h>
#include "Modem.h"
class FSK {
 private:
  Modem* modem;
  SamplingDevice* samplingDevice;

  // tx instance variables
  SineGen* sgen;
  float tone0, tone1;
  int txbitlen;
  uchar prevbits;

  // rx instance variables
  int rxbitlen;
  bool inited;
  tone_detector* td0;
  tone_detector* td1;
  BitBuffer* syncReturnBuffer;
  BitBuffer* syncPutBuffer;
  thread_t rxSyncProcessor;

 public:
  FSK(Modem* m) {
    modem = m;
    samplingDevice = m->samplingDevice;
    sgen = new SineGen(0.0);
    inited = false;
    syncReturnBuffer = new BitBuffer(1);
    syncPutBuffer = new BitBuffer(1);
  }

  ~FSK() {
    delete sgen;
    delete syncReturnBuffer;
    delete syncPutBuffer;
  }

  // tx routines
  void inittx_fsk(ModemOptions::vmode mode);
  void putasync(int n);
  void putsync(int x);
  void putoctet(uchar n);
  void pbit(int bit);

  // rx routines
  void initrx_fsk(ModemOptions::vmode mode);
  int getasync();
  int getsync();
  int syncprocess2();
  inline int getsample();
};

#endif
