#ifndef HAYESOBJ_H
#define HAYESOBJ_H

class Modem;

class Hayes {
 private:
  enum State {
    SCAN,
    SAW_A,
    SAW_AT,      // start to accumulate
    SAW_ASLASH,  // repeat last
  };

  Modem* modem;
  State hayesState;
  int accumulatorPos;
  char accumulator[32];

 public:
  enum Transition { NONE, SHUTDOWN, ONLINE, ANSWER, DIAL, HANGUP };
  char num[32];

  Hayes(Modem* modem);

  Transition interpret(int c);
  Transition parseAttention();

  void ok();
  void error();
  void connect();
  void nocarrier();
  void nodialtone();
  void noanswer();
  void busy();
  void unobtainable();
};

#endif
