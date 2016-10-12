#ifndef TXRXHELPER_H
#define TXRXHELPER_H

#include <termios.h>
#include <pthread.h>
#include "../../threadutil/src/threadutil.h"
#include "SamplingDevice.h"

class TxRxHelper {
 private:
 public:
  SamplingDevice* samplingDevice;

  thread_t inputthread;
  thread_t outputthread;

  pthread_mutex_t inputlock;
  pthread_mutex_t outputlock;
  bool reading;
  bool writing;
  bool readSuspended;
  bool writeSuspended;
  pthread_cond_t resumeRead;
  pthread_cond_t resumeWrite;
  pthread_cond_t readWasSuspended;
  pthread_cond_t writeWasSuspended;

  TxRxHelper(SamplingDevice* sd);
  ~TxRxHelper();
  void stopReading();
  void suspendReading();
  void resumeReading();
  void stopWriting();
  void suspendWriting();
  void resumeWriting();
};

#endif
