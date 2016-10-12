#include "BitBuffer.h"
#include <stdio.h>

int BitBuffer::getBit() {
  int c;
  pthread_mutex_lock(&bufferlock);
  while (head - tail == 0) {
    pthread_cond_wait(&notempty, &bufferlock);
  }

  c = buf[tail++ & capacityMask];

  pthread_cond_signal(&notfull);

  pthread_mutex_unlock(&bufferlock);
  return c;
}

void BitBuffer::putBit(int ch) {
  pthread_mutex_lock(&bufferlock);

  while ((head - tail) >= capacity) {
    pthread_cond_wait(&notfull, &bufferlock);
  }

  buf[head++ & capacityMask] = ch;

  pthread_cond_signal(&notempty);

  pthread_mutex_unlock(&bufferlock);
}

void BitBuffer::clear() {
  pthread_mutex_lock(&bufferlock);
  if (head - tail == 0) {
    pthread_mutex_unlock(&bufferlock);
    return;
  }

  head = 0;
  tail = 0;

  pthread_cond_signal(&notfull);

  pthread_mutex_unlock(&bufferlock);
}
