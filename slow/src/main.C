/* Modem for MIPS   AJF	  January 1995
 Main program */

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

  modem->open();
  modem->start();
  modem->close();

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
  char* telno = NULL;
  char deviceName[16];
  char bluetoothAddress[32];
  int bluetoothChannel = 2;
  deviceName[0] = 0;
  strcpy(bluetoothAddress, "11:11:11:11:11:11");
  SamplingDevice::SamplerType samplerType = SamplingDevice::SoundCard;
  CommonTypes::EndPoint endPoint = CommonTypes::ENDPOINT_1;
  int devInMode = SamplingDevice::DEV_IN_DEVICE;
  int devOutMode = SamplingDevice::DEV_OUT_DEVICE;

  int ap = 1;
  while (ap < argc) {
    char* s = argv[ap++];

    if (seq(s, "-d")) {
      strncpy(deviceName, argv[ap++], 16);
      deviceName[15] = 0;
    } else if (seq(s, "-t")) {
      strncpy(bluetoothAddress, argv[ap++], 32);
      bluetoothAddress[31] = 0;
    } else if (seq(s, "-c")) {
      bluetoothChannel = atoi(argv[ap++]);
    } else if (seq(s, "-m1")) {
      samplerType = SamplingDevice::SharedMemoryPipe;
      endPoint = CommonTypes::ENDPOINT_1;
    } else if (seq(s, "-m2")) {
      samplerType = SamplingDevice::SharedMemoryPipe;
      endPoint = CommonTypes::ENDPOINT_2;
    } else if (seq(s, "-b")) {
      samplerType = SamplingDevice::Bluetooth;
      endPoint = CommonTypes::ENDPOINT_1;
    } else if (seq(s, "-dev")) {
      char* bm = argv[ap++];
      if (seq(bm, "from_device")) {
        devInMode = SamplingDevice::DEV_IN_DEVICE;
      } else if (seq(bm, "from_file")) {
        devInMode = SamplingDevice::DEV_IN_PLAY_FROM_FILE;
      } else if (seq(bm, "from_device_save_to_file")) {
        devInMode = SamplingDevice::DEV_IN_RECORD;
      } else if (seq(bm, "to_device")) {
        devOutMode = SamplingDevice::DEV_OUT_DEVICE;
      } else if (seq(bm, "to_file")) {
        devOutMode = SamplingDevice::DEV_OUT_PLAY_FROM_FILE;
      } else if (seq(bm, "to_device_save_to_file")) {
        devOutMode = SamplingDevice::DEV_OUT_RECORD;
      } else {
        return new ModemOptions(ModemOptions::Invalid);
      }
    } else if (seq(s, "-s")) {
      samplerType = SamplingDevice::SoundCard;
      endPoint = CommonTypes::ENDPOINT_1;
    } else if (seq(s, "-v")) {
      options |= opt_v;
    } else if (seq(s, "-h") || seq(s, "--help")) {
      return new ModemOptions(ModemOptions::Invalid);
    } else if (seq(s, "-bps")) {
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
      new ModemOptions(options, bitrates, veemode, numpages, telno, deviceName,
                       samplerType, endPoint, bluetoothAddress,
                       bluetoothChannel, (devInMode << 8) | devOutMode);
  return modemOptions;
}

static void usage() {
  fprintf(stderr, "FisherSoftModem 1.0\n\n");
  fprintf(stderr,
          "Usage: modem [-a|-b|-s|-m1|-m2] [-t addr] [-c channel] [-v] [-d "
          "ttydevice]\n");
  fprintf(stderr,
          "   -s use /dev/dsp for modulated output/input device (default)\n");
  fprintf(stderr, "   -b use bluetooth for input/output device (HS profile)\n");
  fprintf(stderr, "   -t set bluetooth address\n");
  fprintf(stderr, "   -c set headset audio gateway channel\n");
  fprintf(stderr, "   -m1 become endpoint 1 of shared memory pipe\n");
  fprintf(stderr, "   -m2 become endpoint 2 of shared memory pipe\n");
  fprintf(stderr, "   -d create tty device instead of console\n");
  fprintf(stderr, "   -v verbose\n");
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
