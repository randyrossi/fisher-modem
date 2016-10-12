#ifndef FAX_H
#define FAX_H

#define MAXFRAME 252
#define MAXMESSAGE (MAXFRAME + 4) /* frame plus address, control, checksum */
#define MAXPAGES 50
#define VERSION 1

#include <TxRxHelper.h>

class Fax {
 private:
  Modem* modem;
  SamplingDevice* samplingDevice;
  TxRxHelper* helper;

  // from fax.C
  int numpages, pagecount, state, rxval, framelen, msgendtime, scanbits;
  bool isfinal;
  uchar frame[MAXFRAME];
  uchar identframe[21], dcsframe[4];

  // from doc.C
  FILE* g3file;
  int pageptrs[MAXPAGES]; /* ptr to start of page in file */
  int pagebits[MAXPAGES]; /* num. of bits in each page	*/

 public:
  Fax(Modem* m) {
    modem = m;
    samplingDevice = m->samplingDevice;
    helper = new TxRxHelper(samplingDevice);
  }

  ~Fax() {
    helper->stopReading();
    helper->stopWriting();
  }

  // from fax.C
  void reset();
  void senddoc();
  void makedcs(uchar*);
  void sendtraining();
  void receivedoc();
  void getdcs(bool);
  void checkdcs(uchar*);
  bool gettraining();
  void makeident(uchar);
  void getmessage();
  void printident();
  void getmsg();
  void nextval();
  void msgerror(char*, word = 0, word = 0, word = 0);
  void sendframe(uchar*, int, bool);
  void printframe(char*, uchar*, int);
  char* frametype(uchar);
  bool csumok(uchar*, int);
  ushort computecsum(uchar*, int);
  void setstate(int);

  // from doc.C
  void initdoc();
  void readdoc();
  void sendpage(int pn, int scanbits);
  void receivepage(int pn);
  void writedoc();
};

#endif
