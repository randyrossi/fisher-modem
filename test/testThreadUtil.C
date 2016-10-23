#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../threadutil/src/threadutil.h"

void thread_body(void* data) {
  void* d = malloc(1024);
  fprintf(stdout, "thread is running\n");
  int t = rand() % 5 + 1;
  usleep(t * 1000000);
  fprintf(stdout, "thread is exiting\n");
  free(d);
}

int main(int argc, char* argv[]) {
  int num = 100;
  fprintf(stdout, "main thread running\n");
  thread_t threads[num];
  for (int i = 0; i < num; i++) {
    threads[i] = thread_create(thread_body, NULL, "thread1");
  }

  fprintf(stdout, "main calling thread_run()\n");
  for (int i = 0; i < num; i++) {
    thread_run(threads[i]);
  }

  fprintf(stdout, "main calling thread_join()\n");
  for (int i = 0; i < num; i++) {
    thread_join(threads[i]);
  }

  fprintf(stdout, "main calling thread_destroy()\n");
  for (int i = 0; i < num; i++) {
    thread_destroy(threads[i]);
  }
}
