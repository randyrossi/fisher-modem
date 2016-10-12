#ifndef TERMINAL_DEVICE_H
#define TERMINAL_DEVICE_H

#include <termios.h>
#include "../../threadutil/src/threadutil.h"

#define EOF (-1) /* same as in <stdio.h> */
#define NOCHAR (-3)

#define BUFFERSIZE 1024 /* power of 2 */

struct Buffer {
  int head, tail;
  bool eof;
  char buf[BUFFERSIZE];
  int waitingforinput;
  int waitingforoutput;
  pthread_mutex_t bufferlock;
  pthread_cond_t inputcond;
  pthread_cond_t outputcond;
  int fileHdl;

  Buffer(int fileHdl) {
    head = tail = 0;
    eof = false;
    waitingforoutput = false;
    waitingforinput = false;
    pthread_mutex_init(&bufferlock, NULL);
    pthread_cond_init(&inputcond, NULL);
    pthread_cond_init(&outputcond, NULL);
    this->fileHdl = fileHdl;
  }

  ~Buffer() {
    pthread_cond_destroy(&inputcond);
    pthread_cond_destroy(&outputcond);
    pthread_mutex_destroy(&bufferlock);
  }

  int getch();
  int getchsynch();
  void putch(int);
  void fill(), empty();
};

class TerminalDevice {
 private:
  bool printablesOnly;
  termios cmode, rmode;
  void getmode(termios*);
  void setmode(termios*);

 public:
  Buffer *inbuffer, *outbuffer;
  thread_t inputthread;
  thread_t outputthread;
  char deviceName[16];
  int inputHdl;
  int outputHdl;

  TerminalDevice(char* deviceName);
  void dopen();
  void dclose();
  int inc();
  int incsynch();
  bool isPrintable(int ch);
  void outc(int ch);
};

#endif
