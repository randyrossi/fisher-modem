#ifndef TIMEOUT_H
#define TIMEOUT_H

#include <pthread.h>

typedef void (*timeout_callback_proc_t)(void*);

typedef struct s_timeout {
  timeout_callback_proc_t proc;
  void* arg;
  long timeoutSeconds;
  bool canceled;

  pthread_mutex_t lock;
  pthread_cond_t cond;
  pthread_attr_t attr;
  pthread_t threadid;
} timeout;

typedef timeout* timeout_t;

extern timeout_t timeout_create(timeout_callback_proc_t proc,
                                void* arg,
                                long timeoutSeconds);
extern void timeout_destroy(timeout_t tmout);
extern void timeout_cancel(timeout_t tmout);

#endif
