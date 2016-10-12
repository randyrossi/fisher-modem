#ifndef V34_H
#define V34_H

#include <filters.h>
#include <tonedec.h>
#include <sinegen.h>
#include <equalize.h>
#include <debug.h>
#include <scramble.h>
#include <pthread.h>
#include "Modem.h"

class v34G {
 public:
  Modem* modem;
  SamplingDevice* samplingDevice;
  int mstate;
  pthread_mutex_t lock;

  // tx instance variables
  SineGen* txCarrier;

  // rx instance variables
  cfilter* fe_lpf;
  equalizer* eqz;
  SineGen* rxCarrier;
  char* debugbuf;
  int debugptr;

 public:
  v34G(Modem* modem) {
    this->modem = modem;
    this->samplingDevice = modem->samplingDevice;
    this->mstate = 0;
    pthread_mutex_init(&lock, NULL);
  }

  ~v34G() { pthread_mutex_destroy(&lock); }

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
  void sendinfo(uchar*, int);
  void pbit(int);
  void outsymbol(float);

  // rx routines
  void initrx();
  void tidydebug();
  void getranging();
  bool getinfo();
  int getreversal();
  int gbit();
  complex getsymbol();
  complex gethalfsymb();
  void getprobing();
  void debug(char*);
  void debug(char);
};

#endif
