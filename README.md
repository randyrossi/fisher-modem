#FisherSoftModem v1.0

##What is this?

This is a rewrite of Dr. Tony Fisher's software modem for Linux. All the functionality of the original software modem has been ported and several more features have been added.

##About Tony Fisher

Tony Fisher was a professor of computer science at York University where he taught courses on embedded microcomputer systems design, digital signal processing, and data communications. Unfortunately, he died at age 43 from cancer. A tribute to Tony Fisher can be found here:

	http://www-users.cs.york.ac.uk/~fisher/tribute.html

The original source may be found here:

	http://www-users.cs.york.ac.uk/~fisher/modem

A cached copy of the above URLs are found in the 'fisher' directory in case they disappear from the internet.

##What's Changed

For those familiar with Dr. Fisher's original software modem project, here is a summary of what has changed:

* Co-routines were replaced by threads.  I didnt want to rely on a co-routine library so I used the pthread library instead.  For the most part it was possible for producer routines to run concurrently with consumers. However, sometimes the original code was written in such a way so that it relied on the two ping-ponging back and forth. In those cases, I maintained that behaviour using semaphores.  I also made sure that reset functionality remained in tact as this was important for certain modes.

* All modes are consolidated into one executable.

* Instead of taking command line arguments, the program behaves like a Hayes compatible modem.  It will accept AT commands to dial/answer/change registers etc.  Of course, not all commands are available but most of what you need is there.  A complete list of supported AT commands is listed below.

* I added the ability to create a psuedo terminal device (like /dev/pts/#) so a conventional terminal program can use this as though it were a real modem device.  Any program expecting Hayes commands should work (like minicom or PPP scripting).  By default, stdin/stdout is used unless a tty device name is given at startup.
	  Example: modem -term /dev/ptmx

* The destination device is abstracted as a SamplingDevice so other types of input/output devices can be plugged in.  The three supported types (so far) are 'dsp' (for libsoundalsa), 'mempipe[1|2]' (for shared memory) and 'bluetooth'.  The shared memory device lets you test both ends of a connection without actually using a real device but only works for the modes that implement both originate and answer.

* The sampling devices use a double buffered approach for both sound production and consumption.  This ensures a steady supply of unbroken data as long as the bit producer/consumer threads are fast enough to keep up (which they usually are on modern day computers).

* I made the sample rate configurable.  It was hard coded at 9600 and there were other parts of the code that were ultimately tied to this (like the shapetab arrays in the v.29/v.32/v.34 implementations).  These are now generated based on the sample rate specified in the top level Makefile.  However, I have not tested much with rates other than 9600 and since the default rate works just fine there is little reason to change it.

* I've attempted to get rid of all global variables and make as much of the code object oriented as possible so that multiple modems may be instantiated within one process.   This is not essential for the time being but eventually Id like the software to be able to do this.  I'm sure by making the code more object oriented I've slowed things down a bit but computers are fast these days and seem to have no trouble keeping up with production/consumption.  I typically see less than 2% CPU utilization when I run the modem.

* Some of the support programs (fifi/mkfilter/mkshape) were modified slightly to help with the build process.

##What Works

* V.21 originate/answer modes, 300 bps full duplex
* V.23 originate/answer modes, 1200 bps down, 75 bps up
* V.29 fax send/receive at 9600 bps

##What Doesn't Work

* V.32 originate

I'm not sure if this mode ever worked.  It gets up to MSTATE=2 but never seems to read the rates its expecting.  It is difficult to test since it requires a real modem answering.

* V.34 originate

This mode emits tones but I don't think it was ever finished by Dr. Fisher. The handshake sequence may be done but there is no data mode.

##What Can I Do With This?

You can:

* Learn something about digital signal processing

* Establish a point to point link between two computers through their sound cards.

* (Possibly) establish a point to point link between a computer and a real modem using a sound card.  This is tricky since getting a computer to play to a phone line and record from a phone line is difficult.  I have been successful with V21 and V23 modes.

##AT Command Set

As mentioned earlier, the command line flags were removed and replaced with a small subset of Hayes commands. Only some very basic AT commands are supported.

    ATA        - answer
    ATD        - dial (tone only)
    ATO        - online (from escape)
    ATX#       - 0=don't wait for dial tone before dialing, 1=wait 
    ATE#       - echo on (1) or off (0)
    ATS#=#     - set register

    Registers:
      2 = escape character (default '+')
      12 = escape code guard time (default 50ms)

    ATS#?      - query a register
    AT&V       - show all registers
    ATH        - hangup
    AT+MS=#,#  - set modem mode (mode,v8)

    where mode can be one of:
             0 = .v21 @ 300 bps
             3 = .v23 @ 1200 bps down, 75 bps up
             9 = .v32 @ 9600 (originate only)
            11 = .v34 (unfinished)
            97 = Experimental1 V23 FSK 1200 bps full duplex
            98 = Experimental2 V29 QAM 9600 bps full duplex 
            99 = .v29 fax mode

    and v8 can be one of
             0 = don't use v8
             1 = use v8

###NOTES

1. There is no 'auto' mode like on real modems.  You must set the mode you want to use for the session and the other end must match (or be capable of falling back to your mode).

2. There is no carrier detection so if the other end disappears after having established a connection, the modem will remain in originate/answer mode and spit out garbage until you hang up.

##Experimental Modes

A sound card's output does not feedback into its input like a real phone line.  This means we don't have to worry about things like echo cancellation or making sure the send frequencies are not shared by the receiving frequencies.

Mode 97 uses the same FSK modulation for the 1200 bps downstream communication used in .v23 but for both directions giving 1200 bps full duplex.

Mode 98 uses the same QAM modulation for the 9600 bps half duplex fax mode but uses it for both directions simultaneously for full duplex.  You have to put the modem in originate mode on both ends for this to work (i.e. ATDT####)

These modes would not work over a phone line but might work across a soundcard connection, for example.

##Fax

Sending and receiving a fax works. You can test this locally by using the shared mem pipe device.  The code is currently hard coded to send the fax located in doc/TestFax.g3 and receive it as ReceivedFax.g3 in the current directory.

    shell1:
      modem -dev mempipe1
      at+MS=99,0
      atdt1234

    shell2:
      modem -dev mempipe2
      at+MS=99,0
      ata

If you want to view .g3 files, use mgetty-viewfax:

    sudo apt-get install mgetty-viewfax
    viewfax ReceivedFax.g3

