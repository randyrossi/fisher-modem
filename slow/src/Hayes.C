#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include "Hayes.h"
#include "Modem.h"

Hayes::Hayes(Modem* modem) {
  this->modem = modem;
  hayesState = SCAN;
}

Hayes::Transition Hayes::interpret(int c) {
  if (c == NOCHAR) {
    return NONE;
  }
  switch (hayesState) {
    case SCAN:
      accumulatorPos = 0;
      if (c == 'a' || c == 'A') {
        hayesState = SAW_A;
      }
      break;
    case SAW_A:
      if (c == 't' || c == 'T') {
        hayesState = SAW_AT;
      } else {
        hayesState = SCAN;
      }
      break;
    case SAW_AT:
      if (c == 13) {
        accumulator[accumulatorPos++] = (char)0;
        hayesState = SCAN;

        return parseAttention();
        break;
      } else if (c == 8) {
        accumulatorPos--;
        if (accumulatorPos < 0) {
          accumulatorPos = 0;
          hayesState = SAW_A;
        }
      } else {
        accumulator[accumulatorPos++] = (char)c;
      }
      break;
    default:
      break;
  }

  return NONE;
}

Hayes::Transition Hayes::parseAttention() {
  int pos;
  int len;
  char *q, *e;
  Transition transition = NONE;
  bool sayError = false;

  for (unsigned int i = 0; i < strlen(accumulator); i++) {
    accumulator[i] = toupper(accumulator[i]);
  }

  char* command = strtok(accumulator, " ");

  while (command != NULL) {
    len = strlen(command);
    if (len == 0) {
      return transition;
    }

    switch (command[0]) {
      case 0:
        break;
      case '~':
        transition = SHUTDOWN;
        break;
      case 'A':
        if (modem->modemState == Modem::DISCONNECTED) {
          transition = ANSWER;
        } else {
          sayError = true;
        }
        break;
      case 'O':
        if (modem->modemState == Modem::CONNECTED) {
          transition = ONLINE;
        } else {
          sayError = true;
        }
        break;
      case 'D':
        pos = 0;
        for (unsigned int i = 1; i < strlen(command) && pos < 32; i++) {
          if (i == 1 && command[i] == 'T') {
            continue;
          }
          if (i == 1 && command[i] == 'P') {
            continue;
          }
          num[pos++] = command[i];
        }
        num[pos++] = '\0';
        transition = DIAL;
        break;
      case 'H':
        transition = HANGUP;
        break;
      case 'M':
      case 'L':
      case 'Q':
      case 'Z':
        break;
      case 'E':
        switch (command[1]) {
          case '0':
            if (command[2] == 0) {
              modem->setRegister(reg_echo, 0);
            } else {
              sayError = true;
            }
            break;
          case '1':
            if (command[2] == 0) {
              modem->setRegister(reg_echo, 1);
            } else {
              sayError = true;
            }
            break;
          case 0:
            modem->setRegister(reg_echo, 0);
            break;
          default:
            sayError = true;
            break;
        }
        break;

      case 'V':
        switch (command[1]) {
          case '0':
            if (command[2] == 0) {
              modem->setRegister(reg_verbal, 0);
            } else {
              sayError = true;
            }
            break;
          case '1':
            if (command[2] == 0) {
              modem->setRegister(reg_verbal, 1);
            } else {
              sayError = true;
            }
            break;
          case 0:
            modem->setRegister(reg_verbal, 0);
            break;
          default:
            sayError = true;
            break;
        }
        break;

      case 'X':
        switch (command[1]) {
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
            if (command[2] == 0) {
              modem->setRegister(reg_X, '0' - command[1]);
            } else {
              sayError = true;
            }
            break;
          case 0:
            modem->setRegister(reg_X, 0);
            break;
          default:
            sayError = true;
            break;
        }
        break;
      case 'S':
        q = strchr(command, '?');
        e = strchr(command, '=');
        if (e != NULL) {
          int reg, val;
          if (sscanf(command, "S%d=%d", &reg, &val) == 2) {
            if (reg < 256 && reg >= 0) {
              modem->setRegister(reg, val);
            } else {
              sayError = true;
            }
          } else {
            sayError = true;
          }
        } else if (q != NULL) {
          int reg;
          if (sscanf(command, "S%d?", &reg) == 1) {
            if (reg >= 0 && reg < 256) {
              char tmp[32];
              sprintf(tmp, "S[%d]=%d", reg, modem->getRegister(reg));
              modem->putChars(tmp);
              modem->newLine();
            } else {
              sayError = true;
            }
          } else {
            sayError = true;
          }
        } else {
          sayError = true;
        }
        break;
      case '&':
        if (len == 2 && command[1] == 'V') {
          char tmp[32];
          int cnt = 0;
          for (int reg = 0; reg < 256; reg++) {
            if (modem->isRegisterVisible(reg)) {
              sprintf(tmp, "S[%d]=%d ", reg, modem->getRegister(reg));
              modem->putChars(tmp);
              cnt++;
              if (cnt != 0 && cnt % 4 == 0) {
                modem->newLine();
              }
            }
          }
        } else if (command[1] == 'C') {
          // ignore
        } else {
          sayError = true;
        }
        break;
      case '+':
        if (len >= 3 && command[1] == 'M' && command[2] == 'S') {
          if (len == 4 && command[3] == '?') {
            // show current
            char cur[32];
            int mode = modem->getRegister(reg_mode);
            int v8 = modem->getRegister(reg_v8);
            sprintf(cur, "+MS: %d,%d", mode, v8);
            modem->putChars(cur);
            modem->newLine();
          } else if (len >= 4 && command[3] == '=') {
            if (modem->modemState != Modem::DISCONNECTED) {
              sayError = true;  // can't change mode unless disconnected
              break;
            }
            int mode;
            int v8;
            if (sscanf(command, "+MS=%d,%d", &mode, &v8) < 2) {
              sayError = true;
              break;
            }
            if (v8 < 0 || v8 > 1) {
              sayError = true;
              break;
            }
            switch (mode) {
              case 11:
                v8 = 1;  // v.34 requires v8
              case 0:
              case 3:
              case 9:
              case 96:
              case 97:
              case 98:
              case 99:
                modem->setRegister(reg_mode, mode);
                modem->setRegister(reg_v8, v8);
                break;
              default:
                sayError = true;
            }

          } else {
            sayError = true;
          }
        }
        break;

      default:
        sayError = true;
        break;
    }

    command = strtok(NULL, " ");
  }

  if (sayError) {
    error();
  } else {
    if (modem->modemState == Modem::CONNECTED && transition == HANGUP) {
      // say nothing
    } else if (modem->modemState == Modem::CONNECTED &&
               transition == SHUTDOWN) {
      // must be offline to shutdown
      error();
    } else if (modem->modemState == Modem::DISCONNECTED && transition == DIAL) {
      // say nothing
    } else if (modem->modemState == Modem::DISCONNECTED &&
               transition == ANSWER) {
      // say nothing
    } else {
      ok();
    }
  }

  return transition;
}

void Hayes::ok() {
  modem->putChars("OK");
  modem->newLine();
}

void Hayes::error() {
  modem->putChars("ERROR");
  modem->newLine();
}

void Hayes::connect() {
  modem->putChars("CONNECT");
  modem->newLine();
}

void Hayes::nocarrier() {
  modem->putChars("NO CARRIER");
  modem->newLine();
}

void Hayes::nodialtone() {
  modem->putChars("NO DIALTONE");
  modem->newLine();
}

void Hayes::noanswer() {
  modem->putChars("NO ANSWER");
  modem->newLine();
}

void Hayes::busy() {
  modem->putChars("BUSY");
  modem->newLine();
}

void Hayes::unobtainable() {
  modem->putChars("UNOBTAINABLE");
  modem->newLine();
}
