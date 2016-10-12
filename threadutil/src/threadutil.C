#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "threadutil.h"

void thread_init(void* data) {
  thread_t thrd = (thread_t)data;

  // The first time we run, we hold up this thread until signalled by run
  pthread_mutex_lock(&thrd->lock);
  if (thrd->run == 0) {
    pthread_cond_wait(&thrd->cond, &thrd->lock);
  }
  pthread_mutex_unlock(&thrd->lock);

  // Actually call this thread's procedure
  thrd->proc(thrd->arg);

  // Now exit
  pthread_exit(NULL);
}

thread_t thread_create(thread_proc_t proc, void* arg, char* name) {
  thread_t thrd = (thread_t)malloc(sizeof(thread));

  sprintf(thrd->name, "%s", name);
  thrd->proc = proc;
  thrd->arg = arg;
  thrd->run = 0;
  thrd->waiting = 0;

  if (pthread_cond_init(&thrd->cond, NULL) != 0) {
    fprintf(stderr, "[thread] cond init error\n");
    return NULL;
  }

  if (pthread_mutex_init(&thrd->lock, NULL) != 0) {
    fprintf(stderr, "[thread] mutex init error\n");
    return NULL;
  }

  if (pthread_attr_init(&thrd->attr) != 0) {
    fprintf(stderr, "[thread] attr init error\n");
    return NULL;
  }
  pthread_attr_setdetachstate(&thrd->attr, PTHREAD_CREATE_JOINABLE);

  if (pthread_create(&thrd->threadid, &thrd->attr,
                     (void* (*)(void*))thread_init, thrd) != 0) {
    fprintf(stderr, "[thread] pthread_create error\n");
    return NULL;
  }

  return thrd;
}

void thread_run(thread_t thrd) {
  if (thrd == NULL) {
    return;
  }

  // Wake up the specified thread
  pthread_mutex_lock(&thrd->lock);
  thrd->run = 1;
  pthread_cond_signal(&thrd->cond);
  pthread_mutex_unlock(&thrd->lock);
}

void thread_join(thread_t thrd) {
  if (thrd == NULL) {
    return;
  }

  // Wait for that thread to exit...
  pthread_join(thrd->threadid, NULL);
}

void thread_destroy(thread_t thrd) {
  pthread_attr_destroy(&thrd->attr);
  pthread_mutex_destroy(&thrd->lock);
  pthread_cond_destroy(&thrd->cond);
  free(thrd);
}

void thread_wait(thread_t thrd) {
  // The current thread will block and waiting will be set to 1 to indicate
  // there is a thread waiting to be notified
  pthread_mutex_lock(&thrd->lock);
  thrd->waiting++;
  pthread_cond_wait(&thrd->cond, &thrd->lock);
  pthread_mutex_unlock(&thrd->lock);
}

void thread_notify(thread_t thrd) {
  // Whatever thread was waiting will be woken up.  If there was no thread
  // waiting, the notification will be lost
  pthread_mutex_lock(&thrd->lock);
  if (thrd->waiting > 0) {
    thrd->waiting--;
    pthread_cond_signal(&thrd->cond);
  }
  pthread_mutex_unlock(&thrd->lock);
}
