/* Modem for MIPS   AJF	  January 1995
   Wait for tone (dial, connect, etc.) */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include <complex.h>
#include <filters.h>
#include <tonedec.h>

#include "Modem.h"

#define debug_cadence false


struct cadence_detector
  { cadence_detector()
      { pcnt = acnt = 0;	    /* sample counts */
	oncount = offcount = 0;	    /* cadence on/off counts */
      }
    void insert(bool);
    void debug();
    int oncount, offcount;
private:
    int pcnt, acnt;
  };

/* Filter coeffs constructed by:
   mkfilter -Bu -Bp -o 2 -a (A1) (A2)
   where A1 = F1 / SAMPLERATE, A2 = F2 / SAMPLERATE
   Indexed by tone; see modem.h */

static fspec *bpfspecs[] =
  { mkfilter("-Bu -Bp -o 2 -R %SAMPLERATE% -A 390 410"),        /*  390 ..  410 Hz, centre  400 Hz    [0]        */
    mkfilter("-Bu -Bp -o 2 -R %SAMPLERATE% -A 440 460"),        /*  440 ..  460 Hz, centre  450 Hz    [1] (dial) */
    mkfilter("-Bu -Bp -o 2 -R %SAMPLERATE% -A 2090 2110"),        /* 2090 .. 2110 Hz, centre 2100 Hz    [2] (conn) */
  };

static fspec *fefs = mkfilter("-Hp -Bu -o 2 -R %SAMPLERATE% -A 300");           /* 300 Hz hpf */
static fspec *lpfs = mkfilter("-Bu -Lp -o 2 -R %SAMPLERATE% -A 40");    /*  40 Hz lpf */

inline int secs(float f) { return (int) (f * SAMPLERATE); }

static void progressrxloop(void *data)
{
    Modem *modem = (Modem*) data;

    tone_detector
	*td1 = new tone_detector(fefs, bpfspecs[0], lpfs, true),
	*td2 = new tone_detector(fefs, bpfspecs[modem->progresstone == Modem::DIAL_TONE ? 1 : 2], lpfs, true);
    cadence_detector
	*cd1 = new cadence_detector(),
	*cd2 = new cadence_detector();

    int totcount = 0;

    // TODO: support forceconnect?
    while (modem->progressResult == Modem::UNKNOWN && modem->progressrxloopstop == 0)
    { 
    	int ch = modem->getChar();
    	if (ch != NOCHAR) {
    		modem->progressResult = Modem::KEY_PRESSED;
    		break;
    	}
    	
        float x = modem->samplingDevice->insample(); 
		td1 -> insert(x); cd1 -> insert(td1 -> present);
		td2 -> insert(x); cd2 -> insert(td2 -> present);
		
		if (debug_cadence && totcount%500 == 0)
		{
           td1 -> debug(); cd1 -> debug();
           td2 -> debug(); cd2 -> debug();
           putc('\n', stderr);
		}
		
		switch (modem->progresstone)
        {
            case Modem::DIAL_TONE:
			if (totcount >= secs(5.0f)) {
            	modem->progressResult = Modem::NOT_FOUND;
            } else if (td2 -> prescount > secs(1.5f)) {
				modem->progressResult = Modem::FOUND;
			}
			break;

	    	case Modem::CONN_TONE:
			if (totcount >= secs(45.0f)) {
                modem->progressResult = Modem::NOT_FOUND; 
            } else if ((td2 -> prescount > secs(2.7f)) || (cd2 -> oncount > 6)) {
                modem->progressResult = Modem::FOUND;	/* V.25, V.25 bis */
            }
			break;
		}
		
		if (modem->progressResult == Modem::UNKNOWN && td1 -> prescount >= secs(3.0f))  {
		   modem->progressResult = Modem::UNOBTAINABLE;
        }
        
		if (modem->progressResult == Modem::UNKNOWN && cd1 -> oncount >= 4 && cd1 -> offcount >= 4) {
           modem->progressResult = Modem::BUSY;
        }
        
		totcount++;
    }
    
    delete td1; delete td2; delete cd1; delete cd2;

    modem->progressrxloopstop = 0; // reset to 0
    modem->progresstxloopstop = 1; // tell tx to stop
}

static void progresstxloop(void *data)
{
   Modem *modem = (Modem*) data;
   float f = (modem->getRegister(reg_mode) == 99) ? 1100.0 : 1300.0;

   while (modem->progresstxloopstop == 0)
   {
       switch (modem->progresstone) {
       case Modem::CONN_TONE:
          /* while we wait for conn tone, send CNG or V.25 CT every 3.5 secs */
          modem->infomsg("CNG/V.25CT Beep\n\r");
          modem->sendfreq(f, 0.5f);
          if (modem->progresstxloopstop !=0 ) { break; }

          // Break up the 3.5 sec pause into .5 second chunks and check the
          // stop flag so we get a quicker reaction time to the detection
          // We will be at most .5 seconds off
          for (int i=0;i<7;i++) {
             modem->sendpause(0.5f);
             if (modem->progresstxloopstop !=0 ) { break; }
          }
          break;
       case Modem::DIAL_TONE:
          /* just output silence while we wait for a dial tone */
          modem->sendpause(0.5f);
          break;
       }
   }
   modem->progresstxloopstop = 0;

   // If we're here, we've got a result and since the other
   // end is not interested in anything we are outputting, we can chose
   // not to output what is in the sampling device's output buffer, so
   // just reset it.  This will result in a slightly quicker 'reaction' to the
   // tone detection on this end.
   modem->samplingDevice->discardOutput();
}

Modem::Progress Modem::waitfortone(Modem::Tone tone)
{
	progressResult = UNKNOWN;
    progresstone = tone;

    thread_t rx = thread_create(progressrxloop, this, "progressrx");
    thread_t tx = thread_create(progresstxloop, this, "progresstx");

    thread_run(rx);
    thread_run(tx);

    thread_join(rx);
    thread_join(tx);

    thread_destroy(rx);
    thread_destroy(tx);
    
    return progressResult;
}

void cadence_detector::insert(bool pres)
  { if (pres)
      { pcnt++;
	if (acnt >= secs(0.25f) && acnt <= secs(0.55f)) offcount++;	/* 0.25 was 0.3 */
	acnt = 0;
      }
    else
      { acnt++;
	if (pcnt >= secs(0.25f) && pcnt <= secs(0.55f)) oncount++;	/* 0.25 was 0.3 */
	pcnt = 0;
      }
  }

void cadence_detector::debug()
  { fprintf(stderr, " [%d:%d]", offcount, oncount);
  }

