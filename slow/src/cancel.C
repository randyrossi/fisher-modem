/* Modem for MIPS   AJF	  October 1996
   Echo canceller routines */

#include <stdio.h>
#include <math.h>
#include <string.h> /* memset */

#include <complex.h>
#include <pthread.h>

#include "Modem.h"
#include "cancel.h"

void canceller::reset() {
  pthread_mutex_lock(&lock);
  memset(coeffs, 0, CC_NCS * sizeof(complex));
  memset(in, 0, CC_SIZE * sizeof(complex));
  next = 0;
  pthread_mutex_unlock(&lock);
}

void canceller::insert(complex z) {
  /* circular buffer */
  pthread_mutex_lock(&lock);
  in[next] = z;
  if (++next >= CC_SIZE)
    next = 0;
  pthread_mutex_unlock(&lock);
}

complex canceller::get() {
  /* get predicted echo value */
  pthread_mutex_lock(&lock);
  complex z = 0.0;
  int j = 0;
  int k = CC_SIZE + CC_EBEG - TRDELAY;
  while (j < CC_NCS) {
    int p = (next + k) & (CC_SIZE - 1);
    z += coeffs[j] * in[p];
    j++;
    k += SYMBLEN / 2;
  }
  pthread_mutex_unlock(&lock);
  return z;
}

void canceller::update(complex eps) {
  pthread_mutex_lock(&lock);
  complex deps = (delta / (float)CC_NCS) * eps;
  int j = 0;
  int k = CC_SIZE + CC_EBEG - TRDELAY;
  while (j < CC_NCS) {
    int p = (next + k) & (CC_SIZE - 1);
    coeffs[j] += deps * cconj(in[p]);
    j++;
    k += SYMBLEN / 2;
  }
  pthread_mutex_unlock(&lock);
}

inline float hypot(complex z) {
  return hypot(z.im, z.re);
}

inline float atan2(complex z) {
  return atan2(z.im, z.re);
}

void canceller::print(char* fn) {
  pthread_mutex_lock(&lock);
  FILE* fi = fopen(fn, "w");
  if (fi != NULL) {
    int spc = SYMBLEN / 2;
    fprintf(fi, ".sp 0.5i\n");
    fprintf(fi, ".G1 8i\n");
    fprintf(fi, "new solid\n");
    for (int j = 0; j < CC_NCS; j++)
      fprintf(fi, "%4d %g\n", CC_EBEG + (j * spc), coeffs[j].re);
    fprintf(fi, ".G2\n.bp\n");
    fprintf(fi, ".sp 0.5i\n.G1 8i\n");
    fprintf(fi, "new solid\n");
    for (int j = 0; j < CC_NCS; j++)
      fprintf(fi, "%4d %g\n", CC_EBEG + (j * spc), coeffs[j].im);
    fprintf(fi, ".G2\n.bp\n");
    fprintf(fi, ".sp 0.5i\n.G1 8i\n");
    fprintf(fi, "new solid\n");
    for (int j = 0; j < CC_NCS; j++)
      fprintf(fi, "%4d %g\n", CC_EBEG + (j * spc), hypot(coeffs[j]));
    fprintf(fi, ".G2\n.bp\n");
    fprintf(fi, ".sp 0.5i\n.G1 8i\n");
    fprintf(fi, "new solid\n");
    for (int j = 0; j < CC_NCS; j++)
      fprintf(fi, "%4d %g\n", CC_EBEG + (j * spc), atan2(coeffs[j]));
    fprintf(fi, ".G2\n.bp\n");
    fprintf(fi, ".sp 0.5i\n");
    for (int j = 0; j < CC_NCS; j++)
      fprintf(fi, "{ %10.6f, %10.6f },\n", coeffs[j].re, coeffs[j].im);
    fclose(fi);
  } else {
    fprintf(stderr, "can't create %s\n", fn);
  }
  pthread_mutex_unlock(&lock);
}
