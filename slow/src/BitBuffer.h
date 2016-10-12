#ifndef BITBUFFER_H
#define BITBUFFER_H

#include <pthread.h>

#define CAPACITY 1024
#define CAPACITY_MASK (CAPACITY - 1)

class BitBuffer {
  int capacity;
  int capacityMask;
  int head, tail;
  int* buf;
  pthread_mutex_t bufferlock;
  bool waitingfornotfull;
  bool waitingfornotempty;
  pthread_cond_t notempty;
  pthread_cond_t notfull;

 public:
  BitBuffer(int capacity) {
    this->capacity = capacity;
    this->capacityMask = capacity - 1;
    head = 0;
    tail = 0;
    waitingfornotfull = false;
    waitingfornotempty = false;
    buf = new int[capacity];
    pthread_mutex_init(&bufferlock, NULL);
    pthread_cond_init(&notempty, NULL);
    pthread_cond_init(&notfull, NULL);
  }

  ~BitBuffer() {
    delete[] buf;
    pthread_cond_destroy(&notempty);
    pthread_cond_destroy(&notfull);
    pthread_mutex_destroy(&bufferlock);
  }

  int getBit();
  void putBit(int);
  void clear();
};

#endif
