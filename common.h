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

/*
 * struct definition of settings that are saveable.
 * ENV_MAGICNUM has to be bumped whenever the definition of
 * struct brltty_env is modified otherwise this structure could be
 * filled with incompatible data from disk.
 */
#define ENV_MAGICNUM 0x4005

struct brltty_env {
	unsigned char magicnum[2];
	unsigned char csrvis;
	unsigned char spare1;
	unsigned char attrvis;
	unsigned char spare2;
	unsigned char csrblink;
	unsigned char spare3;
	unsigned char capblink;
	unsigned char spare4;
	unsigned char attrblink;
	unsigned char spare5;
	unsigned char csrsize;
	unsigned char spare6;
	unsigned char csroncnt;
	unsigned char spare7;
	unsigned char csroffcnt;
	unsigned char spare8;
	unsigned char caponcnt;
	unsigned char spare9;
	unsigned char capoffcnt;
	unsigned char spare10;
	unsigned char attroncnt;
	unsigned char spare11;
	unsigned char attroffcnt;
	unsigned char spare12;
	unsigned char sixdots;
	unsigned char spare13;
	unsigned char slidewin;
	unsigned char spare14;
	unsigned char sound;
	unsigned char tunedev;
	unsigned char skpidlns;
	unsigned char spare15;
	unsigned char skpblnkwinsmode;
	unsigned char spare16;
	unsigned char skpblnkwins;
	unsigned char spare17;
	unsigned char stcellstyle;
	unsigned char spare18;
} __attribute__((packed));

/* 
 * struct definition for volatile parameters
 */
struct brltty_param {
	short csrtrk;		/* tracking mode */
	short csrhide;		/* For temporarily hiding cursor */
	short dispmode;		/* text or attributes display */
	short winx, winy;	/* Start row and column of Braille window */
	short cox, coy;		/* Old cursor position */
};


#define TBL_TEXT 0		/* use text translation table */
#define TBL_ATTRIB 1		/* use attribute translation table */


/*
 * Shared variables
 */

extern char VERSION[];			/* BrlTty version string */
extern char COPYRIGHT[];		/* BrlTty copyright banner */
extern volatile int keep_going;		/* zero after reception of SIGTERM */

extern struct brltty_param initparam;	/* defaults for new brltty_param */
extern struct brltty_env env;		/* current env parameters */
extern brldim brl;			/* braille driver reference */

extern short offr; /* Braille display can stick out by brl.x-offr from
		      the right edge of the screen. */
extern short fwinshift;			/* Full window horizontal distance */
extern short hwinshift;			/* Half window horizontal distance */
extern short vwinshift;			/* Window vertical distance */
extern short csr_offright;		/* used for sliding window */


/*
 * Shared functions
 */

void startup(int argc, char *argv[]);
void startbrl(void);
void loadconfig(void);
void saveconfig(void);
void configmenu(void);
void clrbrlstat(void);
