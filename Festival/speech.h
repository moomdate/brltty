/*
 * BrlTty - A daemon providing access to the Linux console (when in text
 *          mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2001 by The BrlTty Team. All rights reserved.
 *
 * BrlTty comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

/* Festival/speech.h - definitions for rudimentary speech support
 * N. Nair, 4 October 1998
 */

#define SPKNAME "Festival"

/* These sequences are sent to Festival's command mode.
 * The first byte is the length, so embedded nuls are allowed.
 */

/* To initialise Festival: */
/* #define INIT_SPEECH "\024(audio_mode 'async)\n" */
#define INIT_SPEECH "\074(audio_mode 'async)\n"\
                    "(Parameter.set 'Audio_Method 'netaudio)\n"

/* Immediately before and after speech data: */
#define PRE_SPEECH "\012(SayText \""
#define POST_SPEECH "\003\")\n"

/* To mute the speech: */
#define MUTE_SEQ "\025(audio_mode 'shutup)\n"

/* The speech data itself is translated character by character.  If a
 * character is less than 33 (i.e. space or control), it is replaced by a
 * space.  If the character is more than MAX_TRANS, it is passed through
 * as is.  Otherwise the character n is replaced by the string vocab[n - 33].
 */

#define MAX_TRANS 126
static unsigned char *vocab[MAX_TRANS - 32] =
{
  " exclamation ",
  " double quote ",
  " hash ",
  " dollar ",
  " percent ",
  " ampersand ",
  " quote ",
  " left paren ",
  " right paren ",
  " star ",
  " plus ",
  " comma ",
  " dash ",
  " dot ",
  " slash ",
  "0",
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
  "9",
  " colon ",
  " semicolon ",
  " less than ",
  " equals ",
  " greater than ",
  " question ",
  " at ",
  "A",
  "B",
  "C",
  "D",
  "E",
  "F",
  "G",
  "H",
  "I",
  "J",
  "K",
  "L",
  "M",
  "N",
  "O",
  "P",
  "Q",
  "R",
  "S",
  "T",
  "U",
  "V",
  "W",
  "X",
  "Y",
  "Z",
  " left bracket ",
  " backslash ",
  " right bracket ",
  " uparrow ",
  " underline ",
  " accent ",
  "a",
  "b",
  "c",
  "d",
  "e",
  "f",
  "g",
  "h",
  "i",
  "j",
  "k",
  "l",
  "m",
  "n",
  "o",
  "p",
  "q",
  "r",
  "s",
  "t",
  "u",
  "v",
  "w",
  "x",
  "y",
  "z",
  " left brace ",
  " bar ",
  " right brace ",
  " tilde "
};