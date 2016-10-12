#ifndef MODEM_OPTIONS_H
#define MODEM_OPTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <SamplingDevice.h>
#include <commonTypes.h>

class ModemOptions {
 public:
  enum Status {
    Valid,
    Invalid,
  };

  // These are used as indices for FSK modes so start at 0
  enum vmode {
    V21o = 0,
    V21a,
    V23o,
    V23a,
    E01o,  // Experimental
    E01a,  // Experimental
    V32o,
    V32a,
    V34o,
    V34a,
    V29a,
    V29o,
    VUndefined,
  };

  Status status;
  uint options;
  uint bitrates;
  vmode veemode;
  int numPages;
  char* telNo;
  char* deviceName;
  char* bluetoothAddress;
  int bluetoothChannel;
  SamplingDevice::SamplerType samplerType;
  CommonTypes::EndPoint endPoint;
  int samplingDeviceMode;

  ushort rateword;

  // Implies failure
  ModemOptions(Status status);

  // Implies success
  ModemOptions(uint options,
               uint bitrates,
               vmode veemode,
               int numPages,
               char* telNo,
               char* deviceName,
               SamplingDevice::SamplerType samplerType,
               CommonTypes::EndPoint endPoint,
               char* bluetoothAddress,
               int bluetoothChannel,
               int samplingDeviceMode);

 private:
  int legalbps(vmode m);
  bool legaltelno(char*);
};

#endif
