#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <math.h>

#include "commonTypes.h"
#include "SamplingDevice.h"
#include "memPipe.h"

#define MODULE_NAME "memPipe"

MemPipe::MemPipe(CommonTypes::EndPoint endPoint)
    : SamplingDevice(SIGNED_8BIT_PCM) {
  this->endPoint = endPoint;
  semsKey = 1240;
  if (endPoint == CommonTypes::ENDPOINT_1) {
    dspOutBufKey = 1241;
    dspInBufKey = 1242;
  } else {
    dspOutBufKey = 1242;
    dspInBufKey = 1241;
  }
}

int MemPipe::dopen() {
  int mode;

  if (endPoint == CommonTypes::ENDPOINT_1) {
    mode = IPC_CREAT;
  } else {
    mode = 0;
  }

  semsId = semget(semsKey, 4, mode | 0666);
  if (semsId < 0) {
    fprintf(stderr, "%s: can't create semaphore\n", MODULE_NAME);
    return 7;
  }

  // If this is the originating end, set all semaphores to 0
  if (endPoint == CommonTypes::ENDPOINT_1) {
    semun argument;
    for (int i = 0; i < 4; i++) {
      argument.val = 0;
      if (semctl(semsId, i, SETVAL, argument) < 0) {
        fprintf(stderr, "%s: can't set semaphore\n", MODULE_NAME);
        return 7;
      }
    }
  }

  dspOutBufSize = SAMPLERATE / 2;
  dspInBufSize = SAMPLERATE / 2;

  dspOutBufShmId = shmget(dspOutBufKey, dspOutBufSize, mode | 0644);
  if (dspOutBufShmId < 0) {
    fprintf(stderr, "%s: can't allocate shared memory segment for outbuf %d\n",
            MODULE_NAME, dspOutBufSize);
    perror("REASON");
    return 7;
  }

  dspOutBuf = (unsigned char*)shmat(dspOutBufShmId, NULL, 0);

  dspInBufShmId = shmget(dspInBufKey, dspInBufSize, mode | 0644);

  if (dspInBufShmId < 0) {
    fprintf(stderr, "%s: can't allocate shared memory segment for inbuf %d\n",
            MODULE_NAME, dspInBufSize);
    perror("REASON");
    return 7;
  }

  dspInBuf = (unsigned char*)shmat(dspInBufShmId, NULL, 0);
  localInBuf = (unsigned char*)malloc(dspInBufSize);

  if (dspOutBuf == NULL || dspInBuf == NULL || localInBuf == NULL) {
    fprintf(stderr, "%s: can't allocate dsp buffer\n", MODULE_NAME);
    return 7;
  }

  dspOutBufPos = 0;
  dspInBufPos = 0;

  if (devOutMode == DEV_OUT_RECORD) {
    if (endPoint == CommonTypes::ENDPOINT_1) {
      fpo = fopen("sndout_o", "w");
    } else {
      fpo = fopen("sndout_a", "w");
    }
  } else if (devOutMode == DEV_OUT_PLAY_FROM_FILE) {
    if (endPoint == CommonTypes::ENDPOINT_1) {
      fpo = fopen("sndout_o", "r");
    } else {
      fpo = fopen("sndout_a", "r");
    }
  }

  if (devInMode == DEV_IN_RECORD) {
    if (endPoint == CommonTypes::ENDPOINT_1) {
      fpi = fopen("sndin_o", "w");
    } else {
      fpi = fopen("sndin_a", "w");
    }
  } else if (devInMode == DEV_IN_PLAY_FROM_FILE) {
    if (endPoint == CommonTypes::ENDPOINT_1) {
      fpi = fopen("sndin_o", "r");
    } else {
      fpi = fopen("sndin_a", "r");
    }
  }

  return 0;
}

void MemPipe::dclose() {
  if (devInMode == DEV_OUT_RECORD || devInMode == DEV_OUT_PLAY_FROM_FILE) {
    fclose(fpo);
  }
  if (devInMode == DEV_IN_RECORD || devInMode == DEV_IN_PLAY_FROM_FILE) {
    fclose(fpi);
  }
  // Now free up all the memory and close handles
  shmdt(dspOutBuf);
  shmdt(dspInBuf);
}

int MemPipe::isOpen() {
  return 1;
}

void MemPipe::putByte(unsigned char b) {
  dspOutBuf[dspOutBufPos] = b;
  dspOutBufPos++;
  if (dspOutBufPos >= dspOutBufSize) {
    if (devOutMode == DEV_OUT_RECORD) {
      fwrite(dspOutBuf, 1, dspOutBufSize, fpo);
      fflush(fpo);
    } else if (devOutMode == DEV_OUT_PLAY_FROM_FILE) {
      fread(dspOutBuf, 1, dspOutBufSize, fpo);
    }
    if (endPoint == CommonTypes::ENDPOINT_1) {
      v(END1_PRODUCER_SIG_END2_CONSUME_OK);
      usleep(500000);  // 1/2 sec
      p(END2_CONSUMER_SIG_END1_PRODUCE_OK);
    } else {
      v(END2_PRODUCER_SIG_END1_CONSUME_OK);
      usleep(500000);  // 1/2 sec
      p(END1_CONSUMER_SIG_END2_PRODUCE_OK);
    }
    dspOutBufPos = 0;
  }
}

unsigned char MemPipe::getByte() {
  if (dspInBufPos == 0) {
    if (endPoint == CommonTypes::ENDPOINT_2) {
      p(END1_PRODUCER_SIG_END2_CONSUME_OK);
    } else {
      p(END2_PRODUCER_SIG_END1_CONSUME_OK);
    }
    memcpy(localInBuf, dspInBuf, dspInBufSize);
    if (devInMode == DEV_IN_RECORD) {
      fwrite(localInBuf, 1, dspInBufSize, fpi);
      fflush(fpi);
    } else if (devInMode == DEV_IN_PLAY_FROM_FILE) {
      fread(localInBuf, 1, dspInBufSize, fpi);
    }
    if (endPoint == CommonTypes::ENDPOINT_2) {
      v(END2_CONSUMER_SIG_END1_PRODUCE_OK);
    } else {
      v(END1_CONSUMER_SIG_END2_PRODUCE_OK);
    }
  }
  unsigned char b = localInBuf[dspInBufPos];
  dspInBufPos++;
  if (dspInBufPos >= dspInBufSize) {
    // We've reached the end of the buffer, signal the consumer
    // on the other end it should start producing again, we will
    // block on another semaphore
    dspInBufPos = 0;
  }
  return b;
}

// TODO: This will cause silence which is not what the caller wants.
// Fix this.
void MemPipe::flush() {
  while (dspOutBufPos < dspOutBufSize - 1) {
    putByte(0);
  }
}

void MemPipe::discardInput() {
  dspInBufPos = 0;
  if (endPoint == CommonTypes::ENDPOINT_2) {
    v(END2_CONSUMER_SIG_END1_PRODUCE_OK);
  } else {
    v(END1_CONSUMER_SIG_END2_PRODUCE_OK);
  }
}

void MemPipe::discardOutput() {
  dspOutBufPos = 0;
}

void MemPipe::setduplex(int n) {}

void MemPipe::v(int semaphore) {
  operation[semaphore][0].sem_num = semaphore;
  operation[semaphore][0].sem_op = 1;
  operation[semaphore][0].sem_flg = 0;
  if (semop(semsId, operation[semaphore], 1) != 0) {
    fprintf(stderr, "%s: can't do v op\n", MODULE_NAME);
    perror("REASON");
  }
}

void MemPipe::p(int semaphore) {
  operation[semaphore][0].sem_num = semaphore;
  operation[semaphore][0].sem_op = -1;
  operation[semaphore][0].sem_flg = 0;
  if (semop(semsId, operation[semaphore], 1) != 0) {
    fprintf(stderr, "%s: can't do p op\n", MODULE_NAME);
    perror("REASON");
  }
}

void MemPipe::offHook() {}

void MemPipe::onHook() {}

float MemPipe::insample() {
  unsigned char b = getByte();
  float y = (((float)b) / 64.0) - 2.0;
  samplecount++;
  return y;
}

void MemPipe::outsample(float x) {
  float y = x + 2.0;
  y = y * 64.0;
  putByte((unsigned char)y);
}
