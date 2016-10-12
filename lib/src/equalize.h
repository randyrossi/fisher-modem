#ifndef EQUALIZER_H
#define EQUALIZER_H

#define EQ_SIZE 16
#define EQ_NP 7

struct equalizer {
  equalizer(float d) {
    delta = d;
    reset();
  }
  void reset();
  void insert(complex); /* put new raw value into equalizer		    */
  complex get();        /* get equalized value			    */
  void update(complex eps) {
    upd(eps, EQ_NP);
  } /* given eps, update coeffs			    */
  void short_update(complex eps) {
    upd(eps, 2);
  }                  /* ditto, use short window	// WAS 1	    */
  int getdt();       /* get timing offset				    */
  void shift(int);   /* shift coefficients vector			    */
  void print(char*); /* print coefficients vector			    */

 private:
  void upd(complex, int);
  complex coeffs[2 * EQ_NP + 1]; /* vector of coefficients */
  complex in[EQ_SIZE]; /* circular buffer				    */
  int next;            /* ptr to next place to insert		    */
  float delta;
};

#endif
