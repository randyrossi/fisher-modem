#ifndef MODEMOBJ_H
#define MODEMOBJ_H

#define SECS(f) ((int)(f * SAMPLERATE + 0.5))

#define BAUDRATE 2400                   /* symbols per sec (V.29)	      */
#define SYMBLEN (SAMPLERATE / BAUDRATE) /* num. samples per symbol (V.29) */

#define TRDELAY (SAMPLERATE / 5) /* 0.2 secs Tx-to-Rx delay */

#define ZBAUDRATE 600
#define ZSYMBLEN (SAMPLERATE / ZBAUDRATE)
#define ZDELAY SECS(0.04)

#include <string.h>
#include <commonTypes.h>
#include <SamplingDevice.h>
#include <TerminalDevice.h>
#include <complex.h>
#include <sinegen.h>
#include <scramble.h>

#include <dsp.h>
#include <memPipe.h>
#include <bt.h>

#include "ModemOptions.h"
#include "BitBuffer.h"
#include "cancel.h"

/* bits in rate word */
#define RWORD 0x0991 /* V.32 bis */
#define rb_4800 0x0400
#define rb_9600 0x0200
#define rb_7200 0x0040
#define rb_12000 0x0020
#define rb_14400 0x0008

#include "FastCoder.h"

typedef unsigned long long u64bit;

#define unless(x) if (!(x))
#define until(x) while (!(x))

#define HDLC_FLAG (-1)
#define HDLC_ABORT (-2)

/* all known options for the modem */
#define opt_mod 0x002 /* modem call	  */
#define opt_bps 0x010 /* bits per sec	  */
#define opt_v 0x020   /* verbose		  */
#define opt_H 0x200   /* high resolution	  */

#define reg_escape_character 0x02        // S2
#define reg_escape_code_guard_time 0x0C  // S12
#define reg_echo 0x40                    // ATEn
#define reg_X 0x41                       // ATXn
#define reg_mode 0x42                    // AT+MS
#define reg_v8 0x43                      // AT+MS
#define reg_wait_conn_tone 0x44
#define reg_send_answer 0x45
#define reg_verbal 0x46

union word /* for message routines */
{
  word() {}
  word(char* sx) { s = sx; }
  word(int ix) { i = ix; }
  char* s;
  int i;
};

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef signed char schar;
typedef void (*proc)();

extern int errno; /* from sys lib */

// extern "C"
//  {
//    void exit(int);
//    void close(int), sleep(int);
//  };

inline bool seq(char* s1, char* s2) {
  return strcmp(s1, s2) == 0;
}

inline float sqr(float x) {
  return x * x;
}

inline float fsgn(float x) {
  return (x > 0.0f) ? +1.0f : (x < 0.0f) ? -1.0f : 0.0f;
}

inline bool after(int t1, int t2) {
  return ((t2 - t1) & (1 << 31)) != 0;
}

class FSK;
class v29G;
class v32G;
class v34G;
class Fax;
class Hayes;

class Modem {
 public:
  enum Progress { NOT_FOUND, UNOBTAINABLE, BUSY, FOUND, KEY_PRESSED, UNKNOWN };

  enum Tone { DIAL_TONE, CONN_TONE };

 private:
  void usage(int n);
  bool sendanswer();
  bool dialnumber(char* telno);
  void collapse(char* ovec, char* s1, char* s2);
  bool senddtmf(char* s);
  Progress waitfortone(Tone tone);
  void becomeSlowModem();  // v21,v23
  void becomeFastModem();  // v32
  void becomeZoomModem();  // v34
  void becomeFaxModem();   // v29
  void becomeExperimentalModem1();   // not a real modem mode
  void becomeExperimentalModem2();  // not a real modem mode

  /* v8 routines */
  bool startsession();

  void setConstructorRegister(int reg, int value);

 public:
  Modem(SamplingDevice* samplingDevice,
        TerminalDevice* terminalDevice,
        ModemOptions* modemOptions);

  ~Modem();

  enum State {
    INITIALIZING,
    DISCONNECTED,
    WAITING_FOR_DIAL_TONE,
    EMITTING_DIAL_TONE,  // for testing dial tone detection
    DIALING,
    ANSWERING,
    CONNECTED,
    SHUTDOWN
  };

  // DISCONNECTED - character input goes to hayes interpretor
  // WAITING_FOR_DIAL_TONE - the state just before dialing
  // DIALING - output of tones
  // SENDING_ANSWER - sending answer tone
  // CONNECTED - full tx/rx loop engaged
  // ESCAPED - connected but character input goes to hayes interpretor

  // COMMON
  SamplingDevice* samplingDevice;
  TerminalDevice* terminalDevice;
  ModemOptions* modemOptions;
  State modemState;
  FSK* fsk;
  v29G* v29;
  v32G* v32;
  v34G* v34;
  Fax* fax;
  Hayes* hayes;
  int registers[256];
  bool registerVisible[256];
  long lastCharTime;
  int plusses;
  bool escaped;

  int open();
  void close();
  void start();
  void putChar(int ch);
  void putChars(char* str);
  void newLine();
  int getChar(void);
  int getCharSynch(void);
  int onlineInterpretChar(int);
  void stopmodem();

  int rxloopstop;
  int txloopstop;

  int progressrxloopstop;
  int progresstxloopstop;

  // what type of tone are we trying to detect? for progress.F
  Tone progresstone;

  // what was the result of the tone detection call?
  Progress progressResult;

  /* v8 routines/attrs that need to be public */
  int v8txloopstop;
  int v8rxloopstop;
  u64bit menu;
  void putmenu(u64bit msg);
  void norm(u64bit& m);
  uint computemodes(ModemOptions::vmode m);
  bool samemsg(u64bit m1, u64bit m2);
  u64bit getmsg();
  int getbyte();

  bool sendfreqs(float f1, float f2, float t);
  bool sendfreq(float f, float t);
  bool sendpause(float t);
  void infomsg(char* msg, ...);
  void giveup(char* msg, ...);

  void setRegister(int reg, int value);
  int getRegister(int reg);
  bool isRegisterVisible(int reg);

  void doInitializingState();
  void doDisconnectedState();
  void doAnsweringState();
  void doConnectedState();
  void doDialingState();
  void doShutdownState();
  bool isOriginator();
};

#endif
