#ifndef DSP_H_INCLUDED
#define DSP_H_INCLUDED

#include <pthread.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

#define FALSE 0
#define TRUE 1

class Dsp : public SamplingDevice {
 public:
  snd_pcm_t* playback_handle;
  snd_pcm_t* capture_handle;
  int buffer_size;
  int bits;
  int bytes_per_frame;
  int num_channels;
  unsigned int fragments;

  int dspOutBufSize;
  int dspInBufSize;

  unsigned char** dspOutBuf;
  int dspOutBufPos;
  int dspOutBufIndex;
  int dspOutReady[2];

  unsigned char** dspInBuf;
  int dspInBufPos;
  int dspInBufIndex;
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
