#ifndef MEMPIPE_H_INCLUDED
#define MEMPIPE_H_INCLUDED

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/sem.h>

#include "commonTypes.h"
#include "SamplingDevice.h"

#define FALSE 0
#define TRUE 1

union semun {
  int val;
  struct semid_ds* buf;
  ushort* array;
};

static const int END1_PRODUCER_SIG_END2_CONSUME_OK = 0;
static const int END2_CONSUMER_SIG_END1_PRODUCE_OK = 1;
static const int END2_PRODUCER_SIG_END1_CONSUME_OK = 2;
static const int END1_CONSUMER_SIG_END2_PRODUCE_OK = 3;

class MemPipe : public SamplingDevice {
 public:
  CommonTypes::EndPoint endPoint;  // originator or answering
  int semsKey;
  int semsId;
  struct sembuf operation[4][1];

  int dspOutBufKey;
  int dspOutBufShmId;
  int dspInBufKey;
  int dspInBufShmId;

  int dspOutBufSize;
  int dspInBufSize;

  unsigned char* dspOutBuf;
  int dspOutBufPos;

  unsigned char* dspInBuf;
  int dspInBufPos;

  unsigned char* localInBuf;

  FILE* fpo;
  FILE* fpi;

  MemPipe(CommonTypes::EndPoint endPoint);

  int dopen();
  void dclose();
  void flush();
  void discardInput();
  void discardOutput();
  void setduplex(int n);
  int isOpen();
  void putByte(unsigned char b);
  unsigned char getByte();

  void onHook();
  void offHook();

  float insample();
  void outsample(float);

 private:
  void v(int);
  void p(int);
};

#endif
