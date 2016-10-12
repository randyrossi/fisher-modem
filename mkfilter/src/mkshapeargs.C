#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <strings.h>

// This program figures out what mkshape arguments are required to
// produce the raised cosine shape given on sample rate and baud.  Since
// we want our sample rate to be dynamic, this will change the length
// of the shape table assuming the 2nd half is desired to be
// SYMBLEN*2+1, where SYMBLEN = SAMPLERATE / BAUDRATE
//
// Usage:
//    mkshapeargs SAMPLERATE BAUDRATE BETA
//
// Example:
//    mkshapeargs.exe 9600 2400 .5
//
// Gives alpha, beta and length arguments
//    1.2500000000e-01   5.0000000000e-01 17

int main(int argc, char** argv) {
  int sampleRate = atoi(argv[1]);
  int baudRate = atoi(argv[2]);
  float beta = atof(argv[3]);

  float alpha = 1.0f / ((float)sampleRate / ((float)baudRate / 2.0f));

  int length = (sampleRate / baudRate * 2 + 1) * 2 - 1;

  printf("%18.10e %18.10e %d\n", alpha, beta, length);
  return 0;
}
