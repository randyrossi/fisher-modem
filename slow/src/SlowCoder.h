#ifndef SLOWCODER_H
#define SLOWCODER_H

/* segments of training sequence */
#define SLOW_SEG_1 0
#define SLOW_SEG_2 48
#define SLOW_SEG_3 (SLOW_SEG_2 + 128)
#define SLOW_SEG_4 (SLOW_SEG_3 + 384)

struct SlowTrainingGen {
  SlowTrainingGen() { reset(); }
  void reset() { reg = 0x2a; }
  complex get(int);

 private:
  uchar reg;
};

struct SlowEncoder {
  SlowEncoder() { reset(); }
  void reset() { state = 0; }
  complex encode(int);

 private:
  int state;
};

struct SlowDecoder {
  SlowDecoder() { reset(); }
  void reset() { state = 0; }
  int decode(complex);
  complex getez();

 private:
  int state;
  int locate(complex);
};

#endif
