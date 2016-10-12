#ifndef SAMPLING_DEVICE_H
#define SAMPLING_DEVICE_H

#include "commonTypes.h"

class SamplingDevice {
 public:
  static int DEV_IN_PLAY_FROM_FILE;
  static int DEV_IN_DEVICE;
  static int DEV_IN_RECORD;

  static int DEV_OUT_PLAY_FROM_FILE;
  static int DEV_OUT_DEVICE;
  static int DEV_OUT_RECORD;

  int format;
  int samplecount;
  int devInMode;
  int devOutMode;

  enum SamplerType {
    SoundCard,         // what we usually want
    SharedMemoryPipe,  // for testing purposes
    Bluetooth,
    AudibleBluetooth,
  };

  enum Format {
    SIGNED_8BIT_PCM,  // for dsp 8 bit
    SIGNED_16BIT_LE_PCM,
    // for dsp 16 bit or sco
  };

  SamplingDevice(int formatp);
  virtual ~SamplingDevice();
  virtual int dopen() = 0;
  virtual void dclose() = 0;
  virtual void flush() = 0;
  virtual void discardInput() = 0;
  virtual void discardOutput() = 0;
  virtual void setduplex(int n) = 0;

  virtual void offHook() = 0;
  virtual void onHook() = 0;

  virtual float insample() = 0;
  virtual void outsample(float) = 0;

  void setDevInMode(int v);
  void setDevOutMode(int v);
};

#endif
