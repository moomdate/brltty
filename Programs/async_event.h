/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2013 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version. Please see the file LICENSE-GPL for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#ifndef BRLTTY_INCLUDED_ASYNC_EVENT
#define BRLTTY_INCLUDED_ASYNC_EVENT

#include "async.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct AsyncEventStruct AsyncEvent;
typedef void AsyncEventCallback (void *eventData, void *signalData);

extern AsyncEvent *asyncNewEvent (AsyncEventCallback *callback, void *data);
extern void asyncDiscardEvent (AsyncEvent *event);
extern void asyncSignalEvent (AsyncEvent *event, void *data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BRLTTY_INCLUDED_ASYNC_EVENT */