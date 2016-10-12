#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <pthread.h>

#include "../../threadutil/src/threadutil.h"
#include "private.h"
#include "exceptions.h"
#include "TerminalDevice.h"

#define BUFFERMASK (BUFFERSIZE - 1)

#define STDIN_HDL 0  /* fd 0 */
#define STDOUT_HDL 1 /* fd 1 */

void inputrunner(void* data) {
  TerminalDevice* td = (TerminalDevice*)data;

  while (td->inbuffer->eof == false) {
    try {
      td->inbuffer->fill();
    } catch (StdinReadException e) {
      e.getMessage();
      td->inbuffer->eof = true;
    }
  }
}

void outputrunner(void* data) {
  TerminalDevice* td = (TerminalDevice*)data;

  while (td->outbuffer->eof == false) {
    try {
      td->outbuffer->empty();
    } catch (StdoutWriteException e) {
      e.getMessage();
      td->outbuffer->eof = true;
    }
  }
}

TerminalDevice::TerminalDevice(char* deviceName) {
  strcpy(this->deviceName, deviceName);
  printablesOnly = false;
}

void TerminalDevice::dopen()  // throws StdioInitException
{
  if (strlen(deviceName) == 0) {
    inputHdl = STDIN_HDL;
    outputHdl = STDOUT_HDL;
  } else {
    int serialDev = open(deviceName, O_RDWR | O_NOCTTY);
    inputHdl = outputHdl = serialDev;
    if (inputHdl < 0) {
      fprintf(stderr, "can't open %s\n", deviceName);
    }
    int rc;

    rc = grantpt(serialDev);
    if (rc < 0) {
      fprintf(stderr, "can't grant rights to %s\n", deviceName);
    }
    rc = unlockpt(serialDev);
    if (rc < 0) {
      fprintf(stderr, "can't unlock %s\n", deviceName);
    }
    char* pSptyName = ptsname(serialDev);

    fprintf(stdout, "pts device is %s\n", pSptyName);
  }

  inbuffer = new Buffer(inputHdl);
  outbuffer = new Buffer(outputHdl);

  // get current mode
  getmode(&cmode);
  // copy the info
  rmode = cmode;
  // modify for raw mode
  rmode.c_iflag = rmode.c_oflag = rmode.c_lflag = 0;
  // set timeout params
  rmode.c_cc[VMIN] = 1;
  rmode.c_cc[VTIME] = 0;
  // set raw mode
  setmode(&rmode);

  // Spawn a thread that will continuously read from input stream
  inputthread = thread_create(inputrunner, this, "input");

  // Spawn a thread that will continuously write to output stream
  outputthread = thread_create(outputrunner, this, "output");

  thread_run(inputthread);
  thread_run(outputthread);
}

void TerminalDevice::dclose() {
  // Send a single EOF character to the outbuffer so the thread will
  // wake up and exit.  If it happens to be in the process of
  // emptying its buffer, thats okay, the eof flag will tell the thread
  // to stop anyway
  outbuffer->eof = true;

  try {
    outbuffer->putch(EOF);
  } catch (StdoutBufferOverflowException e) {
    // ignore this since we are closing anyway
  }

  // Since the input thread polls on stdin, we simply flip the flag
  inbuffer->eof = true;

  thread_join(inputthread);
  thread_join(outputthread);

  thread_destroy(inputthread);
  thread_destroy(outputthread);

  delete inbuffer;
  delete outbuffer;

  setmode(&cmode); /* set cooked mode */

  if (inputHdl != STDIN_HDL) {
    close(inputHdl);
  }
}

void TerminalDevice::getmode(termios* tm)  // throws StdioInitException
{
  int code = tcgetattr(inputHdl, tm);
  unless(code == 0) throw StdioInitException();
}

void TerminalDevice::setmode(termios* tm) {
  tcsetattr(inputHdl, TCSADRAIN, tm); /* ignore errors here */
}

// Never blocks, caller always get a value
int TerminalDevice::inc() {
  /* get char from stdin */
  return inbuffer->getch();
}

// Never blocks, caller always get a value
int TerminalDevice::incsynch() {
  /* get char from stdin */
  return inbuffer->getchsynch();
}

// Never blocks, caller always gets a value
int Buffer::getch() {
  int c;
  pthread_mutex_lock(&bufferlock);
  c = ((head - tail) > 0) ? buf[tail++ & BUFFERMASK] & 0xff : eof ? EOF
                                                                  : NOCHAR;
  pthread_mutex_unlock(&bufferlock);
  return c;
}

int Buffer::getchsynch() {
  int c;
  pthread_mutex_lock(&bufferlock);
  if (head - tail == 0) {
    waitingforinput = true;
    pthread_cond_wait(&inputcond, &bufferlock);
  }

  c = buf[tail++ & BUFFERMASK] & 0xff;

  pthread_mutex_unlock(&bufferlock);
  return c;
}

// Never blocks, caller can always put a value
void TerminalDevice::outc(int ch)  // throws StdoutBufferOverflowException
{
  /* put char to stdout */
  // TODO make this an option for testing purposes to avoid bells
  if (isPrintable(ch)) {
    outbuffer->putch(ch);
  }
}

bool TerminalDevice::isPrintable(int ch) {
  if (printablesOnly) {
    return ((ch >= 32 && ch <= 126) || (ch >= 160 && ch <= 255) || ch == 10 ||
            ch == 13);
  } else {
    return true;
  }
}

void Buffer::putch(int ch)  // throws StdoutBufferOverflowException
{
  if (ch >= 0 || ch == EOF) {
    pthread_mutex_lock(&bufferlock);

    if (ch != EOF) {
      if ((head - tail) >= BUFFERSIZE) {
        throw StdoutBufferOverflowException();
      }
      buf[head++ & BUFFERMASK] = ch;
    }

    if (waitingforoutput == true) {
      // signal the output runner to wake up
      pthread_cond_signal(&outputcond);
      waitingforoutput = false;
    }

    pthread_mutex_unlock(&bufferlock);
  }
}

void Buffer::fill()  // throws StdinReadException
{
  char c;

  timeval tmo;
  fd_set inset;

  tmo.tv_sec = 1;
  tmo.tv_usec = 0;

  FD_ZERO(&inset);
  FD_SET(fileHdl, &inset);

  // Look to see if there's something to read from stdin
  int sel = select(fileHdl + 1, &inset, NULL, NULL, &tmo);

  if (sel == -1) {
    printf("terminal select error %d\n", fileHdl);
    throw StdinReadException();
  } else if (sel) {
    // there is some data, let remaining code read and process it
  } else {
    // timed out, just return and the main loop will check to see
    // if we should still be running...
    return;
  }

  int nb = read(fileHdl, &c, 1);
  if (nb <= 0) {
    printf("treminal read error\n");
    throw StdinReadException();
  }

  pthread_mutex_lock(&bufferlock);

  int hd = head & BUFFERMASK;

  // Put the data into the out buffer
  memcpy(&buf[hd], &c, 1);
  head += 1;

  if (waitingforinput == true) {
    // signal the reader
    pthread_cond_signal(&inputcond);
    waitingforinput = false;
  }

  if (head - tail >= BUFFERSIZE) {
    printf("treminal buffer overflow error\n");
    throw StdinReadException();
  }

  pthread_mutex_unlock(&bufferlock);
}

void Buffer::empty()  // throws StdoutWriteException
{
  pthread_mutex_lock(&bufferlock);
  if (head - tail > 0) {
    while ((head - tail) > 0) {
      int hd = head & BUFFERMASK;
      int tl = tail & BUFFERMASK;
      int nb = write(fileHdl, &buf[tl], (hd > tl) ? hd - tl : BUFFERSIZE - tl);

      // Replace with this for no actual output
      // int nb = (hd > tl) ? hd -tl : BUFFERSIZE -tl;

      if (nb <= 0)
        throw StdoutWriteException();

      tail += nb;
    }
  } else {
    // There is nothing to send to stdout so we should block until there is
    waitingforoutput = true;
    pthread_cond_wait(&outputcond, &bufferlock);
  }
  pthread_mutex_unlock(&bufferlock);
}
