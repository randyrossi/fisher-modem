#ifndef BLUETOOTHSAMPLING_H_INCLUDED
#define BLUETOOTHSAMPLING_H_INCLUDED

#include <pthread.h>

#define FALSE 0
#define TRUE 1

class BluetoothDevice : public SamplingDevice {
 public:
  int rfd;
  int scoind;
  int scooutd;
  int mtu;

  // File in/out
  int scoin_rec_fd;
  int scoout_rec_fd;
  int scoout_play_fd;

  int hookState;  // 0 off the hook, 1 on the hook

  int outBufSize;
  int inBufSize;

  unsigned char** outBuf;
  int outBufPos;
  int outBufIndex;
  int outReady[2];

  unsigned char** inBuf;
  int inBufPos;
  int inBufIndex;
  int inReady[2];

  pthread_attr_t rfcattr;
  pthread_t rfcthreadid;

  pthread_attr_t inputattr;
  pthread_t inputthreadid;

  pthread_attr_t outputattr;
  pthread_t outputthreadid;

  pthread_mutex_t inputlock;
  pthread_mutex_t outputlock;

  pthread_cond_t inputcond;
  pthread_cond_t outputcond;

  int rfcommend;
  int inputend;
  int outputend;

  bool throttle;
  int incounter;   // for throttling
  int outcounter;  // for throttling

  char* bluetoothAddress;
  int bluetoothChannel;

  BluetoothDevice(char* bluetoothAddress, int bluetoothChannel, bool throttle);
  virtual ~BluetoothDevice();
  int dopen();
  void dclose();
  void flush();
  void discardInput();
  void discardOutput();
  void setduplex(int n);
  int isOpen();
  void putByte(unsigned char b);
  unsigned char getByte();

  void onHook();
  void offHook();

  float insample();
  void outsample(float);
};

#endif
