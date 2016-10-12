#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sco.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include "commonTypes.h"
#include "SamplingDevice.h"
#include "bt.h"

#define OUTPUT_WARN 1
#define INPUT_WARN 1

// This code is a mess.
// There are a number of problems with this implementation.  State
// variables like hookState, rfcommend, and rfd are not protected
// from concurrent access by multiple threads.  Race conditions might cause
// some logic to be executed multiple times when they should only be
// executed once.  (Especially on shutdown or detection of dropped connections)
// However, I don't think anything catastrophic will happen.
//
// Also, the write thread must be throttled so it doesn't
// overrun the bluez buffer.  I'm not sure why bluez write op doesn't block
// like every other socket type.  Read is of course paced properly since
// audio data can't possibly be produced faster than real time.  But the write
// side is a different story.  Might want to try select on the write to see
// if it will tell us that sometimes we have to wait to write?  Two threads
// reading/writing on the same socket seems like a bad idea too.  Perhaps
// the read/write threads can be merged into one that alternate.  However,
// that might block the byte producer thread calling into us more often.

void btrfcreader(void* data);
void btscooutputrunner(void* data);
void btscoinputrunner(void* data);

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ABS(a) (((a) < 0) ? -(a) : (a))

static uint64_t currenttimemillis() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000.0;
}

static int sco_connect(char* svr, int* mtu) {
  bdaddr_t ANYADDR;
  ANYADDR.b[0] = 0;
  ANYADDR.b[1] = 0;
  ANYADDR.b[2] = 0;
  ANYADDR.b[3] = 0;
  ANYADDR.b[4] = 0;
  ANYADDR.b[5] = 0;

  struct sockaddr_sco addr;
  struct sco_conninfo conn;
  struct sco_options options;
  socklen_t optlen;
  int sk;

  /* Create socket */
  sk = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO);
  if (sk < 0) {
    printf("Can't create socket: %s (%d)", strerror(errno), errno);
    return -1;
  }

  /* Bind to local address */
  memset(&addr, 0, sizeof(addr));
  addr.sco_family = AF_BLUETOOTH;
  bacpy(&addr.sco_bdaddr, &ANYADDR);

  if (bind(sk, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("Can't bind socket: %s (%d)", strerror(errno), errno);
    goto error;
  }

  /* Connect to remote device */
  memset(&addr, 0, sizeof(addr));
  addr.sco_family = AF_BLUETOOTH;
  str2ba(svr, &addr.sco_bdaddr);

  if (connect(sk, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("Can't connect: %s (%d)", strerror(errno), errno);
    goto error;
  }
  /* Get connection information */
  memset(&conn, 0, sizeof(conn));
  optlen = sizeof(conn);

  if (getsockopt(sk, SOL_SCO, SCO_CONNINFO, &conn, &optlen) < 0) {
    printf("Can't get SCO connection information: %s (%d)", strerror(errno),
           errno);
    goto error;
  }

  printf("Connected [handle %d, class 0x%02x%02x%02x]\n", conn.hci_handle,
         conn.dev_class[2], conn.dev_class[1], conn.dev_class[0]);

  /* Get connection information */
  memset(&options, 0, sizeof(options));
  optlen = sizeof(options);
  if (getsockopt(sk, SOL_SCO, SCO_OPTIONS, &options, &optlen) < 0) {
    printf("Can't get SCO options information: %s (%d)", strerror(errno),
           errno);
    goto error;
  }

  printf("Sco mtu %d\n", options.mtu);
  *mtu = options.mtu;

  // The stack lies.  Override with 48 byte mtu.
  *mtu = 48;

  return sk;

error:
  close(sk);
  return -1;
}

static int rfcomm_connect(bdaddr_t* dst, uint8_t channel) {
  bdaddr_t ANYADDR;
  ANYADDR.b[0] = 0;
  ANYADDR.b[1] = 0;
  ANYADDR.b[2] = 0;
  ANYADDR.b[3] = 0;
  ANYADDR.b[4] = 0;
  ANYADDR.b[5] = 0;

  struct sockaddr_rc addr;
  int s;

  if ((s = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)) < 0) {
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.rc_family = AF_BLUETOOTH;
  bacpy(&addr.rc_bdaddr, &ANYADDR);
  addr.rc_channel = 0;
  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(s);
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.rc_family = AF_BLUETOOTH;
  bacpy(&addr.rc_bdaddr, dst);
  addr.rc_channel = channel;

  if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(s);
    return -1;
  }

  return s;
}

BluetoothDevice::BluetoothDevice(char* addr, int channel, bool t)
    : SamplingDevice(SIGNED_16BIT_LE_PCM),
      incounter(0),
      outcounter(0),
      throttle(t),
      bluetoothAddress(addr),
      bluetoothChannel(channel),
      inBuf(NULL),
      inBufIndex(0),
      inBufPos(0),
      inBufSize(0),
      inputend(0),
      inputthreadid(0),
      outBuf(NULL),
      outBufIndex(0),
      outBufPos(0),
      outBufSize(0),
      outputend(0),
      outputthreadid(0),
      scoout_rec_fd(0),
      scoout_play_fd(0),
      scooutd(0),
      scoin_rec_fd(0),
      scoind(0),
      rfcthreadid(0),
      mtu(0),
      rfcommend(0),
      hookState(1),  // onhook
      rfd(0) {}

BluetoothDevice::~BluetoothDevice() {}

int BluetoothDevice::dopen() {
  incounter = 0;
  outcounter = 0;
  scoind = 0;
  scooutd = 0;
  scoout_rec_fd = 0;
  scoin_rec_fd = 0;
  scoout_play_fd = 0;
  rfd = 0;

  // rfcomm connection remains open as long as the device is active.
  // sco connections come and go

  bdaddr_t bdaddr;
  str2ba(bluetoothAddress, &bdaddr);

  if (devOutMode == DEV_OUT_DEVICE || devOutMode == DEV_OUT_RECORD ||
      devInMode == DEV_IN_DEVICE || devInMode == DEV_IN_RECORD) {
    printf("open rfcomm\r\n");
    rfd = rfcomm_connect(&bdaddr, bluetoothChannel);

    rfcommend = 0;
    // Spawn a thread that will read from the rfcomm channel
    pthread_attr_init(&rfcattr);
    pthread_attr_setdetachstate(&rfcattr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&rfcthreadid, &rfcattr, (void* (*)(void*))btrfcreader, this);
  }

  hookState = 1;

  outBufSize = 1000;
  inBufSize = 1000;

  outBuf = (unsigned char**)malloc(sizeof(unsigned char*) * 2);
  outBuf[0] = (unsigned char*)malloc(outBufSize);
  outBuf[1] = (unsigned char*)malloc(outBufSize);
  memset(outBuf[0], 0, outBufSize);
  memset(outBuf[1], 0, outBufSize);
  inBuf = (unsigned char**)malloc(sizeof(unsigned char*) * 2);
  inBuf[0] = (unsigned char*)malloc(inBufSize);
  inBuf[1] = (unsigned char*)malloc(inBufSize);
  memset(inBuf[0], 0, inBufSize);
  memset(inBuf[1], 0, inBufSize);
  if (outBuf[0] == NULL || outBuf[0] == NULL || inBuf[0] == NULL ||
      inBuf[1] == NULL) {
    fprintf(stderr, "can't allocate bluetooth buffers\n");
    return 7;
  }

  pthread_mutex_init(&inputlock, NULL);
  pthread_mutex_init(&outputlock, NULL);
  pthread_cond_init(&inputcond, NULL);
  pthread_cond_init(&outputcond, NULL);

  return 1;
}

void BluetoothDevice::dclose() {
  // This will go onhook.
  rfcommend = 1;

  // Wait for rfcomm reader thread to end.
  // Also means i/o runners have exited too.
  pthread_join(rfcthreadid, NULL);

  pthread_mutex_destroy(&inputlock);
  pthread_mutex_destroy(&outputlock);
  pthread_cond_destroy(&inputcond);
  pthread_cond_destroy(&outputcond);

  // Now free up all the memory and close handles
  free(outBuf[0]);
  free(outBuf[1]);
  free(outBuf);
  free(inBuf[0]);
  free(inBuf[1]);
  free(inBuf);
}

int BluetoothDevice::isOpen() {
  return (rfd > 0);
}

// Must be called while holding output lock
void BluetoothDevice::putByte(unsigned char b) {
  if (hookState == 0 && outputend == 0) {
    // sco is active, write to outbuf
    outBuf[outBufIndex][outBufPos] = b;
    outBufPos++;
    if (outBufPos >= outBufSize) {
      // We've filled the buffer, wait until the output thread tells us
      // we should start filling the next one
      outReady[outBufIndex] = 1;
      while (outBufPos >= outBufSize) {
        int ret = pthread_cond_wait(&outputcond, &outputlock);
        assert(ret == 0);
      }
      assert(outBufPos == 0);
    }
  } else {
    outBufPos++;
    outcounter++;
    if (outBufPos >= inBufSize) {
      outBufPos = 0;
    }
    if (outcounter >= outBufSize) {
      // Fake the delay that would come if we were actually writing data to
      // something.
      float delay_us = (float)(outBufSize) / 8000.00 * 1000000;
      uint32_t delay = (uint32_t)delay_us;
      usleep(delay);
      outcounter = 0;
    }
  }
}

// Must be called while holding inputlock
unsigned char BluetoothDevice::getByte() {
  unsigned char returnByte = 0;

  if (hookState == 0 && inputend == 0) {
    // sco is active, read from inbuf
    returnByte = inBuf[inBufIndex][inBufPos];
    inBufPos++;
    if (inBufPos >= inBufSize) {
      // We've reached the end of the buffer, wait until the input thread
      // tells us the next one is ready for processing
      inReady[1 - inBufIndex] = 1;
      while (inBufPos >= inBufSize) {
        int ret = pthread_cond_wait(&inputcond, &inputlock);
        assert(ret == 0);
      }
      assert(inBufPos == 0);
    }
  } else {
    returnByte = 0;
    inBufPos++;
    incounter++;
    if (inBufPos >= inBufSize) {
      inBufPos = 0;
    }
    if (incounter >= inBufSize) {
      // Fake the delay that would come if we were actually writing data to
      // something.
      float delay_us = (float)(inBufSize) / 8000.00 * 1000000;
      uint32_t delay = (uint32_t)delay_us;
      usleep(delay);
      incounter = 0;
    }
  }

  return returnByte;
}

// This will cause silence which is not what caller wants...fix this
void BluetoothDevice::flush() {
  pthread_mutex_lock(&outputlock);
  if (outBufPos != 0) {
    int bufIndex = outBufIndex;
    // Put 0 until we cross a buffer boundary
    while (bufIndex == outBufIndex) {
      putByte(0);
    }
  }
  pthread_mutex_unlock(&outputlock);
}

void BluetoothDevice::discardInput() {
  pthread_mutex_lock(&inputlock);
  if (hookState == 0 && inputend == 0) {
    // We've reached the end of the buffer, wait until the input thread
    // tells us the next one is ready for processing
    inReady[1 - inBufIndex] = 1;
    inBufPos = inBufSize;
    pthread_cond_wait(&inputcond, &inputlock);
  } else {
    inBufPos = 0;
  }
  pthread_mutex_unlock(&inputlock);
}

void BluetoothDevice::discardOutput() {
  // Just reset the position to 0 as though we never put anything
  // there in the first place
  pthread_mutex_lock(&outputlock);
  outBufPos = 0;
  pthread_mutex_unlock(&outputlock);
}

void BluetoothDevice::setduplex(int n) {}

void BluetoothDevice::offHook() {
  if (scoind == 0 && scooutd == 0) {
    scoout_rec_fd = 0;
    scoin_rec_fd = 0;
    scoout_play_fd = 0;

    if (devOutMode == DEV_OUT_DEVICE || devOutMode == DEV_OUT_RECORD ||
        devInMode == DEV_IN_DEVICE || devInMode == DEV_IN_RECORD) {
      // Need a real sco channel
      scoind = scooutd = sco_connect(bluetoothAddress, &mtu);
    } else {
      // Use a bigger mtu when all endpoints are files.
      mtu = 1024;
    }

    if (devInMode == DEV_IN_PLAY_FROM_FILE) {
      scoind = open("sco_in", O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }

    if (devInMode == DEV_IN_RECORD) {
      scoin_rec_fd = open("sco_in_rec", O_WRONLY | O_CREAT,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }

    if (devOutMode == DEV_OUT_PLAY_FROM_FILE) {
      scoout_play_fd =
          open("sco_out", O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }

    if (devOutMode == DEV_OUT_RECORD) {
      scoout_rec_fd = open("sco_out_rec", O_WRONLY | O_CREAT,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }

    pthread_mutex_lock(&outputlock);
    pthread_mutex_lock(&inputlock);

    outBufPos = 0;
    outBufIndex = 0;  // producing to bank 0
    outReady[0] = 0;
    outReady[1] = 1;  // bank 1 production done
    outputend = 0;
    // Spawn a thread that will continuously write to sco
    pthread_attr_init(&outputattr);
    pthread_attr_setdetachstate(&outputattr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&outputthreadid, &outputattr,
                   (void* (*)(void*))btscooutputrunner, this);

    inBufPos = 0;
    inBufIndex = 1;  // consuming from bank 1
    inReady[0] = 0;  // consumer not ready to read from here yet
    inReady[1] = 0;
    inputend = 0;
    // Spawn a thread that will continuously read from sco
    pthread_attr_init(&inputattr);
    pthread_attr_setdetachstate(&inputattr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&inputthreadid, &inputattr,
                   (void* (*)(void*))btscoinputrunner, this);

    pthread_mutex_unlock(&inputlock);
    pthread_mutex_unlock(&outputlock);

    hookState = 0;

    if (rfd != 0) {
      // HSP_SPEC_V12 - 4.3 - Our user initiated action is the off hook event.
      // We send AT+CKPD=200.
      // This usually translates to the phone either dialing the last number or
      // answering depending
      // on whether there is an incoming call or not.  What the phone does is
      // implementation dependent.
      write(rfd, "AT+CKPD=200\r", 12);  // spec explicitly states no LF
      usleep(250000);
      // TODO - WAIT for OK instead of delay
    }
  }
}

void BluetoothDevice::onHook() {
  if (hookState == 0) {
    if (scoind != 0 && scooutd != 0) {
      if (rfd != 0) {
        // HSP_SPEC_V12 - 4.5 - Our user initiated action is the on hook event.
        // We send AT+CKPD=200.
        // Wait for OK and then release our sco connections.
        // write(rfd,"AT+CKPD=200\r",12); // spec explicitly states no LF
        // usleep(250000);
        // TODO - WAIT for OK instead of delay
      }

      // Tell the input/output threads to die
      pthread_mutex_lock(&outputlock);
      outputend = 1;
      pthread_mutex_unlock(&outputlock);

      pthread_mutex_lock(&inputlock);
      inputend = 1;
      pthread_mutex_unlock(&inputlock);

      // Now wait for those threads to exit
      pthread_join(inputthreadid, NULL);
      pthread_join(outputthreadid, NULL);

      pthread_attr_destroy(&outputattr);
      pthread_attr_destroy(&inputattr);

      close(scoind);
      if (scoind != scooutd) {
        close(scooutd);
      }
      scoind = 0;
      scooutd = 0;
    }
    // TODO - Sampling devices need a way to force change the carrier state of
    // the modem.
    // i.e. automatically hang up and put the modem back into hayes command mode
    hookState = 1;
  }
}

float BluetoothDevice::insample() {
  float returnSample = 0;

  pthread_mutex_lock(&inputlock);
  if (format == SIGNED_16BIT_LE_PCM) {
    // This works for sco
    int b1 = getByte();
    int b2 = getByte();

    signed short t = b2 << 8 | b1;

    returnSample = (float)t / 16384.0f;
    samplecount++;
  } else {
    printf("UNKNOWN FORMAT!!!\n");
  }
  pthread_mutex_unlock(&inputlock);

  return returnSample;
}

void BluetoothDevice::outsample(float x) {
  // Range must be between -2.0 and +2.0
  if (x > 2.0f)
    x = 2.0f;
  if (x < -2.0f)
    x = -2.0f;

  pthread_mutex_lock(&outputlock);

  if (format == SIGNED_16BIT_LE_PCM) {
    // This works for sco
    float y = x * 16384.0f;
    signed short q = (short)y;

    putByte((unsigned char)(q & 0xFF));
    putByte((unsigned char)((q >> 8) & 0xFF));
  } else {
    printf("UNKNOWN FORMAT!!!\n");
  }

  pthread_mutex_unlock(&outputlock);
}

// When sco is connected, responsible for consuming from the outbuf and
// writing the data to the sco channel
void btscooutputrunner(void* data) {
  BluetoothDevice* blue = (BluetoothDevice*)data;

  int pos, remaining;

  // For throttling
  uint64_t start;
  uint64_t elapsed;
  float total = 0;
  float seconds;
  float targetRate = (float)8000 * 2.0f;  // *2 due to 16 bit output
  float rate;
  int bufWriteCount = 0;

  start = currenttimemillis();

  // This loop never blocks except on the sound card device itself
  // It continuously alternates flushing the contents of buffers 0 and 1
  // to the sound card, detecting if the producer was too slow to fill
  // a buffer its just about to flush
  while (blue->outputend == 0) {
    // Tell producer to start filling buffer 0
    // as we are about to output buffer 1
    pthread_mutex_lock(&blue->outputlock);
    if (blue->outReady[1] == 0) {
#ifdef OUTPUT_WARN
      fprintf(stderr, "too slow producing to btbuf 1 at %d\n", blue->outBufPos);
#endif
      // The producer did not fill this buffer in time, clear the remaining
      // portion and reset buf index to 1 as though they did finish
      // Sound card will get blank audio or 'pops' when this happens
      memset(&(blue->outBuf[1][blue->outBufPos]), 0,
             blue->outBufSize - blue->outBufPos);
    } else {
      pthread_cond_signal(&blue->outputcond);
    }
    blue->outBufPos = 0;
    blue->outBufIndex = 0;
    blue->outReady[1] = 0;
    pthread_mutex_unlock(&blue->outputlock);

    remaining = blue->outBufSize;
    if (blue->scooutd != 0) {
      pos = 0;
      if (blue->scoout_play_fd != 0) {
        // Clobber whatever was put in the outbuf with file contents
        read(blue->scoout_play_fd, blue->outBuf[1], blue->outBufSize);
      }
      while (remaining > 0) {
        int n = write(blue->scooutd, &(blue->outBuf[1][pos]),
                      MIN(blue->mtu, remaining));
        if (n <= 0) {
          blue->outputend = 1;
          break;
        }
        if (blue->scoout_rec_fd != 0) {
          write(blue->scoout_rec_fd, &(blue->outBuf[1][pos]), n);
        }
        remaining -= n;
        pos += n;
      }
    }

    if (blue->throttle) {
      total += (float)blue->outBufSize;
      elapsed = currenttimemillis() - start;
      seconds = (float)(elapsed) / 1000.0f;
      rate = total / seconds;
      while (rate > targetRate) {
        usleep(100);
        elapsed = currenttimemillis() - start;
        seconds = (float)(elapsed) / 1000.0f;
        rate = total / seconds;
      }
      bufWriteCount++;
      if (bufWriteCount > 10) {
        // printf ("RATE=%f elapsed=%lld\n",rate,elapsed);
        bufWriteCount = 0;
        total = 0;
        start = currenttimemillis();
      }
    }

    if (blue->outputend == 1) {
      break;
    }

    // Tell producer to start filling buffer 1
    // as we are about to output buffer 0
    pthread_mutex_lock(&blue->outputlock);
    if (blue->outReady[0] == 0) {
#ifdef OUTPUT_WARN
      fprintf(stderr, "too slow producing to btbuf 0 at %d\n", blue->outBufPos);
#endif
      // The producer did not fill this buffer in time, clear the remaining
      // portion and reset buf index to 0 as though they did finish
      // Sound card will get blank audio or 'pops' when this happens
      memset(&(blue->outBuf[0][blue->outBufPos]), 0,
             blue->outBufSize - blue->outBufPos);
    } else {
      pthread_cond_signal(&blue->outputcond);
    }
    blue->outBufPos = 0;
    blue->outBufIndex = 1;
    blue->outReady[0] = 0;
    pthread_mutex_unlock(&blue->outputlock);

    remaining = blue->outBufSize;
    if (blue->scooutd != 0) {
      pos = 0;
      if (blue->scoout_play_fd != 0) {
        // Clobber whatever was put in the outbuf with file contents
        read(blue->scoout_play_fd, blue->outBuf[0], blue->outBufSize);
      }
      while (remaining > 0) {
        int n = write(blue->scooutd, &(blue->outBuf[0][pos]),
                      MIN(blue->mtu, remaining));
        if (n <= 0) {
          blue->outputend = 1;
          break;
        }
        if (blue->scoout_rec_fd != 0) {
          write(blue->scoout_rec_fd, &(blue->outBuf[0][pos]), n);
        }
        remaining -= n;
        pos += n;
      }
    }

    if (blue->throttle) {
      total += (float)blue->outBufSize;
      elapsed = currenttimemillis() - start;
      seconds = (float)(elapsed) / 1000.0f;
      rate = total / seconds;
      while (rate > targetRate) {
        usleep(100);
        elapsed = currenttimemillis() - start;
        seconds = (float)(elapsed) / 1000.0f;
        rate = total / seconds;
      }
      bufWriteCount++;
      if (bufWriteCount > 10) {
        // printf ("RATE=%f elapsed=%lld\n",rate,elapsed);
        bufWriteCount = 0;
        total = 0;
        start = currenttimemillis();
      }
    }

    if (blue->outputend == 1) {
      break;
    }
  }

  // Make sure producer won't block or wakes up.
  pthread_mutex_lock(&blue->outputlock);
  blue->outputend = 1;
  if (blue->outBufPos >= blue->outBufSize) {
    pthread_cond_signal(&blue->outputcond);
    blue->outBufPos = 0;
  }
  pthread_mutex_unlock(&blue->outputlock);

  pthread_mutex_lock(&blue->inputlock);
  blue->inputend = 1;
  pthread_mutex_unlock(&blue->inputlock);

  printf("sco output is DEAD\n");
  pthread_exit(NULL);
}

// When sco is connected, responsible for reading from the sco channel
// producing data into inbuf
void btscoinputrunner(void* data) {
  BluetoothDevice* blue = (BluetoothDevice*)data;
  int pos, remaining;
  fd_set rfds;
  struct timeval tv;
  int retval;

  tv.tv_sec = 0;
  tv.tv_usec = 200000;

  while (blue->inputend == 0) {
    remaining = blue->inBufSize;
    if (blue->scoind != 0) {
      pos = 0;
      while (remaining > 0 && blue->inputend == 0) {
        FD_ZERO(&rfds);
        FD_SET(blue->scoind, &rfds);
        retval = select(blue->scoind + 1, &rfds, NULL, NULL, &tv);
        if (retval == -1) {
          printf("inputreader select error\n");
          blue->inputend = 1;
          break;
        } else if (retval) {
          int n = read(blue->scoind, &(blue->inBuf[0][pos]), remaining);
          if (n <= 0) {
            blue->inputend = 1;
            break;
          }
          if (blue->scoin_rec_fd != 0) {
            write(blue->scoin_rec_fd, &(blue->inBuf[0][pos]), n);
          }
          remaining -= n;
          pos += n;
        }
      }
    } else {
      memset(&(blue->inBuf[0][0]), 0, remaining);
    }

    if (blue->inputend == 1) {
      break;
    }

    // Fake read delay
    if (blue->devInMode == SamplingDevice::DEV_IN_PLAY_FROM_FILE) {
      usleep(500 * 1000);
    }

    // Tell consumer its okay to start reading from buffer 0 we just filled
    // as we area about to start filling buffer 1
    pthread_mutex_lock(&blue->inputlock);

    if (blue->inReady[0] == 0) {
#ifdef INPUT_WARN
      fprintf(stderr, "too slow consuming from btbuf 1 at %d\n",
              blue->inBufPos);
#endif
      // Consumer will have 'gaps' in its data when this happens
    } else {
      pthread_cond_signal(&blue->inputcond);
    }
    blue->inBufPos = 0;
    blue->inBufIndex = 0;
    blue->inReady[0] = 0;

    pthread_mutex_unlock(&blue->inputlock);

    remaining = blue->inBufSize;
    if (blue->scoind != 0) {
      pos = 0;
      while (remaining > 0 && blue->inputend == 0) {
        FD_ZERO(&rfds);
        FD_SET(blue->scoind, &rfds);
        retval = select(blue->scoind + 1, &rfds, NULL, NULL, &tv);
        if (retval == -1) {
          printf("inputreader select error\n");
          blue->inputend = 1;
          break;
        } else if (retval) {
          int n = read(blue->scoind, &(blue->inBuf[1][pos]), remaining);
          if (n <= 0) {
            blue->inputend = 1;
            break;
          }
          if (blue->scoin_rec_fd != 0) {
            write(blue->scoin_rec_fd, &(blue->inBuf[1][pos]), n);
          }
          remaining -= n;
          pos += n;
        }
      }
    } else {
      memset(&(blue->inBuf[1][0]), 0, remaining);
    }

    if (blue->inputend == 1) {
      break;
    }

    // Fake read delay
    if (blue->devInMode == SamplingDevice::DEV_IN_PLAY_FROM_FILE) {
      usleep(500 * 1000);
    }
    // Tell consumer its okay to start reading from buffer 1 we just filled
    // as we are about to start filling buffer 0
    pthread_mutex_lock(&blue->inputlock);

    if (blue->inReady[1] == 0) {
#ifdef INPUT_WARN
      fprintf(stderr, "too slow consuming from btbuf 0 at %d\n",
              blue->inBufPos);
#endif
      // Consumer will have 'gaps' in its data when this happens
    } else {
      pthread_cond_signal(&blue->inputcond);
    }
    blue->inBufPos = 0;
    blue->inBufIndex = 1;
    blue->inReady[1] = 0;
    pthread_mutex_unlock(&blue->inputlock);
  }

  // Make sure consumer won't block or wakes up.
  pthread_mutex_lock(&blue->inputlock);
  blue->inputend = 1;
  if (blue->inBufPos >= blue->inBufSize) {
    pthread_cond_signal(&blue->inputcond);
    blue->inBufPos = 0;
  }
  pthread_mutex_unlock(&blue->inputlock);

  pthread_mutex_lock(&blue->outputlock);
  blue->outputend = 1;
  pthread_mutex_unlock(&blue->outputlock);

  printf("sco input is DEAD\r\n");

  // When input dies, go on hook. No need to do this in output
  // runner since if it dies we will get here anyway.
  blue->onHook();

  pthread_exit(NULL);
}

// when rfcomm is connected, responsible for listening for reading
// responses or unsolicited rings
void btrfcreader(void* data) {
  BluetoothDevice* bt = (BluetoothDevice*)data;
  char buf[1024];
  fd_set rfds;
  struct timeval tv;
  int retval;

  tv.tv_sec = 0;
  tv.tv_usec = 200000;

  while (bt->rfcommend == 0) {
    memset(buf, 0, sizeof(buf));
    FD_ZERO(&rfds);
    FD_SET(bt->rfd, &rfds);
    retval = select(bt->rfd + 1, &rfds, NULL, NULL, &tv);
    if (retval == -1) {
      printf("RFCOMM: select error...bail\n");
      break;
    } else if (retval) {
      int rlen = read(bt->rfd, buf, sizeof(buf));
      if (rlen < 0) {
        printf("RFCOMM: read error...bail\n");
        break;
      }
      // scrub it
      for (int i = 0; i < rlen; i++) {
        if (buf[i] == '\r')
          buf[i] = 'r';
        else if (buf[i] == '\n')
          buf[i] = 'n';
      }
      printf("RFCOMM: %s\r\n", buf);
    }
  }

  bt->rfcommend = 1;
  close(bt->rfd);
  bt->rfd = 0;

  printf("rfcomm link dead\n");
  bt->onHook();
}
