#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sinegen.h>
#include <sys/time.h>

#include "../../threadutil/src/threadutil.h"
#include "../../threadutil/src/timeout.h"
#include <exceptions.h>
#include <TxRxHelper.h>

#include "Modem.h"
#include "Hayes.h"
#include "FSK.h"
#include "v29.h"
#include "v32.h"
#include "v34.h"
#include "fax.h"

#define MAXSTRLEN 256

static float rowtab[4] = {697.0, 770.0, 852.0, 941.0};
static float coltab[4] = {1209.0, 1336.0, 1477.0, 1633.0};

// rxloop
//
// This routine runs until rxloopstop becomes non zero.
// This loop must always be ahead in processing data from the incoming bit
// buffer. Hence, getasync() may block to wait for the next buffer to be
// filled and is expected.  modem.putChar() never blocks but may
// throw a StdoutBufferOverflowException
static void slowrxloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->fsk->initrx_fsk(modem->modemOptions->veemode);
  while (modem->rxloopstop == 0) {
    int ch = modem->fsk->getasync(); /* get char from phone line */

    if (!modem->escaped) {
      modem->putChar(ch); /* to stdout */
    }
  }
  modem->rxloopstop = 0;
}

static void fastrxloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->v32->initrx(); /* initialize and handshake */

  while (modem->rxloopstop == 0) {
    int ch = modem->v32->getasync(); /* get char from 'phone line */
    if (!modem->escaped) {
      modem->putChar(ch); /* to stdout */
    }
  }
}

// This is not for any real modem mode, just a rx loop for our own
// v29 rx mode.  v29 is used for fax
static void v29rxloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->v29->initrx_v29(); /* initialize and handshake */

  while (modem->rxloopstop == 0) {
    int ch = modem->v29->getasync(); /* get char from 'phone line */
    if (!modem->escaped) {
      modem->putChar(ch); /* to stdout */
    }
  }
}

static void v8rxloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->fsk->initrx_fsk(ModemOptions::V21o);
  u64bit m1 = modem->getmsg();
  int n = 0;
  while (n < 5 && modem->v8rxloopstop == 0) {
    u64bit m2 = modem->getmsg();
    if (m1 == m2)
      n++;
    else
      n = 0;
    m1 = m2;
  }
  if (modem->v8rxloopstop == 0) {
    modem->menu = m1;
    // modem->infomsg("got menu begin");
    while (modem->v8rxloopstop == 0) {
      modem->fsk->getasync();
    }
  }
  modem->v8rxloopstop = 0;
}

static void zoomrxloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->v34->initrx();
}

static void zoomtxloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->v34->inittx();
}

// txloop
//
// This routine runs until txloopstop becomes non zero.
// This loop must always be ahead in producing data for the outgoing bit
// buffer.  Hence, modem.getChar() never blocks even if there is no character
// to send (NOCHAR is returned).  putasync() may block until the sound
// card is ready to receive the next buffer of output bits.
static void slowtxloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->fsk->inittx_fsk(modem->modemOptions->veemode);
  while (modem->txloopstop == 0) {
    int ch = modem->getChar();

    modem->fsk->putasync(ch); /* to phone line */
  }
  modem->txloopstop = 0;
}

void fasttxloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->v32->inittx(); /* initialize and handshake */

  while (modem->txloopstop == 0) {
    int ch = modem->getChar(); /* from stdin */
    modem->v32->putasync(ch);  /* to 'phone line */
  }
}

// This is not for any real modem mode, just a tx loop for our own
// v29 tx mode.  v29 is used for fax
void v29txloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->v29->inittx_v29(); /* initialize and handshake */

  while (modem->txloopstop == 0) {
    int ch = modem->getChar(); /* from stdin */
    modem->v29->putasync(ch);  /* to 'phone line */
  }
}

static void v8txloop(void* data) {
  Modem* modem = (Modem*)data;

  modem->fsk->inittx_fsk(ModemOptions::V21o);
  uint m = modem->computemodes(modem->modemOptions->veemode);
  u64bit msg = ((u64bit)0xe0c1 << 48) | ((u64bit)m << 24);
  // modem->infomsg("txloop before putmenu");
  while (modem->menu == 0 && modem->v8txloopstop == 0)
    modem->putmenu(msg);
  // modem->infomsg("txloop after putmenu");

  if (!modem->samemsg(modem->menu, msg)) {
    // modem->giveup("Negotiation failed (V.8)");
    modem->progressResult = Modem::NOT_FOUND;
  } else {
    modem->progressResult = Modem::FOUND;
    modem->fsk->putasync(0);
    modem->fsk->putasync(0);
    modem->fsk->putasync(0); /* send CJ */
    modem->sendpause(0.075);
  }
  modem->samplingDevice->flush();
  // tell v8rxloop to stop
  modem->v8rxloopstop = 1;
}

Modem::Modem(SamplingDevice* samplingDevice,
             TerminalDevice* terminalDevice,
             ModemOptions* modemOptions) {
  this->samplingDevice = samplingDevice;
  this->modemOptions = modemOptions;
  this->terminalDevice = terminalDevice;
  this->fsk = new FSK(this);
  this->v29 = new v29G(this);
  this->v32 = new v32G(this);
  this->v34 = new v34G(this);
  this->fax = new Fax(this);
  this->rxloopstop = 0;
  this->txloopstop = 0;
  this->v8rxloopstop = 0;
  this->v8txloopstop = 0;
  this->progressrxloopstop = 0;
  this->progresstxloopstop = 0;
  this->modemState = INITIALIZING;
  this->hayes = new Hayes(this);
  this->plusses = 0;
  this->lastCharTime = 0;
  this->escaped = false;

  setConstructorRegister(reg_echo, 0);
  setConstructorRegister(reg_escape_character, '+');
  setConstructorRegister(reg_escape_code_guard_time, 50);
  setConstructorRegister(reg_X, 2);
  setConstructorRegister(reg_mode, 0);
  setConstructorRegister(reg_v8, 0);
  setConstructorRegister(reg_send_answer, 1);
  setConstructorRegister(reg_wait_conn_tone, 1);

  // temporary for testing
  setRegister(reg_mode, 0);            // slow
  setRegister(reg_X, 0);               // NO WAIT DIALTONE
  setRegister(reg_wait_conn_tone, 0);  // NO WAIT CONNECT TONE
  setRegister(reg_send_answer, 0);     // NO SEND ANSWER TONE
  setRegister(reg_v8, 0);              // NO V8 NEGOTIATE
  setRegister(reg_echo, 1);            // turn on echo
}

Modem::~Modem() {
  delete this->hayes;
}

int Modem::open() {
  if (samplingDevice->dopen() < 0) {
    return -1;
  }
  terminalDevice->dopen();
  return 0;
}

void Modem::close() {
  samplingDevice->dclose();
  terminalDevice->dclose();
}

void Modem::putChar(int ch) {
  terminalDevice->outc(ch);
}

void Modem::putChars(char* str) {
  if (str == NULL)
    return;
  int i = 0;
  while (str[i] != '\0') {
    putChar(str[i]);
    i++;
  };
}

void Modem::newLine() {
  putChars("\n\r");
}

int Modem::getChar() {
  int ch = terminalDevice->inc();
  return onlineInterpretChar(ch);
}

int Modem::getCharSynch() {
  return terminalDevice->incsynch();
}

int Modem::onlineInterpretChar(int ch) {
  long now;

  struct timeval tp;
  gettimeofday(&tp, NULL);
  now = (long)(((tp.tv_sec) * 1000 + tp.tv_usec / 1000.0) + 0.5);

  if (!escaped) {
    if (ch != NOCHAR) {
      if (ch == getRegister(reg_escape_character)) {
        if (plusses == 0 &&
            now - lastCharTime >=
                getRegister(reg_escape_code_guard_time) * 20) {
          plusses++;
        } else if (plusses == 1 &&
                   now - lastCharTime <
                       getRegister(reg_escape_code_guard_time) * 20) {
          plusses++;
        } else if (plusses == 2 &&
                   now - lastCharTime <
                       getRegister(reg_escape_code_guard_time) * 20) {
          plusses++;
        } else {
          plusses = 0;
        }
      } else {
        plusses = 0;
      }
      lastCharTime = now;
    } else {
      if (now - lastCharTime >= getRegister(reg_escape_code_guard_time) * 20) {
        if (plusses == 3) {
          hayes->ok();
          escaped = true;
        }
        plusses = 0;
      }
    }
  } else {
    if (ch != NOCHAR) {
      if (registers[reg_echo] != 0) {
        if (ch == 13) {
          putChar('\n');
        }
        putChar(ch);
      }

      Hayes::Transition transition = hayes->interpret(ch);

      switch (transition) {
        case Hayes::HANGUP:
          escaped = false;
          stopmodem();
          break;
        case Hayes::ONLINE:
          escaped = false;
          break;
        default:
          break;
      }
    }

    // While we are escaped, we produce no chars
    ch = NOCHAR;
  }

  return ch;
}

void Modem::start() {
  while (modemState != SHUTDOWN) {
    switch (modemState) {
      case INITIALIZING:
        doInitializingState();
        break;
      case DISCONNECTED:
        doDisconnectedState();
        break;
      case CONNECTED:
        doConnectedState();
        break;
      case ANSWERING:
        doAnsweringState();
        break;
      case DIALING:
        doDialingState();
        break;
      case SHUTDOWN:
        doShutdownState();
        break;
      default:
        break;
    }
  }
}

void Modem::doInitializingState() {
  // Give producer consumer threads a chance to start
  // !!! Replace this with a signal in open routines so we know when to move to
  // next state
  usleep(1000 * 1000);

  // Move to next state
  modemState = DISCONNECTED;
}

void Modem::doDisconnectedState() {
  // Put line on hook
  samplingDevice->onHook();

  TxRxHelper* helper = new TxRxHelper(samplingDevice);
  helper->resumeReading();
  helper->resumeWriting();

  do {
    int c = getCharSynch();
    if (registers[reg_echo] != 0) {
      if (c == 13) {
        putChar('\n');
      }
      putChar(c);
    }

    Hayes::Transition transition = hayes->interpret(c);

    switch (transition) {
      case Hayes::SHUTDOWN:
        modemState = SHUTDOWN;
        break;
      case Hayes::ANSWER:
        modemState = ANSWERING;
        break;
      case Hayes::DIAL:
        modemState = DIALING;
        break;
      default:
        break;
    }

  } while (modemState == DISCONNECTED);

  helper->stopReading();
  helper->stopWriting();
}

void Modem::doAnsweringState() {
  int mode = getRegister(reg_mode);
  switch (mode) {
    case 0:
      modemOptions->veemode = ModemOptions::V21a;
      break;
    case 3:
      modemOptions->veemode = ModemOptions::V23a;
      break;
    case 97:
      // Not a real modem mode.
      modemOptions->veemode = ModemOptions::E01a;
      break;
    case 98:
      // Not a real modem mode.
      modemOptions->veemode = ModemOptions::V29a;
      break;
    case 99:
      modemOptions->veemode = ModemOptions::V29a;
      break;
    default:
      // no other modes we implement support answer
      hayes->error();
      modemState = DISCONNECTED;
  }

  // Take line off hook
  samplingDevice->offHook();

  TxRxHelper* helper = new TxRxHelper(samplingDevice);
  helper->resumeReading();
  bool interrupted = getRegister(reg_send_answer) == 1 ? sendanswer() : false;
  helper->stopReading();

  if (interrupted) {
    hayes->nocarrier();
    modemState = DISCONNECTED;
  } else {
    hayes->connect();
    modemState = CONNECTED;
  }
}

void Modem::doDialingState() {
  int mode = getRegister(reg_mode);
  switch (mode) {
    case 0:
      modemOptions->veemode = ModemOptions::V21o;
      break;
    case 3:
      modemOptions->veemode = ModemOptions::V23o;
      break;
    case 9:
      modemOptions->veemode = ModemOptions::V32o;
      break;
    case 11:
      modemOptions->veemode = ModemOptions::V34o;
      break;
    case 97:
      // Not a real modem mode.
      modemOptions->veemode = ModemOptions::E01o;
      break;
    case 98:
      // Not a real modem mode.
      modemOptions->veemode = ModemOptions::V29o;
      break;
    case 99:
      modemOptions->veemode = ModemOptions::V29o;
      break;
    default:
      // no other modes we implement support originate
      hayes->error();
      modemState = DISCONNECTED;
      return;
  }

  // Take line off hook
  samplingDevice->offHook();

  if (registers[reg_X] >= 2) {
    int result = waitfortone(DIAL_TONE);
    if (result != FOUND) {
      if (result == KEY_PRESSED) {
        hayes->nocarrier();
      } else {
        hayes->nodialtone();
      }
      modemState = DISCONNECTED;
      return;
    }
  }

  TxRxHelper* helper = new TxRxHelper(samplingDevice);
  helper->resumeReading();
  bool interrupted = dialnumber(hayes->num);
  helper->stopReading();

  if (interrupted) {
    hayes->nocarrier();
    modemState = DISCONNECTED;
    return;
  }

  if (getRegister(reg_wait_conn_tone) != 0) {
    Progress progress = waitfortone(CONN_TONE);
    if (progress == FOUND) {
      hayes->connect();
      modemState = CONNECTED;
    } else if (progress == BUSY) {
      hayes->busy();
      modemState = DISCONNECTED;
    } else if (progress == UNOBTAINABLE) {
      hayes->unobtainable();
      modemState = DISCONNECTED;
    } else if (progress == KEY_PRESSED) {
      hayes->nocarrier();
      modemState = DISCONNECTED;
    } else {
      hayes->noanswer();
      modemState = DISCONNECTED;
    }
  } else {
    // Go right to connected state
    hayes->connect();
    modemState = CONNECTED;
  }
}

void Modem::doConnectedState() {
  // int contime = time(NULL);

  // V.8 procedures
  if (getRegister(reg_v8 != 0)) {
    if (!startsession()) {
      hayes->nocarrier();
      modemState = DISCONNECTED;
      return;
    }
  }

  int mode = getRegister(reg_mode);

  if (mode == 0 || mode == 3) {
    becomeSlowModem();
  } else if (mode == 9) {
    becomeFastModem();
  } else if (mode == 11) {
    becomeZoomModem();
  } else if (mode == 97) {
    becomeExperimentalModem1();
  } else if (mode == 98) {
    becomeExperimentalModem2();
  } else if (mode == 99) {
    fax->initdoc();
    if (isOriginator()) {
      fax->readdoc();
    }

    becomeFaxModem();

    if (!isOriginator()) {
      fax->writedoc();
    }
  }

  hayes->nocarrier();
  modemState = DISCONNECTED;
}

void Modem::doShutdownState() {
  // Nothing to do...
}

/* send V.25 answer sequence   */
bool Modem::sendanswer() {
  // silence for 2.15 sec
  if (sendpause(2.15)) {
    return true;
  }
  SineGen* sgen = new SineGen(2100.0);
  for (int i = 0; i < 7; i++) {
    // 2100 Hz for 450 ms
    int ns = (int)(0.45 * SAMPLERATE);
    for (int j = 0; j < ns; j++) {
      if (getChar() != NOCHAR) {
        return true;
      }
      float val = sgen->fnext();
      samplingDevice->outsample(val);
    }

    // flip phase for next segment
    sgen->flipphase();
  }
  delete sgen;
  // silence for 75 ms
  if (sendpause(0.075)) {
    return true;
  }

  return false;
}

bool Modem::dialnumber(char* telno) {
  return senddtmf(telno);
}

void Modem::collapse(char* ovec, char* s1, char* s2) {
  int n1 = strlen(s1), n2 = strlen(s2);
  if (strncmp(ovec, s1, n1) == 0) {
    char nvec[MAXSTRLEN + 1];
    strcpy(nvec, s2);
    strcpy(&nvec[n2], &ovec[n1]);
    strcpy(ovec, nvec);
  }
}

bool Modem::senddtmf(char* s) {
  int k = 0;
  until(s[k] == '\0') {
    char* dstr = "123A456B789C*0#D";
    char* p = strchr(dstr, s[k++]);
    unless(p == NULL) {
      int n = p - dstr;
      /* tone for 100ms */
      if (sendfreqs(rowtab[n >> 2], coltab[n & 3], 0.1)) {
        return true;
      }
      /* silence for 100ms */
      if (sendpause(0.1)) {
        return true;
      }
    }
  }
  samplingDevice->flush();
  return false;
}

// Flips the tx and rx loop flags so tx and rx threads will exit gracefully
void Modem::stopmodem() {
  txloopstop = 1;
  rxloopstop = 1;
  progresstxloopstop = 1;
  progressrxloopstop = 1;
  v8rxloopstop = 1;
}

void Modem::becomeZoomModem() {
  v34->mstate = 0;

  thread_t rx = thread_create(zoomrxloop, this, "zoomrx");
  thread_t tx = thread_create(zoomtxloop, this, "zoomtx");

  thread_run(rx);
  thread_run(tx);

  thread_join(rx);
  thread_join(tx);

  thread_destroy(rx);
  thread_destroy(tx);

  printf("fast done\n");
}

void Modem::becomeFastModem() {
  v32->mstate = 0;
  v32->canceler = new canceller(0.01);

  thread_t rx = thread_create(fastrxloop, this, "fastrx");
  thread_t tx = thread_create(fasttxloop, this, "fasttx");

  thread_run(rx);
  thread_run(tx);

  thread_join(rx);
  thread_join(tx);

  thread_destroy(rx);
  thread_destroy(tx);

  printf("fast done\n");
}

void Modem::becomeSlowModem() {
  // fsk mode
  samplingDevice->setduplex(SAMPLERATE / 5);  // 0.2 secs

  thread_t rx = thread_create(slowrxloop, this, "rx");
  thread_t tx = thread_create(slowtxloop, this, "tx");

  thread_run(rx);
  thread_run(tx);

  thread_join(rx);
  thread_join(tx);

  thread_destroy(rx);
  thread_destroy(tx);
}

void Modem::becomeExperimentalModem1() {
  // was v29rx/tx
  thread_t rx = thread_create(slowrxloop, this, "rx");
  thread_t tx = thread_create(slowtxloop, this, "tx");

  thread_run(rx);
  thread_run(tx);

  thread_join(rx);
  thread_join(tx);

  thread_destroy(rx);
  thread_destroy(tx);
}

void Modem::becomeExperimentalModem2() {
  thread_t rx = thread_create(v29rxloop, this, "rx");
  thread_t tx = thread_create(v29txloop, this, "tx");

  thread_run(rx);
  thread_run(tx);

  thread_join(rx);
  thread_join(tx);

  thread_destroy(rx);
  thread_destroy(tx);
}

void Modem::becomeFaxModem() {
  fax->reset();
  if (isOriginator()) {
    fax->senddoc();
  } else {
    fax->receivedoc();
  }
}

static void v8timeout(void* data) {
  Modem* modem = (Modem*)data;
  modem->v8txloopstop = 1;
}

bool Modem::startsession() {
  menu = 0;
  samplingDevice->setduplex(SAMPLERATE / 5); /* 0.2 secs */
  progressResult = NOT_FOUND;

  thread_t txco = thread_create(v8txloop, this, "txco");
  thread_t rxco = thread_create(v8rxloop, this, "rxco");
  timeout_t timer = timeout_create(v8timeout, this, 10);

  thread_run(txco);
  thread_run(rxco);

  thread_join(txco);
  thread_join(rxco);

  timeout_cancel(timer);

  thread_destroy(txco);
  thread_destroy(rxco);
  timeout_destroy(timer);

  return progressResult == FOUND;

  // infomsg("V.8 negotiation O.K.");
}

void Modem::putmenu(u64bit msg) {
  // fprintf(stderr, ">>> %08x %08x\r\n", (uint) (msg >> 32), (uint) msg);
  fsk->putasync(-1); /* 10 `1' bits */
  while (msg) {
    fsk->putasync(msg >> 56);
    msg <<= 8;
  }
  // infomsg("putmenu over");
}

/* work out modulation octets, given vmode */
uint Modem::computemodes(ModemOptions::vmode m) {
  switch (m) {
    case ModemOptions::V21o:
      return 0x051090;
    case ModemOptions::V23o:
      return 0x051014;
    case ModemOptions::V32o:
      return 0x051110;
    case ModemOptions::V34o:
      return 0x451010;
    default:
      return 0x051010;
  }
}

bool Modem::samemsg(u64bit m1, u64bit m2) {
  norm(m1);
  norm(m2);
  return (m1 == m2);
}

void Modem::norm(u64bit& m) {
  until((m == 0) || (m & 0xff)) m >>= 8; /* position at bottom of word */
  while ((m & 0xff) == 0x10)
    m >>= 8; /* discard extension octets with no bits set */
}

u64bit Modem::getmsg() {
  int b;
  u64bit msg = 0;
  do
    b = getbyte();
  until(b == 0xe0 || v8rxloopstop != 0); /* look for sync word */
  do {
    msg = (msg << 8) | b;
    b = getbyte();
  }
  until(b == 0xe0 || v8rxloopstop != 0);
  until(msg >> 56) msg <<= 8;
  // fprintf(stderr, "<<< %08x %08x\r\n", (uint) (msg >> 32), (uint) msg);
  return msg;
}

int Modem::getbyte() {
  int b;
  do
    b = fsk->getasync();
  while (b < 0 && v8rxloopstop == 0); /* ignore timeouts */
  return b;
}

bool Modem::sendfreqs(float f1, float f2, float t) /* send 2 tones */
{
  SineGen* sgen1 = new SineGen(f1);
  SineGen* sgen2 = new SineGen(f2);
  int ns = (int)(t * SAMPLERATE);
  for (int i = 0; i < ns; i++) {
    if (getChar() != NOCHAR) {
      return true;
    }
    float val = (sgen1->fnext()) + (sgen2->fnext());
    samplingDevice->outsample(val);
  }
  delete sgen1;
  delete sgen2;
  return false;
}

bool Modem::sendfreq(float f, float t) /* send a single tone */
{
  SineGen* sgen = new SineGen(f);
  int ns = (int)(t * SAMPLERATE);
  for (int i = 0; i < ns; i++) {
    if (getChar() != NOCHAR) {
      return true;
    }
    float val = sgen->fnext();
    samplingDevice->outsample(val);
  }
  delete sgen;
  return false;
}

bool Modem::sendpause(float t) /* silence */
{
  int ns = (int)(t * SAMPLERATE);
  for (int i = 0; i < ns; i++) {
    if (getChar() != NOCHAR) {
      return true;
    }
    samplingDevice->outsample(0.0);
  }
  return false;
}

void Modem::giveup(char* msg, ...) {
  char buf[1024];
  char* s;
  char* fmt = msg;
  int d;
  char c;
  va_list vaArgs;

  strcpy(buf, msg);

  va_start(vaArgs, msg);

  int state = 0;
  while (*fmt) {
    switch (*fmt++) {
      case '%':
        state++;
      // no break
      case 's':
        state++;
        if (state == 2) {
          s = va_arg(vaArgs, char*);
          sprintf(buf, msg, s);
          break;
        }
      // no break
      case 'd':
        state++;
        if (state == 2) {
          d = va_arg(vaArgs, int);
          sprintf(buf, msg, d);
          break;
        }
      // no break
      case 'c':
        state++;
        if (state == 2) {
          c = va_arg(vaArgs, int);
          sprintf(buf, msg, c);
          break;
        }
      // no break
      default:
        state = 0;
    }
  }
  va_end(vaArgs);
  fprintf(stderr, "%s\r\n", buf);
  exit(1);
}

void Modem::infomsg(char* msg, ...) {
  char buf[1024];
  char* s;
  char* fmt = msg;
  int d;
  char c;
  va_list vaArgs;

  strcpy(buf, msg);

  va_start(vaArgs, msg);

  int state = 0;
  while (*fmt) {
    switch (*fmt++) {
      case '%':
        state++;
      // no break
      case 's':
        if (state == 1) {
          s = va_arg(vaArgs, char*);
          sprintf(buf, msg, s);
          state = 0;
          break;
        }
      // no break
      case 'd':
        if (state == 1) {
          d = va_arg(vaArgs, int);
          sprintf(buf, msg, d);
          state = 0;
          break;
        }
      // no break
      case 'c':
        if (state == 1) {
          c = va_arg(vaArgs, int);
          sprintf(buf, msg, c);
          state = 0;
          break;
        }
      // no break
      default:
        state = 0;
    }
  }
  va_end(vaArgs);
  fprintf(stderr, "%s\r\n", buf);
}

void Modem::setConstructorRegister(int reg, int value) {
  registers[reg] = value;
  registerVisible[reg] = true;
}

void Modem::setRegister(int reg, int value) {
  registers[reg] = value;
}

int Modem::getRegister(int reg) {
  return registers[reg];
}

bool Modem::isOriginator() {
  switch (modemOptions->veemode) {
    case ModemOptions::V21o:
    case ModemOptions::V23o:
    case ModemOptions::V29o:
    case ModemOptions::V32o:
    case ModemOptions::V34o:
    case ModemOptions::E01o:
      return true;
    default:
      return false;
  }
}

bool Modem::isRegisterVisible(int reg) {
  // Only registers we initialized in contructor are visible
  return registerVisible[reg];
}
