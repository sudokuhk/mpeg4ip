/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997, 1998, 1999, 2000  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@devolution.com
*/

#ifdef SAVE_RCSID
static char rcsid =
 "@(#) $Id: SDL_lowaudio.h,v 1.1 2001/02/05 20:26:27 cahighlander Exp $";
#endif

#ifndef _SDL_lowaudio_h
#define _SDL_lowaudio_h

// #include <libraries/ahi_sub.h>

#include "SDL_sysaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hidden "this" pointer for the audio functions */
#define _THIS	SDL_AudioDevice *this

struct SDL_PrivateAudioData {
	/* The handle for the audio device */
	struct AHIRequest *audio_req[2];
	struct MsgPort *audio_port;
	Sint32 freq,type,bytespersample;
	Uint8 *mixbuf[2];           /* The app mixing buffer */
	int current_buffer;
	Uint32 playing;
};

/* Old variable names */
#define audio_port		(this->hidden->audio_port)
#define audio_req		(this->hidden->audio_req)
#define mixbuf			(this->hidden->mixbuf)
#define current_buffer		(this->hidden->current_buffer)
#define playing			(this->hidden->playing)

#endif /* _SDL_lowaudio_h */