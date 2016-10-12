#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "timeout.h"

void timeout_init(void* data) {
  timespec oneSecond;
  timeout_t tmout = (timeout_t)data;

  // The first time we run, we hold up this thread until signalled by run
  pthread_mutex_lock(&tmout->lock);

  while (!tmout->canceled && tmout->timeoutSeconds > 0) {
    clock_gettime(CLOCK_REALTIME, &oneSecond);
    oneSecond.tv_sec += 1;
    pthread_cond_timedwait(&tmout->cond, &tmout->lock, &oneSecond);
    tmout->timeoutSeconds--;
  }

  if (!tmout->canceled && tmout->timeoutSeconds <= 0) {
    // We hit the timeout, call the callback function
    tmout->proc(tmout->arg);
  }
  pthread_mutex_unlock(&tmout->lock);

  // Now exit
  pthread_exit(NULL);
}

timeout_t timeout_create(timeout_callback_proc_t proc,
                         void* arg,
                         long timeoutSeconds) {
  timeout_t tmout = (timeout_t)malloc(sizeof(timeout));

  tmout->proc = proc;
  tmout->arg = arg;
  tmout->canceled = false;
  tmout->timeoutSeconds = timeoutSeconds;

  if (pthread_cond_init(&tmout->cond, NULL) != 0) {
    fprintf(stderr, "[thread] cond init error\n");
    return NULL;
  }

  if (pthread_mutex_init(&tmout->lock, NULL) != 0) {
    fprintf(stderr, "[thread] mutex init error\n");
    return NULL;
  }

  if (pthread_attr_init(&tmout->attr) != 0) {
    fprintf(stderr, "[thread] attr init error\n");
    return NULL;
  }
  pthread_attr_setdetachstate(&tmout->attr, PTHREAD_CREATE_JOINABLE);

  if (pthread_create(&tmout->threadid, &tmout->attr,
                     (void* (*)(void*))timeout_init, tmout) != 0) {
    fprintf(stderr, "[thread] pthread_create error\n");
    return NULL;
  }

  return tmout;
}

void timeout_cancel(timeout_t tmout) {
  if (tmout == NULL) {
    return;
  }

  // Wake up the specified thread
  pthread_mutex_lock(&tmout->lock);
  tmout->canceled = 1;
  pthread_cond_signal(&tmout->cond);
  pthread_mutex_unlock(&tmout->lock);
}

void timeout_destroy(timeout_t tmout) {
  pthread_attr_destroy(&tmout->attr);
  pthread_mutex_destroy(&tmout->lock);
  pthread_cond_destroy(&tmout->cond);
  free(tmout);
}
