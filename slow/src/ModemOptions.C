#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SamplingDevice.h>
#include <commonTypes.h>
#include "Modem.h"
#include "bitrates.h"

ModemOptions::ModemOptions(Status status) {
  this->status = status;
}

ModemOptions::ModemOptions(uint op,
                           uint br,
                           vmode mode,
                           int nump,
                           char* tel,
                           char* dName,
                           SamplingDevice::SamplerType samplerType,
                           CommonTypes::EndPoint endPoint,
                           char* btAddr,
                           int btChannel,
                           int samplingDeviceMode) {
  this->status = ModemOptions::Valid;
  this->options = op;
  this->bitrates = br;
  this->veemode = mode;
  this->numPages = nump;
  this->telNo = tel;
  this->deviceName = strdup(dName);
  this->bluetoothAddress = strdup(btAddr);
  this->bluetoothChannel = btChannel;
  this->samplerType = samplerType;
  this->endPoint = endPoint;
  this->samplingDeviceMode = samplingDeviceMode;

  if (options & opt_mod) {
    uint b = legalbps(veemode);
    /* default is the full set */
    unless(options & opt_bps) bitrates = b;
    if (bitrates & ~b) {
      this->status = Invalid;
    }
  }

  rateword = RWORD;
  if (bitrates & bps_4800)
    rateword |= rb_4800;
  if (bitrates & bps_7200)
    rateword |= rb_7200;
  if (bitrates & bps_9600)
    rateword |= rb_9600; /* trellis coding */
  if (bitrates & bps_12000)
    rateword |= rb_12000;
  if (bitrates & bps_14400)
    rateword |= rb_14400;
}

int ModemOptions::legalbps(vmode m) {
  switch (m) {
    default:
      return 0;
    case V32o:
      return bps_4800 | bps_7200 | bps_9600 | bps_12000 | bps_14400;

    case V34o:
      return bps_2400 | bps_4800 | bps_7200 | bps_9600 | bps_12000 | bps_14400 |
             bps_16800 | bps_19200 | bps_21600 | bps_24000 | bps_26400 |
             bps_28800;
  }
}

bool ModemOptions::legaltelno(char* vec) {
  int n = 0;
  char* dstr = "0123456789*#ABCD";
  if (vec == NULL) {
    return false;
  }
  if (vec[n] == '+' || vec[n] == '=')
    n++;
  until(vec[n] == '\0' || strchr(dstr, vec[n]) == NULL) n++;
  return (n <= 20 && vec[n] == '\0');
}
