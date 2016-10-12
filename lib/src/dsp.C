#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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

#include "commonTypes.h"
#include "SamplingDevice.h"
#include "dsp.h"

#define OUTPUT_WARN
#define INPUT_WARN
//#define PLAY_RECORDING 1
//#define RECORD_INPUT
//#define RECORD_OUTPUT 1

void soundoutputrunner(void* data);
void soundinputrunner(void* data);

int Dsp::configure_alsa_audio(snd_pcm_t* device) {
  snd_pcm_hw_params_t* hw_params;
  int err;
  unsigned int tmp;
  snd_pcm_uframes_t frames;

  /* allocate memory for hardware parameter structure */
  if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
    fprintf(stderr, "cannot allocate parameter structure (%s)\n",
            snd_strerror(err));
    return -1;
  }
  /* fill structure from current audio parameters */
  if ((err = snd_pcm_hw_params_any(device, hw_params)) < 0) {
    fprintf(stderr, "cannot initialize parameter structure (%s)\n",
            snd_strerror(err));
    return -1;
  }

  /* set access type, sample rate, sample format, channels */
  if ((err = snd_pcm_hw_params_set_access(device, hw_params,
                                          SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    fprintf(stderr, "cannot set access type: %s\n", snd_strerror(err));
    return -1;
  }
  // TODO - support 8 bit here too - check format
  snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
  if (bits == 8) {
    format = SND_PCM_FORMAT_U8;
  }
  if ((err = snd_pcm_hw_params_set_format(device, hw_params, format)) < 0) {
    fprintf(stderr, "cannot set sample format: %s\n", snd_strerror(err));
    return -1;
  }

  tmp = SAMPLERATE;
  if ((err = snd_pcm_hw_params_set_rate_near(device, hw_params, &tmp, 0)) < 0) {
    fprintf(stderr, "cannot set sample rate: %s\n", snd_strerror(err));
    return 1;
  }

  if (tmp != SAMPLERATE) {
    fprintf(stderr,
            "Could not set requested sample rate, asked for %d got %d\n",
            SAMPLERATE, tmp);
    return -1;
  }

  if ((err = snd_pcm_hw_params_set_channels(device, hw_params, num_channels)) <
      0) {
    fprintf(stderr, "cannot set channel count: %s\n", snd_strerror(err));
    return -1;
  }

  if ((err = snd_pcm_hw_params_set_periods_near(device, hw_params, &fragments,
                                                0)) < 0) {
    fprintf(stderr, "Error setting # fragments to %d: %s\n", fragments,
            snd_strerror(err));
    return -1;
  }

  frames = buffer_size / bytes_per_frame * fragments;
  if ((err = snd_pcm_hw_params_set_buffer_size_near(device, hw_params,
                                                    &frames)) < 0) {
    fprintf(stderr, "Error setting buffer_size %d frames: %s\n", frames,
            snd_strerror(err));
    return -1;
  }

  if (buffer_size != frames * bytes_per_frame / fragments) {
    fprintf(stderr,
            "Could not set requested buffer size, asked for %d got %d\n",
            buffer_size, frames * bytes_per_frame / fragments);
    buffer_size = frames * bytes_per_frame / fragments;
  }

  if ((err = snd_pcm_hw_params(device, hw_params)) < 0) {
    fprintf(stderr, "Error setting HW params: %s\n", snd_strerror(err));
    return -1;
  }

  snd_pcm_hw_params_free(hw_params);

  printf("BUFFER SIZE = %d\n", buffer_size);
  printf("FRAMES = %d\n", frames);
  printf("FRAGMENTS = %d\n", fragments);
  return 0;
}

static uint64_t currenttimemillis() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000.0;
}

Dsp::Dsp(Format f)
    : SamplingDevice(f),
      num_channels(1),
      fragments(2),
      playback_handle(NULL),
      capture_handle(NULL),
      buffer_size(1024),
      outputthreadid(NULL),
      inputthreadid(NULL),
      dspInBufSize(0),
      dspOutBufSize(0),
      inputend(0),
      outputend(0),
      dspInBufPos(0),
      dspOutBufPos(0),
      dspInBuf(NULL),
      dspOutBuf(NULL),
      dspInBufIndex(1),
      dspOutBufIndex(0) {
  if (format == SIGNED_8BIT_PCM) {
    bits = 8;
  } else if (format == SIGNED_16BIT_LE_PCM) {
    bits = 16;
  }
  bytes_per_frame = bits / 8 * num_channels;
}

Dsp::~Dsp() {}

int Dsp::dopen() {
  int err;

  char* snd_device_in = "plughw:0,0";
  char* snd_device_out = "plughw:1,3";

  if ((err = snd_pcm_open(&playback_handle, snd_device_out,
                          SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    fprintf(stderr, "cannot open output audio device %s: %s\n", snd_device_in,
            snd_strerror(err));
    exit(1);
  }

  if ((err = snd_pcm_open(&capture_handle, snd_device_in,
                          SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    fprintf(stderr, "cannot open input audio device %s: %s\n", snd_device_out,
            snd_strerror(err));
    exit(1);
  }

  configure_alsa_audio(playback_handle);
  dspInBufSize = buffer_size * fragments;

  configure_alsa_audio(capture_handle);
  dspOutBufSize = buffer_size * fragments;

  dspOutBuf = (unsigned char**)malloc(sizeof(unsigned char*) * 2);
  dspOutBuf[0] = (unsigned char*)malloc(dspOutBufSize);
  dspOutBuf[1] = (unsigned char*)malloc(dspOutBufSize);
  memset(dspOutBuf[0], 0, dspOutBufSize);
  memset(dspOutBuf[1], 0, dspOutBufSize);
  dspInBuf = (unsigned char**)malloc(sizeof(unsigned char*) * 2);
  dspInBuf[0] = (unsigned char*)malloc(dspInBufSize);
  dspInBuf[1] = (unsigned char*)malloc(dspInBufSize);
  memset(dspInBuf[0], 0, dspInBufSize);
  memset(dspInBuf[1], 0, dspInBufSize);
  if (dspOutBuf[0] == NULL || dspOutBuf[0] == NULL || dspInBuf[0] == NULL ||
      dspInBuf[1] == NULL) {
    fprintf(stderr, "can't allocate dsp buffer\n");
    return 1;
  }

  dspOutBufPos = 0;
  dspOutBufIndex = 0;  // producing to bank 0
  dspOutReady[0] = 0;
  dspOutReady[1] = 1;  // bank 1 production done
  dspInBufPos = 0;
  dspInBufIndex = 1;  // consuming from bank 1
  dspInReady[0] = 0;  // consumer not ready to read from here yet
  dspInReady[1] = 0;
  outputend = 0;
  inputend = 0;

  pthread_mutex_init(&inputlock, NULL);
  pthread_mutex_init(&outputlock, NULL);
  pthread_cond_init(&inputcond, NULL);
  pthread_cond_init(&outputcond, NULL);

  // Spawn a thread that will continuously read from sound card
  pthread_attr_init(&inputattr);
  pthread_attr_setdetachstate(&inputattr, PTHREAD_CREATE_JOINABLE);
  pthread_create(&inputthreadid, &inputattr, (void* (*)(void*))soundinputrunner,
                 this);

  // Spawn a thread that will continuously write to sound card
  pthread_attr_init(&outputattr);
  pthread_attr_setdetachstate(&outputattr, PTHREAD_CREATE_JOINABLE);
  pthread_create(&outputthreadid, &outputattr,
                 (void* (*)(void*))soundoutputrunner, this);

  return 0;
}

void Dsp::dclose() {
  // First tell the input/output threads to die
  inputend = 1;
  outputend = 1;

  // Now wait for those threads to exit gracefully
  pthread_join(inputthreadid, NULL);
  pthread_join(outputthreadid, NULL);

  pthread_attr_destroy(&outputattr);
  pthread_attr_destroy(&inputattr);
  pthread_cond_destroy(&inputcond);
  pthread_cond_destroy(&outputcond);
  pthread_mutex_destroy(&inputlock);
  pthread_mutex_destroy(&outputlock);

  // Now free up all the memory and close handles
  free(dspOutBuf[0]);
  free(dspOutBuf[1]);
  free(dspOutBuf);
  free(dspInBuf[0]);
  free(dspInBuf[1]);
  free(dspInBuf);

  snd_pcm_close(playback_handle);
  snd_pcm_close(capture_handle);
  playback_handle = NULL;
  capture_handle = NULL;
}

int Dsp::isOpen() {
  return !(playback_handle == 0 || capture_handle == 0);
}

// Must be called while holding output lock
void Dsp::putByte(unsigned char b) {
  dspOutBuf[dspOutBufIndex][dspOutBufPos] = b;
  dspOutBufPos++;
  if (dspOutBufPos >= dspOutBufSize) {
    // We've filled the buffer, wait until the output thread tells us
    // we should start filling the next one
    dspOutReady[dspOutBufIndex] = 1;
    while (dspOutBufPos >= dspOutBufSize) {
      int ret = pthread_cond_wait(&outputcond, &outputlock);
      assert(ret == 0);
    }
    assert(dspOutBufPos == 0);
  }
}

// Must be called while holding input lock
unsigned char Dsp::getByte() {
  unsigned char b = dspInBuf[dspInBufIndex][dspInBufPos];
  dspInBufPos++;
  if (dspInBufPos >= dspInBufSize) {
    // We've reached the end of the buffer, wait until the input thread
    // tells us the next one is ready for processing
    dspInReady[1 - dspInBufIndex] = 1;
    while (dspInBufPos >= dspInBufSize) {
      int ret = pthread_cond_wait(&inputcond, &inputlock);
      assert(ret == 0);
    }
    assert(dspInBufPos == 0);
  }
  return b;
}

// This will cause silence which is not what caller wants...fix this
void Dsp::flush() {
  pthread_mutex_lock(&outputlock);
  if (dspOutBufPos != 0) {
    int bufIndex = dspOutBufIndex;
    // Put 0 until we cross a buffer boundary
    while (bufIndex == dspOutBufIndex) {
      putByte(0);
    }
  }
  pthread_mutex_unlock(&outputlock);
}

void Dsp::discardInput() {
  pthread_mutex_lock(&inputlock);
  // We've reached the end of the buffer, wait until the input thread
  // tells us the next one is ready for processing
  dspInReady[1 - dspInBufIndex] = 1;
  // TODO while?
  pthread_cond_wait(&inputcond, &inputlock);
  pthread_mutex_unlock(&inputlock);
}

void Dsp::discardOutput() {
  // Just reset the position to 0 as though we never put anything
  // there in the first place
  pthread_mutex_lock(&outputlock);
  dspOutBufPos = 0;
  pthread_mutex_unlock(&outputlock);
}

void Dsp::setduplex(int n) {}

void soundoutputrunner(void* data) {
  Dsp* dsp = (Dsp*)data;

  int restarting = 1;
  int outframes;

#ifdef RECORD_OUTPUT
  FILE* fp = fopen("/tmp/sndout", "w");
#endif

  // This loop never blocks except on the sound card device itself
  // It continuously alternates flushing the contents of buffers 0 and 1
  // to the sound card, detecting if the producer was too slow to fill
  // a buffer it's just about to flush
  while (dsp->outputend == 0) {
    if (restarting) {
      restarting = 0;
      snd_pcm_drop(dsp->playback_handle);
      snd_pcm_prepare(dsp->playback_handle);
    }

    // Tell producer to start filling buffer 0
    // as we are about to output buffer 1
    pthread_mutex_lock(&dsp->outputlock);

    if (dsp->dspOutReady[1] == 0) {
#ifdef OUTPUT_WARN
      fprintf(stderr, "too slow producing to buf 1 at %d\n", dsp->dspOutBufPos);
#endif
      // The producer did not fill this buffer in time, clear the remaining
      // portion and reset buf index to 1 as though they did finish
      // Sound card will get blank audio or 'pops' when this happens
      memset(&dsp->dspOutBuf[1][dsp->dspOutBufPos], 0,
             dsp->dspOutBufSize - dsp->dspOutBufPos);
    } else {
      pthread_cond_signal(&dsp->outputcond);
    }
    dsp->dspOutBufPos = 0;
    dsp->dspOutBufIndex = 0;
    dsp->dspOutReady[1] = 0;
    pthread_mutex_unlock(&dsp->outputlock);

#ifdef RECORD_OUTPUT
    fwrite(dsp->dspOutBuf[1], 1, dsp->dspOutBufSize, fp);
#endif
    while ((outframes = snd_pcm_writei(
                dsp->playback_handle, dsp->dspOutBuf[1],
                dsp->dspOutBufSize / dsp->bytes_per_frame)) < 0) {
      if (outframes == -EAGAIN)
        continue;
      fprintf(stderr, "Output buffer underrun\n");
      restarting = 1;
      snd_pcm_prepare(dsp->playback_handle);
    }

    if (restarting) {
      restarting = 0;
      snd_pcm_drop(dsp->playback_handle);
      snd_pcm_prepare(dsp->playback_handle);
    }

    // Tell producer to start filling buffer 1
    // as we are about to output buffer 0
    pthread_mutex_lock(&dsp->outputlock);

    if (dsp->dspOutReady[0] == 0) {
#ifdef OUTPUT_WARN
      fprintf(stderr, "too slow producing to buf 0 at %d\n", dsp->dspOutBufPos);
#endif
      // The producer did not fill this buffer in time, clear the remaining
      // portion and reset buf index to 0 as though they did finish
      // Sound card will get blank audio or 'pops' when this happens
      memset(&dsp->dspOutBuf[0][dsp->dspOutBufPos], 0,
             dsp->dspOutBufSize - dsp->dspOutBufPos);
    } else {
      pthread_cond_signal(&dsp->outputcond);
    }
    dsp->dspOutBufPos = 0;
    dsp->dspOutBufIndex = 1;
    dsp->dspOutReady[0] = 0;
    pthread_mutex_unlock(&dsp->outputlock);

#ifdef RECORD_OUTPUT
    fwrite(dsp->dspOutBuf[0], 1, dsp->dspOutBufSize, fp);
#endif
    while ((outframes = snd_pcm_writei(
                dsp->playback_handle, dsp->dspOutBuf[0],
                dsp->dspOutBufSize / dsp->bytes_per_frame)) < 0) {
      if (outframes == -EAGAIN)
        continue;
      fprintf(stderr, "Output buffer underrun\n");
      restarting = 1;
      snd_pcm_prepare(dsp->playback_handle);
    }

#ifdef RECORD_OUTPUT
    fflush(fp);
#endif
  }
#ifdef RECORD_OUTPUT
  fclose(fp);
#endif

  pthread_exit(NULL);
}

void soundinputrunner(void* data) {
  Dsp* dsp = (Dsp*)data;

  int restarting = 1;
  int inframes;

#ifdef RECORD_INPUT
  FILE* fp = fopen("/tmp/sndin", "w");
#endif
#ifdef PLAY_RECORDING
  FILE* fp = fopen("/tmp/sndin", "r");
#endif

  // This loop never blocks except on the sound card device itself
  // It continuously alternates filling the contents of buffers 0 and 1
  // from the sound card, detecting if the consumer was too slow to process
  // a buffer it filled previously
  while (dsp->inputend == 0) {
    if (restarting) {
      restarting = 0;
      snd_pcm_drop(dsp->capture_handle);
      snd_pcm_prepare(dsp->capture_handle);
    }

    while ((inframes =
                snd_pcm_readi(dsp->capture_handle, dsp->dspInBuf[0],
                              dsp->dspInBufSize / dsp->bytes_per_frame)) < 0) {
      if (inframes == -EAGAIN)
        continue;
      fprintf(stderr, "Input buffer overrun\n");
      restarting = 1;
      snd_pcm_prepare(dsp->capture_handle);
    }

#ifdef PLAY_RECORDING
    fread(dsp->dspInBuf[0], 1, dsp->dspInBufSize, fp);
#endif
#ifdef RECORD_INPUT
    fwrite(dsp->dspInBuf[0], 1, dsp->dspInBufSize, fp);
#endif

    // Tell consumer its okay to start reading from buffer 0 we just filled
    // as we area about to start filling buffer 1
    pthread_mutex_lock(&dsp->inputlock);

    assert(dsp->dspInBufIndex == 1);

    if (dsp->dspInReady[0] == 0) {
#ifdef INPUT_WARN
      fprintf(stderr, "too slow consuming from 1 at %d\n", dsp->dspInBufPos);
#endif
      // Consumer will have 'gaps' in its data when this happens
    } else {
      pthread_cond_signal(&dsp->inputcond);
    }
    dsp->dspInBufPos = 0;
    dsp->dspInBufIndex = 0;
    dsp->dspInReady[0] = 0;

    pthread_mutex_unlock(&dsp->inputlock);

    if (restarting) {
      restarting = 0;
      snd_pcm_drop(dsp->capture_handle);
      snd_pcm_prepare(dsp->capture_handle);
    }

    while ((inframes =
                snd_pcm_readi(dsp->capture_handle, dsp->dspInBuf[1],
                              dsp->dspInBufSize / dsp->bytes_per_frame)) < 0) {
      if (inframes == -EAGAIN)
        continue;
      fprintf(stderr, "Input buffer overrun\n");
      restarting = 1;
      snd_pcm_prepare(dsp->capture_handle);
    }

#ifdef PLAY_RECORDING
    fread(dsp->dspInBuf[1], 1, dsp->dspInBufSize, fp);
#endif
#ifdef RECORD_INPUT
    fwrite(dsp->dspInBuf[1], 1, dsp->dspInBufSize, fp);
#endif

    // Tell consumer its okay to start reading from buffer 1 we just filled
    // as we are about to start filling buffer 0
    pthread_mutex_lock(&dsp->inputlock);

    assert(dsp->dspInBufIndex == 0);

    if (dsp->dspInReady[1] == 0) {
#ifdef INPUT_WARN
      fprintf(stderr, "too slow consuming from 0 at %d\n", dsp->dspInBufPos);
#endif
      // Consumer will have 'gaps' in its data when this happens
    } else {
      pthread_cond_signal(&dsp->inputcond);
    }
    dsp->dspInBufPos = 0;
    dsp->dspInBufIndex = 1;
    dsp->dspInReady[1] = 0;

    pthread_mutex_unlock(&dsp->inputlock);

#ifdef RECORD_INPUT
    fflush(fp);
#endif
  }

#ifdef RECORD_INPUT
  fclose(fp);
#endif
  pthread_exit(NULL);
}

void Dsp::offHook() {}

void Dsp::onHook() {}

float Dsp::insample() {
  float returnSample = 0;

  pthread_mutex_lock(&inputlock);

  if (format == SIGNED_8BIT_PCM) {
    unsigned char b = getByte();
    returnSample = (((float)b) / 64.0) - 2.0;
    samplecount++;
  } else if (format == SIGNED_16BIT_LE_PCM) {
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

void Dsp::outsample(float x) {
  // Range must be between -2.0 and +2.0
  if (x > 2.0f)
    x = 2.0f;
  if (x < -2.0f)
    x = -2.0f;

  pthread_mutex_lock(&outputlock);
  if (format == SIGNED_8BIT_PCM) {
    float y = x + 2.0;
    y = y * 64.0;
    putByte((unsigned char)y);
  } else if (format == SIGNED_16BIT_LE_PCM) {
    float y = x * 16384.0f;
    signed short q = (short)y;

    putByte((unsigned char)(q & 0xFF));
    putByte((unsigned char)((q >> 8) & 0xFF));
  } else {
    printf("UNKNOWN FORMAT!!!\n");
  }
  pthread_mutex_unlock(&outputlock);
}
