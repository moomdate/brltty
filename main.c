/*
 * BRLTTY - A background process providing access to the Linux console (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2001 by The BRLTTY Team. All rights reserved.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

/*
 * main.c - Main processing loop plus signal handling
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "brl.h"
#include "spk.h"
#include "scr.h"
#include "tunes.h"
#include "cut.h"
#include "misc.h"
#include "message.h"
#include "config.h"
#include "common.h"
#include "route.h"

int refreshInterval = REFRESH_INTERVAL;
int messageDelay = MESSAGE_DELAY;

/*
 * Misc param variables
 */
short dispmd = LIVE_SCRN;        /* freeze screen on/off */
short infmode = 0;                /* display screen image or info */
short hwinshift;                /* Half window horizontal distance */
short fwinshift;                /* Full window horizontal distance */
short vwinshift;                /* Window vertical distance */
Preferences prefs;                /* environment (i.e. global) parameters */
brldim brl;                        /* For the Braille routines */
ScreenDescription scr;                        /* For screen state infos */
unsigned int TickCount = 0;        /* incremented each main loop cycle */


/*
 * useful macros
 */

#define BRL_ISUPPER(c) \
        (isupper (c) || (c) == '@' || (c) == '[' || (c) == '^' || \
         (c) == ']' || (c) == '\\')

#define TOGGLEPLAY(var)  playTune((var) ? &tune_toggle_on : &tune_toggle_off)
#define TOGGLE(var) \
        (var = (keypress & VAL_SWITCHON) \
               ? 1 : ((keypress & VAL_SWITCHOFF) \
                      ? 0 : (!var)))


unsigned char *curtbl = texttrans;        /* active translation table */

static void
setTranslationTable (int attributes) {
  curtbl = attributes? attribtrans: texttrans;
}


/* 
 * Structure definition for volatile screen state variables.
 */
typedef struct {
  short column;
  short row;
} ScreenMark;
typedef struct {
  short trackCursor;		/* tracking mode */
  short hideCursor;		/* For temporarily hiding cursor */
  short showAttributes;		/* text or attributes display */
  short winx, winy;	/* upper-left corner of braille window */
  short motx, moty;	/* user motion of braille window */
  short trkx, trky;	/* tracked cursor position */
  ScreenMark marks[0X100];
} ScreenState;
static ScreenState initialScreenState = {
  INIT_CSRTRK, INIT_CSRHIDE, 0,
  0, 0, 0, 0, 0, 0,
  {[0X00 ... 0XFF]={0, 0}}
};

/* 
 * Array definition containing pointers to ScreenState structures for 
 * each screen.  Each structure is dynamically allocated when its 
 * screen is used for the first time.
 * Screen 0 is reserved for the help screen; its structure is static.
 */
#define MAX_SCR 0X3F              /* actual number of separate screens */
static ScreenState scr0;        /* at least one is statically allocated */
static ScreenState *scrparam[MAX_SCR+1] = {&scr0, };
static ScreenState *p = &scr0;        /* pointer to current state structure */
static int curscr;                        /* current screen number */

static void
switchto (unsigned int scrno) {
  curscr = scrno;
  if (scrno > MAX_SCR)
    scrno = 0;
  if (!scrparam[scrno]) {         /* if not already allocated... */
    {
      if (!(scrparam[scrno] = malloc(sizeof(*p))))
        scrno = 0;         /* unable to allocate a new structure */
      else
        *scrparam[scrno] = initialScreenState;
    }
  }
  p = scrparam[scrno];
  setTranslationTable(p->showAttributes);
}

void
clearStatusCells (void) {
   memset(statcells, 0, sizeof(statcells));
   braille->setstatus(statcells);
}

static void
setStatusCells (void) {
   memset(statcells, 0, sizeof(statcells));
   switch (prefs.stcellstyle) {
      case ST_AlvaStyle:
         if ((dispmd & HELP_SCRN) == HELP_SCRN) {
            statcells[0] = texttrans['h'];
            statcells[1] = texttrans['l'];
            statcells[2] = texttrans['p'];
         } else {
            /* The coords are given with letters as the DOS tsr */
            statcells[0] = ((TickCount / 16) % (scr.posy / 25 + 1))? 0:
                           texttrans[scr.posy % 25 + 'a'] |
                           ((scr.posx / brl.x) << 6);
            statcells[1] = ((TickCount / 16) % (p->winy / 25 + 1))? 0:
                           texttrans[p->winy % 25 + 'a'] |
                           ((p->winx / brl.x) << 6);
            statcells[2] = texttrans[(p->showAttributes)? 'a':
                                      ((dispmd & FROZ_SCRN) == FROZ_SCRN)? 'f':
                                      p->trackCursor? 't':
                                      ' '];
         }
         break;
      case ST_TiemanStyle:
         statcells[0] = (portraitDigits[(p->winx / 10) % 10] << 4) |
                        portraitDigits[(scr.posx / 10) % 10];
         statcells[1] = (portraitDigits[p->winx % 10] << 4) |
                        portraitDigits[scr.posx % 10];
         statcells[2] = (portraitDigits[(p->winy / 10) % 10] << 4) |
                        portraitDigits[(scr.posy / 10) % 10];
         statcells[3] = (portraitDigits[p->winy % 10] << 4) |
                        portraitDigits[scr.posy % 10];
         statcells[4] = ((dispmd & FROZ_SCRN) == FROZ_SCRN? 1: 0) |
                        (prefs.csrvis << 1) |
                        (p->showAttributes << 2) |
                        (prefs.csrsize << 3) |
                        (prefs.sound << 4) |
                        (prefs.csrblink << 5) |
                        (p->trackCursor << 6) |
                        (prefs.slidewin << 7);
         break;
      case ST_PB80Style:
         statcells[0] = (portraitDigits[(p->winy+1) % 10] << 4) |
                        portraitDigits[((p->winy+1) / 10) % 10];
         break;
      case ST_Generic:
         statcells[FirstStatusCell] = FSC_GENERIC;
         statcells[STAT_brlcol] = p->winx+1;
         statcells[STAT_brlrow] = p->winy+1;
         statcells[STAT_csrcol] = scr.posx+1;
         statcells[STAT_csrrow] = scr.posy+1;
         statcells[STAT_scrnum] = scr.no;
         statcells[STAT_tracking] = p->trackCursor;
         statcells[STAT_dispmode] = p->showAttributes;
         statcells[STAT_frozen] = (dispmd & FROZ_SCRN) == FROZ_SCRN;
         statcells[STAT_visible] = prefs.csrvis;
         statcells[STAT_size] = prefs.csrsize;
         statcells[STAT_blink] = prefs.csrblink;
         statcells[STAT_capitalblink] = prefs.capblink;
         statcells[STAT_dots] = prefs.sixdots;
         statcells[STAT_sound] = prefs.sound;
         statcells[STAT_skip] = prefs.skpidlns;
         statcells[STAT_underline] = prefs.attrvis;
         statcells[STAT_blinkattr] = prefs.attrblink;
         break;
      case ST_MDVStyle:
         statcells[0] = portraitDigits[((p->winy+1) / 10) % 10] |
                        (portraitDigits[((p->winx+1) / 10) % 10] << 4);
         statcells[1] = portraitDigits[(p->winy+1) % 10] |
                        (portraitDigits[(p->winx+1) % 10] << 4);
         break;
      case ST_VoyagerStyle: /* 3cells (+1 blank) */
         statcells[0] = (portraitDigits[p->winy % 10] << 4) |
                        portraitDigits[(p->winy / 10) % 10];
         statcells[1] = (portraitDigits[scr.posy % 10] << 4) |
                        portraitDigits[(scr.posy / 10) % 10];
         if ((dispmd & FROZ_SCRN) == FROZ_SCRN)
            statcells[2] = texttrans['F'];
         else
            statcells[2] = (portraitDigits[scr.posx % 10] << 4) |
                           portraitDigits[(scr.posx / 10) % 10];
         break;
      default:
         break;
   }
   braille->setstatus(statcells);
}

void
setStatusText (const unsigned char *text) {
   int i;
   memset(statcells, 0, sizeof(statcells));
   for (i=0; i<sizeof(statcells); ++i) {
      unsigned char character = text[i];
      if (!character) break;
      statcells[i] = texttrans[character];
   }
   braille->setstatus(statcells);
}

static void
showInfo (void)
{
  /* Here we must be careful. Some displays (e.g. Braille Lite 18)
   * are very small, and others (e.g. Bookworm) are even smaller.
   */
  unsigned char infbuf[22];
  setStatusText("info");
  if (brl.x*brl.y >= 21) {
    sprintf(infbuf, "%02d:%02d %02d:%02d %02d %c%c%c%c%c%c",
            p->winx, p->winy, scr.posx, scr.posy, curscr, 
            p->trackCursor? 't': ' ',
            prefs.csrvis? (prefs.csrblink? 'B': 'v'):
                        (prefs.csrblink? 'b': ' '),
            p->showAttributes? 'a': 't',
            ((dispmd & FROZ_SCRN) == FROZ_SCRN)? 'f': ' ',
            prefs.sixdots? '6': '8',
            prefs.capblink? 'B': ' ');
    message(infbuf, MSG_SILENT|MSG_NODELAY);
  } else {
    int i;
    sprintf(infbuf, "xxxxx %02d %c%c%c%c%c%c     ",
            curscr,
            p->trackCursor? 't': ' ',
            prefs.csrvis? (prefs.csrblink? 'B' : 'v'):
                        (prefs.csrblink? 'b': ' '),
            p->showAttributes? 'a': 't',
            ((dispmd & FROZ_SCRN) == FROZ_SCRN) ?'f': ' ',
            prefs.sixdots? '6': '8',
            prefs.capblink? 'B': ' ');
    infbuf[0] = portraitDigits[(p->winx / 10) % 10] << 4 |
                portraitDigits[(scr.posx / 10) % 10];
    infbuf[1] = portraitDigits[p->winx % 10] << 4 | portraitDigits[scr.posx % 10];
    infbuf[2] = portraitDigits[(p->winy / 10) % 10] << 4 |
                portraitDigits[(scr.posy / 10) % 10];
    infbuf[3] = portraitDigits[p->winy % 10] << 4 | portraitDigits[scr.posy % 10];
    infbuf[4] = (((dispmd & FROZ_SCRN) == FROZ_SCRN)? B1: 0) |
                (p->showAttributes?    B2: 0) |
                (prefs.sound?    B3: 0) |
                (prefs.csrvis?   B4: 0) |
                (prefs.csrsize?  B5: 0) |
                (prefs.csrblink? B6: 0) |
                (p->trackCursor?      B7: 0) |
                (prefs.slidewin? B8: 0);

    /* We have to do the Braille translation ourselves, since
     * we don't want the first five characters translated ...
     */
    for (i=5; infbuf[i]; i++)
      infbuf[i] = texttrans[infbuf[i]];

    memcpy(brl.disp, infbuf, brl.x*brl.y);
    braille->write(&brl);
  }
}

static void
handleSignal (int number, void (*handler) (int))
{
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  action.sa_handler = handler;
  if (sigaction(number, &action, NULL) == -1) {
    LogError("signal set");
  }
}

static void
exitLog (void) {
  /* Reopen syslog (in case -e closed it) so that there will
   * be a "stopped" message to match the "starting" message.
   */
  LogOpen(0);
  LogPrint(LOG_INFO, "Terminated.");
  LogClose();
}

static void
exitScreenParameters (void) {
  int i;
  /* don't forget that scrparam[0] is staticaly allocated */
  for (i = 1; i <= MAX_SCR; i++) 
    free(scrparam[i]);
}

static void
terminateProgram (int quickly) {
  int silently = quickly || (strcmp(speech->name, "NoSpeech") == 0);
  clearStatusCells();
  message("BRLTTY exiting.", 
          MSG_NODELAY | (silently? MSG_SILENT: 0));
  if (!silently) {
    int awaitSilence = speech->isSpeaking();
    int i;
    for (i=0; i<messageDelay; i+=refreshInterval) {
      delay(refreshInterval);
      if (braille->read(CMDS_MESSAGE) != EOF) break;
      if (awaitSilence) {
        speech->processSpkTracking();
        if (!speech->isSpeaking()) break;
      }
    }
  }
  exit(0);
}

static void 
terminationHandler (int signalNumber) {
  terminateProgram(signalNumber == SIGINT);
}

static void 
childDeathHandler (int signalNumber)
{
  pid_t pid = wait(NULL);
  if (pid == csr_pid) {
    csr_pid = 0;
    csr_active--;
  }
}

static void 
slideWindowVertically (int y)
{
  if (y < p->winy)
    p->winy = y;
  else if  (y >= (p->winy + brl.y))
    p->winy = y - (brl.y - 1);
}

static void 
placeWindowHorizontally (int x)
{
  p->winx = x / brl.x * brl.x;
}

static void 
trackCursor (int place)
{
  if (prefs.slidewin) {
    int reset = brl.x * 3 / 10;
    int trigger = prefs.eager_slidewin? brl.x*3/20: 0;
    if (place)
      if ((scr.posx < p->winx) || (scr.posx >= (p->winx + brl.x)) ||
          (scr.posy < p->winy) || (scr.posy >= (p->winy + brl.y)))
        placeWindowHorizontally(scr.posx);
    if (scr.posx < (p->winx + trigger))
      p->winx = MAX(scr.posx-reset, 0);
    else if (scr.posx >= (p->winx + brl.x - trigger))
      p->winx = MIN(scr.posx+reset+1, scr.cols) - brl.x;
  } else
    placeWindowHorizontally(scr.posx);
  slideWindowVertically(scr.posy);
}

static int speaking_start_line = 0;
static void
trackSpeech (int inx)
{
  placeWindowHorizontally(inx % scr.cols);
  slideWindowVertically((inx / scr.cols) + speaking_start_line);
}

static int
upDifferentLine(short mode) {
   if (p->winy > 0) {
      char buffer1[scr.cols], buffer2[scr.cols];
      int skipped = 0;
      if (mode==SCR_TEXT && p->showAttributes)
         mode = SCR_ATTRIB;
      readScreen((ScreenBox){0, p->winy, scr.cols, 1},
                 buffer1, mode);
      do {
         readScreen((ScreenBox){0, --p->winy, scr.cols, 1},
                    buffer2, mode);
         if (memcmp(buffer1, buffer2, scr.cols) ||
             (mode==SCR_TEXT && prefs.csrvis && p->winy==scr.posy))
            return 1;

         /* lines are identical */
         if (skipped == 0)
            playTune(&tune_skip_first);
         else if (skipped <= 4)
            playTune(&tune_skip);
         else if (skipped % 4 == 0)
            playTune(&tune_skip_more);
         skipped++;
      } while (p->winy > 0);
   }

   playTune(&tune_bounce);
   return 0;
}

static int
downDifferentLine(short mode) {
   if (p->winy < (scr.rows - brl.y)) {
      char buffer1[scr.cols], buffer2[scr.cols];
      int skipped = 0;
      if (mode==SCR_TEXT && p->showAttributes)
         mode = SCR_ATTRIB;
      readScreen((ScreenBox){0, p->winy, scr.cols, 1},
                  buffer1, mode);
      do {
         readScreen((ScreenBox){0, ++p->winy, scr.cols, 1},
                    buffer2, mode);
         if (memcmp(buffer1, buffer2, scr.cols) ||
             (mode==SCR_TEXT && prefs.csrvis && p->winy==scr.posy))
            return 1;

         /* lines are identical */
         if (skipped == 0)
            playTune(&tune_skip_first);
         else if (skipped <= 4)
            playTune(&tune_skip);
         else if (skipped % 4 == 0)
            playTune(&tune_skip_more);
         skipped++;
      } while (p->winy < (scr.rows - brl.y));
   }

   playTune(&tune_bounce);
   return 0;
}

static void
upLine(short mode) {
   if (prefs.skpidlns)
      upDifferentLine(mode);
   else if (p->winy > 0)
      p->winy--;
   else
      playTune(&tune_bounce);
}

static void
downLine(short mode) {
   if (prefs.skpidlns)
      downDifferentLine(mode);
   else if (p->winy < (scr.rows - brl.y))
      p->winy++;
   else
      playTune(&tune_bounce);
}


static int
insertCharacter (unsigned char character, int flags) {
  if (islower(character)) {
    if (flags & (VPC_SHIFT | VPC_UPPER)) character = toupper(character);
  } else if (flags & VPC_SHIFT) {
    switch (character) {
      case '1': character = '!'; break;
      case '2': character = '@'; break;
      case '3': character = '#'; break;
      case '4': character = '$'; break;
      case '5': character = '%'; break;
      case '6': character = '^'; break;
      case '7': character = '&'; break;
      case '8': character = '*'; break;
      case '9': character = '('; break;
      case '0': character = ')'; break;
      case '-': character = '_'; break;
      case '=': character = '+'; break;
      case '[': character = '{'; break;
      case ']': character = '}'; break;
      case'\\': character = '|'; break;
      case ';': character = ':'; break;
      case'\'': character = '"'; break;
      case '`': character = '~'; break;
      case ',': character = '<'; break;
      case '.': character = '>'; break;
      case '/': character = '?'; break;
    }
  }
  if (flags & VPC_CONTROL) {
    if ((character & 0X6F) == 0X2F)
      character |= 0X50;
    else
      character &= 0X9F;
  }
  if (flags & VPC_META) {
    if (prefs.metamode)
      character |= 0X80;
    else
      if (!insertKey(KEY_ESCAPE)) return 0;
  }
  return insertKey(character);
}


int
main (int argc, char *argv[])
{
  int keypress;                        /* character received from braille display */
  int i;                        /* loop counter */
  short csron = 1;                /* display cursor on (toggled during blink) */
  short csrcntr = 1;
  short capon = 0;                /* display caps off (toggled during blink) */
  short capcntr = 1;
  short attron = 0;
  short attrcntr = 1;
  short oldwinx, oldwiny;
  short speaking_scrno = -1, speaking_prev_inx = -1;

  /* Open the system log. */
  LogOpen(0);
  LogPrint(LOG_INFO, "Starting.");
  atexit(exitLog);

  /* Initialize global data assumed to be ready by the termination handler. */
  *p = initialScreenState;
  scrparam[0] = p;
  for (i = 1; i <= MAX_SCR; i++)
    scrparam[i] = NULL;
  curscr = 0;
  atexit(exitScreenParameters);
  
  /* We install SIGPIPE handler before startup() so that speech drivers which
   * use pipes can't cause program termination (the call to message() in
   * startup() in particular).
   */
  handleSignal(SIGPIPE, SIG_IGN);

  /* Install the program termination handler. */
  initializeBraille();
  initializeSpeech();
  handleSignal(SIGTERM, terminationHandler);
  handleSignal(SIGINT, terminationHandler);

  /* Install the handler which monitors the death of child processes. */
  handleSignal(SIGCHLD, childDeathHandler);

  /* Setup everything required on startup */
  startup(argc, argv);

  describeScreen(&scr);
  /* NB: screen size can sometimes change, f.e. the video mode may be changed
   * when installing a new font. Will be detected by another call to
   * describeScreen in the main loop. Don't assume that scr.rows
   * and scr.cols are constants across loop iterations.
   */
  switchto(scr.no);                        /* allocate current screen params */
  p->trkx = scr.posx; p->trky = scr.posy;
  trackCursor(1);        /* set initial window position */
  p->motx = p->winx; p->moty = p->winy;
  oldwinx = p->winx; oldwiny = p->winy;

  /*
   * Main program loop 
   */
  while (1) {
    /* The braille display can stick out by brl.x-offr columns from the
     * right edge of the screen.
     */
    short offr = scr.cols % brl.x;
    if (!offr) offr = brl.x;

    closeTuneDevice(0);
    TickCount++;
    /*
     * Process any Braille input 
     */
    while ((keypress = braille->read(infmode? CMDS_STATUS:
                                     ((dispmd & HELP_SCRN) == HELP_SCRN)? CMDS_HELP:
                                     CMDS_SCREEN)) != EOF) {
      int oldmotx = p->winx;
      int oldmoty = p->winy;
      LogPrint(LOG_DEBUG, "Command: %5.5X", keypress);
      if (!executeScreenCommand(keypress)) {
        switch (keypress & ~VAL_SWITCHMASK) {
          case CMD_NOOP:        /* do nothing but loop */
            continue;
          case CMD_RESTARTBRL:
            stopBrailleDriver();
            playTune(&tune_braille_off);
            LogPrint(LOG_INFO, "Reinitializing braille driver.");
            startBrailleDriver();
            break;
          case CMD_RESTARTSPEECH:
            stopSpeechDriver();
            LogPrint(LOG_INFO, "Reinitializing speech driver.");
            startSpeechDriver();
            break;
          case CMD_TOP:
            p->winy = 0;
            break;
          case CMD_TOP_LEFT:
            p->winy = 0;
            p->winx = 0;
            break;
          case CMD_BOT:
            p->winy = scr.rows - brl.y;
            break;
          case CMD_BOT_LEFT:
            p->winy = scr.rows - brl.y;
            p->winx = 0;
            break;
          case CMD_WINUP:
            if (p->winy == 0)
              playTune (&tune_bounce);
            p->winy = MAX (p->winy - vwinshift, 0);
            break;
          case CMD_WINDN:
            if (p->winy == scr.rows - brl.y)
              playTune (&tune_bounce);
            p->winy = MIN (p->winy + vwinshift, scr.rows - brl.y);
            break;
          case CMD_FWINLT:
            if (!(prefs.skpblnkwins && (prefs.skpblnkwinsmode == sbwAll))) {
              int oldX = p->winx;
              if (p->winx > 0) {
                p->winx = MAX(p->winx-fwinshift, 0);
                if (prefs.skpblnkwins) {
                  if (prefs.skpblnkwinsmode == sbwEndOfLine)
                    goto skipEndOfLine;
                  if (!prefs.csrvis ||
                      (scr.posy != p->winy) ||
                      (scr.posx >= (p->winx + brl.x))) {
                    int charCount = MIN(scr.cols, p->winx+brl.x);
                    int charIndex;
                    char buffer[charCount];
                    readScreen((ScreenBox){0, p->winy, charCount, 1},
                               buffer, SCR_TEXT);
                    for (charIndex=0; charIndex<charCount; ++charIndex)
                      if ((buffer[charIndex] != ' ') && (buffer[charIndex] != 0))
                        break;
                    if (charIndex == charCount)
                      goto wrapUp;
                  }
                }
                break;
              }
            wrapUp:
              if (p->winy == 0) {
                playTune(&tune_bounce);
                p->winx = oldX;
                break;
              }
              playTune(&tune_wrap_up);
              p->winx = MAX((scr.cols-offr)/fwinshift*fwinshift, 0);
              upLine(SCR_TEXT);
            skipEndOfLine:
              if (prefs.skpblnkwins && (prefs.skpblnkwinsmode == sbwEndOfLine)) {
                int charIndex;
                char buffer[scr.cols];
                readScreen((ScreenBox){0, p->winy, scr.cols, 1},
                           buffer, SCR_TEXT);
                for (charIndex=scr.cols-1; charIndex>=0; --charIndex)
                  if ((buffer[charIndex] != ' ') && (buffer[charIndex] != 0))
                    break;
                if (prefs.csrvis && (scr.posy == p->winy))
                  charIndex = MAX(charIndex, scr.posx);
                charIndex = MAX(charIndex, 0);
                if (charIndex < p->winx)
                  p->winx = charIndex / fwinshift * fwinshift;
              }
              break;
            }
          case CMD_FWINLTSKIP: {
            int oldX = p->winx;
            int oldY = p->winy;
            int tuneLimit = 3;
            int charCount;
            int charIndex;
            char buffer[scr.cols];
            while (1) {
              if (p->winx > 0) {
                p->winx = MAX(p->winx-fwinshift, 0);
              } else {
                if (p->winy == 0) {
                  playTune(&tune_bounce);
                  p->winx = oldX;
                  p->winy = oldY;
                  break;
                }
                if (tuneLimit-- > 0)
                  playTune(&tune_wrap_up);
                p->winx = MAX((scr.cols-offr)/fwinshift*fwinshift, 0);
                upLine(SCR_TEXT);
              }
              charCount = MIN(brl.x, scr.cols-p->winx);
              readScreen((ScreenBox){p->winx, p->winy, charCount, 1},
                         buffer, SCR_TEXT);
              for (charIndex=(charCount-1); charIndex>=0; charIndex--)
                if ((buffer[charIndex] != ' ') && (buffer[charIndex] != 0))
                  break;
              if (prefs.csrvis &&
                  (scr.posy == p->winy) &&
                  (scr.posx < (p->winx + charCount)))
                charIndex = MAX(charIndex, scr.posx-p->winx);
              if (charIndex >= 0) {
                if (prefs.slidewin)
                  p->winx = MAX(p->winx+charIndex-brl.x+1, 0);
                break;
              }
            }
            break;
          }
          case CMD_LNUP:
            upLine(SCR_TEXT);
            break;
          case CMD_PRDIFLN:
            upDifferentLine(SCR_TEXT);
            break;
          case CMD_ATTRUP:
            upDifferentLine(SCR_ATTRIB);
            break;
          case CMD_FWINRT:
            if (!(prefs.skpblnkwins && (prefs.skpblnkwinsmode == sbwAll))) {
              int oldX = p->winx;
              if (p->winx < (scr.cols - brl.x)) {
                p->winx = MIN(p->winx+fwinshift, scr.cols-offr);
                if (prefs.skpblnkwins) {
                  if (!prefs.csrvis ||
                      (scr.posy != p->winy) ||
                      (scr.posx < p->winx)) {
                    int charCount = scr.cols - p->winx;
                    int charIndex;
                    char buffer[charCount];
                    readScreen((ScreenBox){p->winx, p->winy, charCount, 1},
                               buffer, SCR_TEXT);
                    for (charIndex=0; charIndex<charCount; ++charIndex)
                      if ((buffer[charIndex] != ' ') && (buffer[charIndex] != 0))
                        break;
                    if (charIndex == charCount)
                      goto wrapDown;
                  }
                }
                break;
              }
            wrapDown:
              if (p->winy >= (scr.rows - brl.y)) {
                playTune(&tune_bounce);
                p->winx = oldX;
                break;
              }
              playTune(&tune_wrap_down);
              p->winx = 0;
              downLine(SCR_TEXT);
              break;
            }
          case CMD_FWINRTSKIP: {
            int oldX = p->winx;
            int oldY = p->winy;
            int tuneLimit = 3;
            int charCount;
            int charIndex;
            char buffer[scr.cols];
            while (1) {
              if (p->winx < (scr.cols - brl.x)) {
                p->winx = MIN(p->winx+fwinshift, scr.cols-offr);
              } else {
                if (p->winy >= (scr.rows - brl.y)) {
                  playTune(&tune_bounce);
                  p->winx = oldX;
                  p->winy = oldY;
                  break;
                }
                if (tuneLimit-- > 0)
                  playTune(&tune_wrap_down);
                p->winx = 0;
                downLine(SCR_TEXT);
              }
              charCount = MIN(brl.x, scr.cols-p->winx);
              readScreen((ScreenBox){p->winx, p->winy, charCount, 1},
                         buffer, SCR_TEXT);
              for (charIndex=0; charIndex<charCount; charIndex++)
                if ((buffer[charIndex] != ' ') && (buffer[charIndex] != 0))
                  break;
              if (prefs.csrvis &&
                  (scr.posy == p->winy) &&
                  (scr.posx >= p->winx))
                charIndex = MIN(charIndex, scr.posx-p->winx);
              if (charIndex < charCount) {
                if (prefs.slidewin)
                  p->winx = MIN(p->winx+charIndex, scr.cols-offr);
                break;
              }
            }
            break;
          }
          case CMD_LNDN:
            downLine(SCR_TEXT);
            break;
          case CMD_NXDIFLN:
            downDifferentLine(SCR_TEXT);
            break;
          case CMD_ATTRDN:
            downDifferentLine(SCR_ATTRIB);
            break;
          case CMD_NXBLNKLN:
          case CMD_PRBLNKLN: {
            int dir = (keypress == CMD_NXBLNKLN) ? +1 : -1;
            int i;
            int found = 0;
            int l = p->winy +dir;
            char buffer[scr.cols];
            /* look for a blank line */
            while(l>=0 && l <= scr.rows - brl.y) {
                readScreen((ScreenBox){0, l, scr.cols, 1},
                           buffer, SCR_TEXT);
                for(i=0; i<scr.cols; i++)
                  if(buffer[i] != ' ' && buffer[i] != 0)
                    break;
                if(i==scr.cols){
                  found = 1;
                  break;
                }
                l += dir;
            }
            if(found) {
              /* look for a non-blank line */
              found = 0;
              while(l>=0 && l <= scr.rows - brl.y) {
                readScreen((ScreenBox){0, l, scr.cols, 1},
                           buffer, SCR_TEXT);
                for(i=0; i<scr.cols; i++)
                  if(buffer[i] != ' ' && buffer[i] != 0)
                    break;
                if(i<scr.cols){
                  p->winy = l;
                  p->winx = 0;
                  found = 1;
                  break;
                }
                l += dir;
              }
            }
            if(!found) {
              playTune (&tune_bounce);
              playTune (&tune_bounce);
            }
            break;
          }
          case CMD_NXSEARCH:
          case CMD_PRSEARCH: {
            int dir = (keypress == CMD_NXSEARCH) ? +1 : -1;
            int found = 0, caseSens = 0;
            int l = p->winy + dir;
            char buffer[scr.cols+1];
            char *ptr;
            if(!cut_buffer || strlen(cut_buffer) > scr.cols)
              break;
            ptr = cut_buffer;
            while(*ptr)
              if(isupper(*(ptr++))) {
                caseSens = 1;
                break;
              }
            while(l>=0 && l <= scr.rows - brl.y) {
              readScreen((ScreenBox){0, l, scr.cols, 1},
                         buffer, SCR_TEXT);
              buffer[scr.cols] = 0;
              if(!caseSens) {
                ptr = buffer;
                while(*ptr) {
                  *ptr = tolower(*ptr);
                  ptr++;
                }
              }
              if((ptr = strstr(buffer, cut_buffer))) {
                p->winy = l;
                p->winx = (ptr-buffer) / brl.x * brl.x;
                found = 1;
                break;
              }
              l += dir;
            }
            if(!found) {
              playTune (&tune_bounce);
              playTune (&tune_bounce);
            }
            break;
          }
          case CMD_HOME:
            trackCursor(1);
            break;
          case CMD_BACK:
            p->winx = p->motx;
            p->winy = p->moty;
            break;
          case CMD_SPKHOME:
            if(scr.no == speaking_scrno) {
              trackSpeech(speech->track());
            } else {
              playTune(&tune_bad_command);
            }
            break;
          case CMD_LNBEG:
            p->winx = 0;
            break;
          case CMD_LNEND:
            p->winx = scr.cols - brl.x;
            break;
          case CMD_CHRLT:
            if (p->winx == 0)
              playTune (&tune_bounce);
            p->winx = MAX (p->winx - 1, 0);
            break;
          case CMD_CHRRT:
            /* here we ignore offr so as to allow off-right positions when
             * explicitely requested.
             */
            if (p->winx < (scr.cols - 1))
              p->winx++;
            else
              playTune(&tune_bounce);
            break;
          case CMD_HWINLT:
            if (p->winx == 0)
              playTune (&tune_bounce);
            p->winx = MAX (p->winx - hwinshift, 0);
            break;
          case CMD_HWINRT:
            if (p->winx >= scr.cols - offr)
              playTune (&tune_bounce);
            p->winx = MIN (p->winx + hwinshift, scr.cols - offr);
            break;
          case CMD_CSRJMP:
            if (!routeCursor(p->winx, p->winy, curscr))
              playTune(&tune_bad_command);
            break;
          case CMD_CSRJMP_VERT:
            if (!routeCursor(-1, p->winy, curscr))
              playTune(&tune_bad_command);
            break;
          case CMD_CSRVIS:
            /* toggles the preferences option that decides whether cursor
               is shown at all */
            TOGGLEPLAY ( TOGGLE(prefs.csrvis) );
            break;
          case CMD_CSRHIDE:
            /* This is for briefly hiding the cursor */
            TOGGLE(p->hideCursor);
            /* no tune */
            break;
          case CMD_ATTRVIS:
            TOGGLEPLAY ( TOGGLE(prefs.attrvis) );
            break;
          case CMD_CSRBLINK:
            csron = 1;
            TOGGLEPLAY ( TOGGLE(prefs.csrblink) );
            if (prefs.csrblink)
              {
                csrcntr = prefs.csroncnt;
                attron = 1;
                attrcntr = prefs.attroncnt;
                capon = 0;
                capcntr = prefs.capoffcnt;
              }
            break;
          case CMD_CSRTRK:
            TOGGLE(p->trackCursor);
            if (p->trackCursor) {
              if (speech->isSpeaking())
                speaking_prev_inx = -1;
              else
                trackCursor(1);
              playTune(&tune_link);
            } else
              playTune(&tune_unlink);
            break;
          case CMD_CUT_BEG:
            cut_begin (p->winx, p->winy);
            break;
          case CMD_CUT_END:
            if (!cut_rectangle(MIN(p->winx + brl.x - 1, scr.cols-1), p->winy + brl.y - 1)) {
              playTune(&tune_bad_command);
            }
            break;
          case CMD_PASTE:
            if ((dispmd & HELP_SCRN) != HELP_SCRN && !csr_active)
              if (cut_paste())
                break;
            playTune(&tune_bad_command);
            break;
          case CMD_SND:
            TOGGLEPLAY ( TOGGLE(prefs.sound) );        /* toggle sound on/off */
            break;
          case CMD_DISPMD:
            setTranslationTable(TOGGLE(p->showAttributes));
            break;
          case CMD_FREEZE:
            { unsigned char v = (dispmd & FROZ_SCRN) ? 1 : 0;
              TOGGLE(v);
              if (v) {
                dispmd = selectDisplay(dispmd | FROZ_SCRN);
                playTune(&tune_freeze);
              }else{
                dispmd = selectDisplay(dispmd & ~FROZ_SCRN);
                playTune(&tune_unfreeze);
              }
            }
            break;
          case CMD_HELP:
            { int v;
              infmode = 0;        /* ... and not in info mode */
              v = (dispmd & HELP_SCRN)? 1: 0;
              TOGGLE(v);
              if (v) {
                dispmd = selectDisplay(dispmd | HELP_SCRN);
                if (dispmd & HELP_SCRN) /* help screen selection successful */
                  {
                    switchto(0);        /* screen 0 for help screen */
                    *p = initialScreenState;        /* reset params for help screen */
                  }
                else        /* help screen selection failed */
                    message ("can't find help", 0);
              }else
                dispmd = selectDisplay(dispmd & ~HELP_SCRN);
            }
            break;
          case CMD_CAPBLINK:
            capon = 1;
            TOGGLEPLAY( TOGGLE(prefs.capblink) );
            if (prefs.capblink)
              {
                capcntr = prefs.caponcnt;
                attron = 0;
                attrcntr = prefs.attroffcnt;
                csron = 0;
                csrcntr = prefs.csroffcnt;
              }
            break;
          case CMD_ATTRBLINK:
            attron = 1;
            TOGGLEPLAY( TOGGLE(prefs.attrblink) );
            if (prefs.attrblink)
              {
                attrcntr = prefs.attroncnt;
                capon = 1;
                capcntr = prefs.caponcnt;
                csron = 0;
                csrcntr = prefs.csroffcnt;
              }
            break;
          case CMD_INFO:
            TOGGLE(infmode);
            break;
          case CMD_CSRSIZE:
            TOGGLEPLAY ( TOGGLE(prefs.csrsize) );
            break;
          case CMD_SIXDOTS:
            TOGGLEPLAY ( TOGGLE(prefs.sixdots) );
            break;
          case CMD_SLIDEWIN:
            TOGGLEPLAY ( TOGGLE(prefs.slidewin) );
            break;
          case CMD_SKPIDLNS:
            TOGGLEPLAY ( TOGGLE(prefs.skpidlns) );
            break;
          case CMD_SKPBLNKWINS:
            TOGGLEPLAY ( TOGGLE(prefs.skpblnkwins) );
            break;
          case CMD_PREFSAVE:
            if (savePreferences()) {
              playTune(&tune_done);
            }
            break;
          case CMD_PREFMENU:
            updatePreferences();
            break;
          case CMD_PREFLOAD:
            if (loadPreferences(1)) {
              csron = 1;
              capon = 0;
              csrcntr = capcntr = 1;
              playTune(&tune_done);
            }
            break;
          case CMD_SAY:
          case CMD_SAYALL:
            {
              /* OK heres a crazy idea: why not send the attributes with the
                 text, in case some inflection or marking can be added...! The
                 speech driver's say function will receive a buffer of text
                 and a length, but in reality the buffer will contain twice
                 len bytes: the text followed by the video attribs data. */
              unsigned int i;
              unsigned r = (keypress == CMD_SAYALL) ? scr.rows - p->winy :1;
              unsigned char buffer[ 2*( r *scr.cols )];
              readScreen((ScreenBox){0, p->winy, scr.cols, r},
                         buffer, SCR_TEXT);
              i = r*scr.cols;
              i--;
              while(i>=0 && buffer[i] == 0) i--;
              i++;
              if(i==0) break;
              if(speech->sayWithAttribs != NULL) {
                readScreen((ScreenBox){0, p->winy, scr.cols, r},
                           buffer+i, SCR_ATTRIB);
                speech->sayWithAttribs(buffer, i);
              }else speech->say(buffer, i);
              if ((keypress & ~VAL_SWITCHMASK) == CMD_SAYALL) {
                speaking_scrno = scr.no;
                speaking_start_line = p->winy;
              } else {
                /* 
                 * We don't want speech tracking to move the braille window 
                 * for a single line reading.
                 */ 
                speaking_scrno = -1;
              }
            }
            break;
          case CMD_MUTE:
            speech->mute();
            break;
          case CMD_SWITCHVT_PREV:
            if (!switchVirtualTerminal(scr.no-1))
              playTune(&tune_bad_command);
            break;
          case CMD_SWITCHVT_NEXT:
            if (!switchVirtualTerminal(scr.no+1))
              playTune(&tune_bad_command);
            break;
          default: {
            int key = keypress & VAL_BLK_MASK;
            int arg = keypress & VAL_ARG_MASK;
            int flags = keypress & VAL_FLG_MASK;
            switch (key) {
              case VAL_PASSKEY: {
                unsigned short key;
                switch (arg) {
                  case VPK_RETURN:
                    key = KEY_RETURN;
                    break;
                  case VPK_TAB:
                    key = KEY_TAB;
                    break;
                  case VPK_BACKSPACE:
                    key = KEY_BACKSPACE;
                    break;
                  case VPK_ESCAPE:
                    key = KEY_ESCAPE;
                    break;
                  case VPK_CURSOR_LEFT:
                    key = KEY_CURSOR_LEFT;
                    break;
                  case VPK_CURSOR_RIGHT:
                    key = KEY_CURSOR_RIGHT;
                    break;
                  case VPK_CURSOR_UP:
                    key = KEY_CURSOR_UP;
                    break;
                  case VPK_CURSOR_DOWN:
                    key = KEY_CURSOR_DOWN;
                    break;
                  case VPK_PAGE_UP:
                    key = KEY_PAGE_UP;
                    break;
                  case VPK_PAGE_DOWN:
                    key = KEY_PAGE_DOWN;
                    break;
                  case VPK_HOME:
                    key = KEY_HOME;
                    break;
                  case VPK_END:
                    key = KEY_END;
                    break;
                  case VPK_INSERT:
                    key = KEY_INSERT;
                    break;
                  case VPK_DELETE:
                    key = KEY_DELETE;
                    break;
                  default:
                    if (arg < VPK_FUNCTION) goto badKey;
                    key = KEY_FUNCTION + (arg - VPK_FUNCTION);
                    break;
                }
                if (!insertKey(key))
                badKey:
                  playTune(&tune_bad_command);
                break;
              }
              case VAL_PASSCHAR:
                if (!insertCharacter(arg, flags)) {
                  playTune(&tune_bad_command);
                }
                break;
              case VAL_PASSDOTS:
                if (!insertCharacter(untexttrans[arg], flags)) {
                  playTune(&tune_bad_command);
                }
                break;
              case CR_ROUTE:
                if (arg < brl.x)
                  if (routeCursor(MIN(p->winx+arg, scr.cols-1), p->winy, curscr))
                    break;
                playTune(&tune_bad_command);
                break;
              case CR_CUTBEGIN:
                if (arg < brl.x && p->winx+arg < scr.cols)
                  cut_begin(p->winx+arg, p->winy);
                else
                  playTune(&tune_bad_command);
                break;
              case CR_CUTAPPEND:
                if (arg < brl.x && p->winx+arg < scr.cols)
                  cut_append(p->winx+arg, p->winy);
                else
                  playTune(&tune_bad_command);
                break;
              case CR_CUTRECT:
                if (arg < brl.x && p->winx+arg < scr.cols)
                  if (cut_rectangle(MIN(p->winx+arg, scr.cols-1), p->winy))
                    break;
                playTune(&tune_bad_command);
                break;
              case CR_CUTLINE:
                if (arg < brl.x && p->winx+arg < scr.cols)
                  if (cut_line(MIN(p->winx+arg, scr.cols-1), p->winy))
                    break;
                playTune(&tune_bad_command);
                break;
              case CR_DESCCHAR:
                if (arg < brl.x && p->winx+arg < scr.cols) {
                  static char *colours[] = {
                    "black",   "red",     "green",   "yellow",
                    "blue",    "magenta", "cyan",    "white"
                  };
                  char buffer[0X40];
                  unsigned char character, attributes;
                  readScreen((ScreenBox){p->winx+arg, p->winy, 1, 1},
                             &character, SCR_TEXT);
                  readScreen((ScreenBox){p->winx+arg, p->winy, 1, 1},
                             &attributes, SCR_ATTRIB);
                  sprintf(buffer, "char %d (0x%02x): %s on %s",
                          character, character,
                          colours[attributes&0X07],
                          colours[(attributes&0X70)>>4]);
                  if (attributes & 0X08) strcat(buffer, " bright");
                  if (attributes & 0X80) strcat(buffer, " blink");
                  message(buffer, 0);
                } else
                  playTune(&tune_bad_command);
                break;
              case CR_SETLEFT:
                if (arg < brl.x && p->winx+arg < scr.cols)
                  p->winx += arg;
                else
                  playTune(&tune_bad_command);
                break;
              case CR_SETMARK: {
                ScreenMark *mark = &p->marks[arg];
                mark->column = p->winx;
                mark->row = p->winy;
                break;
              }
              case CR_GOTOMARK: {
                ScreenMark *mark = &p->marks[arg];
                p->winx = mark->column;
                p->winy = mark->row;
                break;
              }
              case CR_SWITCHVT:
                  if (!switchVirtualTerminal(arg+1))
                       playTune(&tune_bad_command);
                  break;
              {
                int increment;
              case CR_NXINDENT:
                increment = 1;
                goto find;
              case CR_PRINDENT:
                increment = -1;
              find:
                {
                  int count = MIN(p->winx+arg+1, scr.cols);
                  int found = 0;
                  int row = p->winy + increment;
                  char buffer[scr.cols];
                  while ((row >= 0) && (row <= scr.rows-brl.y)) {
                    int column;
                    readScreen((ScreenBox){0, row, count, 1},
                               buffer, SCR_TEXT);
                    for (column=0; column<count; column++)
                      if ((buffer[column] != ' ') && (buffer[column] != 0))
                        break;
                    if (column < count) {
                      found = 1;
                      p->winy = row;
                      break;
                    }
                    row += increment;
                  }
                  if (!found) {
                    playTune(&tune_bounce);
                    playTune(&tune_bounce);
                  }
                }
                break;
              }
              default:
                playTune(&tune_bad_command);
                LogPrint(LOG_WARNING, "Driver sent unrecognized command: %X", keypress);
            }
            break;
          }
        }
      }

      if ((p->winx != oldmotx) || (p->winy != oldmoty))
        { // The window has been manually moved.
          p->motx = p->winx;
          p->moty = p->winy;
        }
    }
        
    /*
     * Update blink counters: 
     */
    if (prefs.csrblink)
      if (!--csrcntr)
        csrcntr = (csron ^= 1) ? prefs.csroncnt : prefs.csroffcnt;
    if (prefs.capblink)
      if (!--capcntr)
        capcntr = (capon ^= 1) ? prefs.caponcnt : prefs.capoffcnt;
    if (prefs.attrblink)
      if (!--attrcntr)
        attrcntr = (attron ^= 1) ? prefs.attroncnt : prefs.attroffcnt;

    /*
     * Update Braille display and screen information.  Switch screen 
     * params if screen number has changed.
     */
    describeScreen(&scr);
    if (!(dispmd & (HELP_SCRN|FROZ_SCRN)) && curscr != scr.no)
      switchto(scr.no);
    /* NB: This should also accomplish screen resizing: scr.rows and
     * scr.cols may have changed.
     */
    {
      short maximum = scr.rows - brl.y;
      short *table[] = {&p->winy, &p->moty, NULL};
      short **value = table;
      while (*value) {
        if (**value > maximum) **value = maximum;
        ++value;
      }
    }
    {
      short maximum = scr.cols - 1;
      short *table[] = {&p->winx, &p->motx, NULL};
      short **value = table;
      while (*value) {
        if (**value > maximum) **value = maximum;
        ++value;
      }
    }

    /* speech tracking */
    speech->processSpkTracking(); /* called continually even if we're not tracking
                             so that the pipe doesn't fill up. */
    if(p->trackCursor && speech->isSpeaking() && scr.no == speaking_scrno) {
      int inx = speech->track();
      if(inx != speaking_prev_inx) {
        trackSpeech(speaking_prev_inx = inx);
      }
    }
    else /* cursor tracking */
    if (p->trackCursor)
      {
        /* If cursor moves while blinking is on */
        if (prefs.csrblink)
          {
            if (scr.posy != p->trky)
              {
                /* turn off cursor to see what's under it while changing lines */
                csron = 0;
                csrcntr = prefs.csroffcnt;
              }
            else if (scr.posx != p->trkx)
              {
                /* turn on cursor to see it moving on the line */
                csron = 1;
                csrcntr = prefs.csroncnt;
              }
          }
        /* If the cursor moves in cursor tracking mode: */
        if (!csr_active && (scr.posx != p->trkx || scr.posy != p->trky)) {
          trackCursor(0);
          p->trkx = scr.posx;
          p->trky = scr.posy;
        }
      }
    /* If attribute underlining is blinking during display movement */
    if(prefs.attrvis && prefs.attrblink){
      /* We could check to see if we changed screen, but that doesn't
         really matter... this is mainly for when you are hunting up/down
         for the line with attributes. */
      if(p->winx != oldwinx || p->winy != oldwiny){
        attron = 1;
        attrcntr = prefs.attroncnt;
      }
      /* problem: this still doesn't help when the braille window is
         stationnary and the attributes themselves are moving
         (example: tin). */
    }
    oldwinx = p->winx; oldwiny = p->winy;
    /* If not in info mode, get screen image: */
    if (infmode) {
      showInfo();
    } else {
      int winlen = MIN(brl.x, scr.cols -p->winx);
      readScreen((ScreenBox){p->winx, p->winy, winlen, brl.y},
                 brl.disp,
                       p->showAttributes? SCR_ATTRIB: SCR_TEXT);
      if (prefs.capblink && !capon)
        for (i = 0; i < winlen * brl.y; i++)
          if (BRL_ISUPPER (brl.disp[i]))
            brl.disp[i] = ' ';

      /*
       * Do Braille translation using current table: 
       */
      if (prefs.sixdots && curtbl != attribtrans)
        for (i = 0; i < winlen * brl.y; brl.disp[i] = \
        curtbl[brl.disp[i]] & 0x3f, i++);
      else
        for (i = 0; i < winlen * brl.y; brl.disp[i] = \
        curtbl[brl.disp[i]], i++);

      if(winlen <brl.x) {
        /* We got a rectangular piece of text with getscr. But the display
           is in an off-right position, with some cells at the end blank.
           So we'll insert these cells and blank them. */
        for(i=brl.y-1; i>0; i--)
          memmove(brl.disp +i*brl.x, brl.disp +i*winlen, winlen);
        for(i=0; i<brl.y; i++)
          memset(brl.disp +i*brl.x +winlen, 0, brl.x-winlen);
      }

      /* Attribute underlining: if viewing text (not attributes), attribute
         underlining is active and visible and we're not in help, then we
         get the attributes for the current region and OR the underline. */
      if (!p->showAttributes && prefs.attrvis && (!prefs.attrblink || attron)) {
        int x,y;
        unsigned char attrbuf[winlen*brl.y];
        readScreen((ScreenBox){p->winx, p->winy, winlen, brl.y},
                   attrbuf, SCR_ATTRIB);
        for(y=0; y<brl.y; y++)
          for (x=0; x<winlen; x++)
            switch (attrbuf[y*winlen + x]) {
              /* Experimental! Attribute values are hardcoded... */
              case 0x08: /* dark-gray on black */
              case 0x07: /* light-gray on black */
              case 0x17: /* light-gray on blue */
              case 0x30: /* black on cyan */
                break;
              case 0x70: /* black on light-gray */
                brl.disp[y*brl.x +x] |= ATTR1CHAR;
                break;
              case 0x0F: /* white on black */
              default:
                brl.disp[y*brl.x + x] |= ATTR2CHAR;
                break;
            };
      }

      /*
       * If the cursor is visible and in range, and help is off: 
       */
      if (prefs.csrvis && !p->hideCursor
          && (!prefs.csrblink || csron) && scr.posx >= p->winx &&
          scr.posx < p->winx + brl.x && scr.posy >= p->winy &&
          scr.posy < p->winy + brl.y)
        brl.disp[(scr.posy - p->winy) * brl.x + scr.posx - p->winx] |= \
          prefs.csrsize ? BIG_CSRCHAR : SMALL_CSRCHAR;

      setStatusCells();
      braille->write(&brl);
    }
    delay(refreshInterval);
  }

  terminateProgram(0);
  return 0;
}

void 
message (unsigned char *text, short flags) {
   int length = strlen(text);

   if (prefs.sound && !(flags & MSG_SILENT)) {
      speech->mute();
      speech->say(text, length);
   }

   if (braille && brl.disp) {
      while (length) {
         int count;
         int index;

         /* strip leading spaces */
         while (*text == ' ')  text++, length--;

         if (length <= brl.x*brl.y) {
            count = length; /* the whole message fits on the braille window */
         } else {
            /* split the message across multiple windows on space characters */
            for (count=brl.x*brl.y-2; count>0 && text[count]!=' '; count--);
            if (!count) count = brl.x * brl.y - 1;
         }

         memset(brl.disp, ' ', brl.x*brl.y);
         for (index=0; index<count; brl.disp[index++]=*text++);
         if (length -= count) {
            while (index < brl.x*brl.y) brl.disp[index++] = '-';
            brl.disp[brl.x*brl.y - 1] = '>';
         }

         /*
          * Do Braille translation using text table. * Six-dot mode is
          * ignored, since case can be important, and * blinking caps won't 
          * work ... 
          */
         for (index=0; index<brl.x*brl.y; ++index) brl.disp[index] = texttrans[brl.disp[index]];
         braille->write(&brl);

         if (flags & MSG_WAITKEY)
            while (braille->read(CMDS_MESSAGE) == EOF) delay(refreshInterval);
         else if (length || !(flags & MSG_NODELAY)) {
            int i;
            for (i=0; i<messageDelay; i+=refreshInterval) {
               delay(refreshInterval);
               if (braille->read(CMDS_MESSAGE) != EOF) break;
            }
         }
      }
   }
}
