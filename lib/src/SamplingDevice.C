#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "commonTypes.h"
#include "private.h"
#include "SamplingDevice.h"

// Still reads from device but replaces the data with file data.
int SamplingDevice::DEV_IN_PLAY_FROM_FILE = 1;
// Just reads from device.
int SamplingDevice::DEV_IN_DEVICE = 2;
// Reads from device and saves it to a file.
int SamplingDevice::DEV_IN_RECORD = 3;

// Writes file data to the device instead of any generated data.
int SamplingDevice::DEV_OUT_PLAY_FROM_FILE = 1;
// Writes to generated data to device.
int SamplingDevice::DEV_OUT_DEVICE = 2;
// Still writes generated data to device and saves to a file also.
int SamplingDevice::DEV_OUT_RECORD = 3;

SamplingDevice::SamplingDevice(int formatp) : format(formatp) {
  samplecount = 0;

  // Default is modem algorithm generates data out to device
  // and modem algorithm gets data from device
  devInMode = DEV_IN_DEVICE;
  devOutMode = DEV_OUT_DEVICE;
}

SamplingDevice::~SamplingDevice() {}

void SamplingDevice::setDevInMode(int m) {
  devInMode = m;
}

void SamplingDevice::setDevOutMode(int m) {
  devOutMode = m;
}
