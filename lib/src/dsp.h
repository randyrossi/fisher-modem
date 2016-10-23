#ifndef DSP_H_INCLUDED
#define DSP_H_INCLUDED

#include <pthread.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

#include "commonTypes.h"
#include "SamplingDevice.h"

#define FALSE 0
#define TRUE 1

class Dsp : public SamplingDevice {
 public:
  snd_pcm_t* playback_handle;
  snd_pcm_t* capture_handle;
  unsigned int buffer_size;
  unsigned int bits;
  unsigned int bytes_per_frame;
  unsigned int num_channels;
  unsigned int fragments;

  unsigned int dspOutBufSize;
  unsigned char** dspOutBuf;
  unsigned int dspOutBufPos;
  unsigned int dspOutBufIndex;
  int dspOutReady[2];

  unsigned int dspInBufSize;
  unsigned char** dspInBuf;
  unsigned int dspInBufPos;
  unsigned int dspInBufIndex;
  int dspInReady[2];

  pthread_mutex_t inputlock;
  pthread_mutex_t outputlock;

  pthread_cond_t inputcond;
  pthread_cond_t outputcond;

  pthread_attr_t inputattr;
  pthread_attr_t outputattr;

  pthread_t inputthreadid;
  pthread_t outputthreadid;

  int inputend;
  int outputend;

  Dsp(Format format);
  virtual ~Dsp();

  int dopen();
  int configure_alsa_audio(snd_pcm_t* device);

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
