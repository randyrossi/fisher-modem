/* Modem for MIPS   AJF	  January 1995
 Send a document (T.4), in Group 3 format on stdin */

#include <stdio.h>
#include <string.h>

#include "Modem.h"
#include "v29.h"
#include "fax.h"

void Fax::initdoc() {
  g3file = tmpfile();
  if (g3file == NULL)
    modem->giveup("can't create temporary file");
}

void Fax::readdoc() { /* Copy document from stdin to g3file */
  modem->infomsg("Reading input...");
  FILE* docFP = fopen("doc/TestFax.g3", "r");
  if (docFP == NULL) {
    modem->giveup("can't open fax");
  }
  int vsn, xres, yres;
  int ni = fscanf(docFP, "!<Group 3> %d %d %d\n", &vsn, &xres, &yres);
  unless(ni == 3 && vsn == VERSION && xres == 200 &&
         (yres == 100 || yres == 200))
      modem->giveup("input is not in Group 3 format");
  if (yres == 200)
    modem->modemOptions->options |= opt_H;
  numpages = 0;
  int ch = fgetc(docFP);
  while (ch >= 0 && numpages < MAXPAGES) {
    pageptrs[numpages] = ftell(g3file);
    int nbits = 0;
    /* Transfer one page from stdin to g3file */
    uint prevbits = ~0;
    int neols = 0;
    while (neols < 5) {
      if (ch < 0)
        goto l; /* ignore (we hope, short) partial page */
      uchar c = ch;
      for (int j = 0; j < 8 && neols < 5; j++) {
        prevbits = (prevbits << 1) | (c >> 7);
        c <<= 1;
        nbits++;
        /* 6 consecutive EOLs mark end of page; note that 2 consec. EOLs never
         * occur in body of page */
        if ((prevbits & 0xffffff) == 0x001001)
          neols++;
      }
      putc(ch, g3file);
      ch = fgetc(docFP);
    }
    pagebits[numpages++] =
        nbits - 72; /* don't count the RTC in the bit count */
  }
l:
  if (ch >= 0)
    modem->giveup("sorry, too many pages");
  if (fflush(g3file) != 0)
    modem->giveup("write error on temp file");
  fclose(docFP);
}

void Fax::sendpage(int pn,
                   int scanbits) { /* Copy one page from g3file to V.29 */
  if (modem->modemOptions->options & opt_v)
    fprintf(stderr, ">>> Page %d\n", pn);
  if (pn < 1 || pn > numpages)
    modem->giveup("bug: bad page page number: %d", pn);
  fseek(g3file, pageptrs[pn - 1], 0);
  int nb = 0, lb = 0;
  uint prevbits = ~0;
  while (nb < pagebits[pn - 1]) {
    uchar c = getc(g3file);
    for (int j = 0; j < 8 && nb < pagebits[pn - 1]; j++) {
      int b = c >> 7;
      prevbits = (prevbits << 1) | b;
      if ((prevbits & 0xfff) == 0x001) /* EOL */
      {
        while (lb++ < scanbits)
          modem->v29->putbit(0); /* pad scan line to achieve min. scan time */
        lb = 0;
      }
      modem->v29->putbit(b);
      lb++;
      nb++;
      c <<= 1;
    }
  }
  while (lb++ < scanbits)
    modem->v29->putbit(0);    /* pad final scan line */
  for (int i = 0; i < 6; i++) /* send RTC */
  {
    for (int j = 0; j < 11; j++)
      modem->v29->putbit(0);
    modem->v29->putbit(1);
  }
  for (int i = 0; i < 50; i++)
    modem->v29->putbit(0); /* flush buffer */
}

void Fax::receivepage(int pn) { /* Copy one page from V.29 to g3file */
  if (modem->modemOptions->options & opt_v)
    fprintf(stderr, "<<< Page %d\n", pn);
  if (pn < 1)
    modem->giveup("bug: bad page page number: %d", pn);
  uint prevbits = ~0;
  int neols = 0;
  while (neols < 5) {
    uchar n = 0;
    int nb = 0;
    while (nb < 8 && neols < 5) {
      int b = modem->v29->getbit();
      prevbits = (prevbits << 1) | b;
      n = (n << 1) | b;
      nb++;
      /* 6 consecutive EOLs mark end of page; note that 2 consec. EOLs never
       * occur in body of page */
      if ((prevbits & 0xffffff) == 0x001001)
        neols++;
    }
    while (nb < 8) {
      n <<= 1;
      nb++;
    }
    putc(n, g3file);
  }
  if (fflush(g3file) != 0)
    modem->giveup("write error on temp file");
}

void Fax::writedoc() { /* Copy document from g3file to stdout */
  modem->infomsg("Writing output...");
  FILE* receivedFP = fopen("ReceivedFax.g3", "w");
  fprintf(receivedFP, "!<Group 3> %d %d %d\n", VERSION, 200,
          (modem->modemOptions->options & opt_H) ? 200 : 100);
  rewind(g3file);
  int ch = getc(g3file);
  while (ch >= 0) {
    fputc(ch, receivedFP);
    ch = getc(g3file);
  }
  if (fflush(receivedFP) != 0)
    modem->giveup("write error on stdout");
  fclose(receivedFP);
}
