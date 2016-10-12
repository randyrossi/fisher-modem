#ifndef THREADUTIL_H
#define THREADUTIL_H

#include <pthread.h>

typedef void (*thread_proc_t)(void*);

typedef struct s_thread {
  thread_proc_t proc;
  void* arg;
  int run;  // switch to hold up the thread until thread_run()
  int waiting;

  pthread_mutex_t lock;
  pthread_cond_t cond;
  pthread_attr_t attr;
  pthread_t threadid;
  char name[16];
} thread;

typedef thread* thread_t;

extern thread_t thread_create(thread_proc_t proc, void* arg, char* name);
extern void thread_destroy(thread_t thread);
extern void thread_run(thread_t thread);
extern void thread_join(thread_t thread);
extern void thread_wait(thread_t thread);
extern void thread_notify(thread_t thread);

#endif
