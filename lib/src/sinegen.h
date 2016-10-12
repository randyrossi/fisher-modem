#ifndef SINEGEN_H
#define SINEGEN_H

#include "complex.h"

typedef unsigned int uint;

class SineGen {
 public:
  SineGen(float);

  void setfreq(float);
  float fnext();
  complex cnext();
  void resetphase() { ptr = 0; }
  void flipphase() { ptr ^= (1 << 31); }

 private:
  uint ptr;
  int phinc;
};

#endif
