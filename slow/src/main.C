/*
 * Modem for MIPS   AJF	  January 1995
 * Main program
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/fcntl.h>
#include <time.h>

#include "Modem.h"

struct vminfo {
  char* s;
  ModemOptions::vmode v;
};

static void catchsignal(int), sighandler(int);
static uint bitrate(int);
static ModemOptions* getOptions(int, char**);
static void usage();

inline bool isdigit(char ch) {
  return (ch >= '0') && (ch <= '9');
}

int main(int argc, char** argv) {
  catchsignal(SIGINT);
  catchsignal(SIGTERM);
  catchsignal(SIGUSR1);
  catchsignal(SIGABRT);

  ModemOptions* options = getOptions(argc, argv);
  if (options->status != ModemOptions::Valid) {
    usage();
    exit(1);
  }

  SamplingDevice* samplingDevice = NULL;

  switch (options->samplerType) {
    case SamplingDevice::SoundCard:
      samplingDevice = new Dsp(Dsp::SIGNED_16BIT_LE_PCM);
      // samplingDevice = new Dsp(Dsp::SIGNED_8BIT_PCM);
      break;
    case SamplingDevice::SharedMemoryPipe:
      samplingDevice = new MemPipe(options->endPoint);
      break;
    case SamplingDevice::Bluetooth:
      // Need throttling due to bluez write wierdness
      samplingDevice = new BluetoothDevice(options->bluetoothAddress,
                                           options->bluetoothChannel, true);
      break;
    default:
      fprintf(stderr, "unknown sampling device type\n");
      return -1;
  }

  samplingDevice->setDevInMode((options->samplingDeviceMode >> 8) & 0xFF);
  samplingDevice->setDevOutMode(options->samplingDeviceMode & 0xFF);

  TerminalDevice* terminalDevice = new TerminalDevice(options->deviceName);

  Modem* modem = new Modem(samplingDevice, terminalDevice, options);

  if (modem->open() == 0) {
    modem->start();
    modem->close();
  }

  delete modem;
  delete samplingDevice;
  delete terminalDevice;
  delete options;

  return 0;
}

static void catchsignal(int sig) {
  signal(sig, sighandler);
}

static void sighandler(int sig) {
  switch (sig) {
    case SIGUSR1:
      fprintf(stderr, "SIGUSR1 caught\n");
    default:
      fprintf(stderr, "caught signal %d\n", sig);
  }
}

static ModemOptions* getOptions(int argc, char** argv) {
  int numpages = 0;
  uint options = 0, bitrates = 0;
  ModemOptions::vmode veemode = ModemOptions::VUndefined;
  SamplingDevice::SamplerType samplerType = SamplingDevice::SoundCard;
  CommonTypes::EndPoint endPoint = CommonTypes::UNSPECIFIED;
  int devInMode = SamplingDevice::DEV_IN_DEVICE;
  int devOutMode = SamplingDevice::DEV_OUT_DEVICE;

  char* telno = NULL;
  char terminalDeviceName[16];
  char outputDeviceName[16];
  char bluetoothAddress[32];
  int bluetoothChannel = 2;

  strcpy(terminalDeviceName, "");
  strcpy(outputDeviceName, "");
  strcpy(bluetoothAddress, "11:11:11:11:11:11");

  int ap = 1;
  while (ap < argc) {
    char* s = argv[ap++];

    if (seq(s, "-term")) {
      if (ap >= argc) return new ModemOptions(ModemOptions::Invalid);
      strncpy(terminalDeviceName, argv[ap++], 16);
      terminalDeviceName[15] = 0;
    } else if (seq(s, "-t")) {
      if (ap >= argc) return new ModemOptions(ModemOptions::Invalid);
      strncpy(bluetoothAddress, argv[ap++], 32);
      bluetoothAddress[31] = 0;
    } else if (seq(s, "-c")) {
      if (ap >= argc) return new ModemOptions(ModemOptions::Invalid);
      bluetoothChannel = atoi(argv[ap++]);
    } else if (seq(s, "-dev")) {
      if (ap >= argc) return new ModemOptions(ModemOptions::Invalid);
      strncpy(outputDeviceName, argv[ap++], 16);
      outputDeviceName[15] = 0;
      if (strcmp(outputDeviceName, "bluetooth") == 0) {
        samplerType = SamplingDevice::Bluetooth;
      } else if (strcmp(outputDeviceName, "mempipe1") == 0) {
        samplerType = SamplingDevice::SharedMemoryPipe;
        endPoint = CommonTypes::ENDPOINT_1;
      } else if (strcmp(outputDeviceName, "mempipe2") == 0) {
        samplerType = SamplingDevice::SharedMemoryPipe;
        endPoint = CommonTypes::ENDPOINT_2;
      } else if (strcmp(outputDeviceName, "dsp") == 0) {
        samplerType = SamplingDevice::SoundCard;
      } else {
        return new ModemOptions(ModemOptions::Invalid);
      }
    } else if (seq(s, "-in")) {
      if (ap >= argc) return new ModemOptions(ModemOptions::Invalid);
      char* bm = argv[ap++];
      if (seq(bm, "play")) {
        devInMode = SamplingDevice::DEV_IN_PLAY_FROM_FILE;
      } else if (seq(bm, "record")) {
        devInMode = SamplingDevice::DEV_IN_RECORD;
      } else {
        return new ModemOptions(ModemOptions::Invalid);
      }
    } else if (seq(s, "-out")) {
      if (ap >= argc) return new ModemOptions(ModemOptions::Invalid);
      char* bm = argv[ap++];
      if (seq(bm, "play")) {
        devOutMode = SamplingDevice::DEV_OUT_PLAY_FROM_FILE;
      } else if (seq(bm, "record")) {
        devOutMode = SamplingDevice::DEV_OUT_RECORD;
      }
    } else if (seq(s, "-v")) {
      options |= opt_v;
    } else if (seq(s, "-h") || seq(s, "--help")) {
      return new ModemOptions(ModemOptions::Invalid);
    } else if (seq(s, "-bps")) {
      if (ap >= argc) return new ModemOptions(ModemOptions::Invalid);
      while (argv[ap] != NULL && isdigit(argv[ap][0])) {
        int bps = atoi(argv[ap++]);
        bitrates |= bitrate(bps);
      }
      options |= opt_bps;
    } else {
      return new ModemOptions(ModemOptions::Invalid);
    }
  }

  ModemOptions* modemOptions =
      new ModemOptions(options, bitrates, veemode, numpages, telno, terminalDeviceName,
                       samplerType, endPoint, bluetoothAddress,
                       bluetoothChannel, (devInMode << 8) | devOutMode);
  return modemOptions;
}

static void usage() {
  fprintf(stderr, "FisherSoftModem 1.0\n\n");
  fprintf(stderr,
          "Usage: modem [options]\n");
  fprintf(stderr, " -term <tty_device> (create tty device instead of console)\n");
  fprintf(stderr, " -dev <output_device> (default dsp)\n");
  fprintf(stderr, "   where output_device is one of:\n");
  fprintf(stderr, "     dsp (soundcard)\n");
  fprintf(stderr, "     bluetooth\n");
  fprintf(stderr, "     mempipe1 : enpoint 1 of shared mem pipe\n");
  fprintf(stderr, "     mempipe2 : enpoint 2 of shared mem pipe\n");
  fprintf(stderr, "  -t set bluetooth address\n");
  fprintf(stderr, "  -c <channel> set headset audio gateway channel\n");
  fprintf(stderr, "  -d \n");
  fprintf(stderr, "  -out <play|record>\n");
  fprintf(stderr, "  -in <play|record>\n");
  fprintf(stderr, "  -v verbose\n");
  putc('\n', stderr);
}

static uint bitrate(int n) {
  /* see bitrates.h */
  unless(n >= 2400 && n <= 28800 && n % 2400 == 0) {
    fprintf(stderr, "unknown bit rate %d\n", n);
    exit(1);
  }
  return 1 << (n / 2400 - 1);
}
