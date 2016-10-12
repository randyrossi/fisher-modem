#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <pthread.h>

#include "../../threadutil/src/threadutil.h"
#include "private.h"
#include "exceptions.h"
#include "TxRxHelper.h"

// Consume and throw away
void helper_inputrunner(void* data) {
  TxRxHelper* hp = (TxRxHelper*)data;

  for (;;) {
    pthread_mutex_lock(&hp->inputlock);
    if (!hp->reading) {
      break;
    }
    if (hp->readSuspended) {
      pthread_cond_signal(&hp->readWasSuspended);
      pthread_cond_wait(&hp->resumeRead, &hp->inputlock);
    }
    pthread_mutex_unlock(&hp->inputlock);
    hp->samplingDevice->insample();
  }
}

// Write silence
void helper_outputrunner(void* data) {
  TxRxHelper* hp = (TxRxHelper*)data;
  for (;;) {
    pthread_mutex_lock(&hp->outputlock);
    if (!hp->writing) {
      break;
    }
    if (hp->writeSuspended) {
      pthread_cond_signal(&hp->writeWasSuspended);
      pthread_cond_wait(&hp->resumeWrite, &hp->outputlock);
    }
    pthread_mutex_unlock(&hp->outputlock);
    hp->samplingDevice->outsample(0.0f);
  }
}

TxRxHelper::TxRxHelper(SamplingDevice* sd) {
  this->inputthread = NULL;
  this->outputthread = NULL;
  this->samplingDevice = sd;

  pthread_mutex_init(&inputlock, NULL);
  pthread_mutex_init(&outputlock, NULL);
  reading = false;
  writing = false;
  readSuspended = false;
  writeSuspended = false;
  pthread_cond_init(&resumeRead, NULL);
  pthread_cond_init(&resumeWrite, NULL);
  pthread_cond_init(&readWasSuspended, NULL);
  pthread_cond_init(&writeWasSuspended, NULL);
}

TxRxHelper::~TxRxHelper() {
  pthread_cond_destroy(&writeWasSuspended);
  pthread_cond_destroy(&readWasSuspended);
  pthread_cond_destroy(&resumeWrite);
  pthread_cond_destroy(&resumeRead);
  pthread_mutex_destroy(&outputlock);
  pthread_mutex_destroy(&inputlock);
}

void TxRxHelper::stopReading() {
  pthread_mutex_lock(&inputlock);
  bool joinAndDestroy = false;
  if (reading) {
    reading = false;
    joinAndDestroy = true;
  }
  pthread_mutex_unlock(&inputlock);
  if (joinAndDestroy) {
    thread_join(inputthread);
    thread_destroy(inputthread);
  }
  inputthread = NULL;
}

void TxRxHelper::suspendReading() {
  pthread_mutex_lock(&inputlock);
  if (!readSuspended) {
    readSuspended = true;
    if (reading) {
      // wait for confirmation that read loop is going to block
      pthread_cond_wait(&readWasSuspended, &inputlock);
    }
  }
  pthread_mutex_unlock(&inputlock);
}

void TxRxHelper::resumeReading() {
  if (!reading) {
    reading = true;
    inputthread = thread_create(helper_inputrunner, this, "reader");
    thread_run(inputthread);
  } else {
    pthread_mutex_lock(&inputlock);
    if (readSuspended) {
      pthread_cond_signal(&resumeRead);
    }
    readSuspended = false;
    pthread_mutex_unlock(&inputlock);
  }
}

void TxRxHelper::stopWriting() {
  pthread_mutex_lock(&outputlock);
  bool joinAndDestroy = false;
  if (writing) {
    writing = false;
    joinAndDestroy = true;
  }
  pthread_mutex_unlock(&outputlock);
  if (joinAndDestroy) {
    thread_join(outputthread);
    thread_destroy(outputthread);
  }
  outputthread = NULL;
}

void TxRxHelper::suspendWriting() {
  pthread_mutex_lock(&outputlock);
  if (!writeSuspended) {
    writeSuspended = true;
    if (writing) {
      // wait for confirmation that write loop is going to block
      pthread_cond_wait(&writeWasSuspended, &outputlock);
    }
  }
  pthread_mutex_unlock(&outputlock);
}

void TxRxHelper::resumeWriting() {
  if (!writing) {
    writing = true;
    outputthread = thread_create(helper_outputrunner, this, "writer");
    thread_run(outputthread);
  } else {
    pthread_mutex_lock(&outputlock);
    if (writeSuspended) {
      pthread_cond_signal(&resumeWrite);
    }
    writeSuspended = false;
    pthread_mutex_unlock(&outputlock);
  }
}
