#ifndef CANCEL_H
#define CANCEL_H

#include <pthread.h>
#include "complex.h"

#define CC_SIZE 2048
#define CC_EBEG -60
#define CC_EEND 30
#define CC_NCS ((CC_EEND - CC_EBEG) / (SYMBLEN / 2))

class canceller {
 public:
  canceller(float d) {
    pthread_mutex_init(&lock, NULL);
    delta = d;
    reset();
  }

  ~canceller() { pthread_mutex_destroy(&lock); }

  void reset();

  void insert(complex c); /* put new Tx value into canceller	     */
  complex get();          /* get predicted echo value		     */
  void update(complex c); /* given eps, update coeffs		     */
  void print(char*);      /* print coeffs				     */

 private:
  complex coeffs[CC_NCS]; /* vectors of coefficients		*/
  complex in[CC_SIZE];    /* circular buffer for input samples	*/
  int next;               /* ptr to next place to insert		*/
  float delta;

  pthread_mutex_t lock;
};

#endif
