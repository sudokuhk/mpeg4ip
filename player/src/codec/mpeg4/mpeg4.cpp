/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is MPEG4IP.
 * 
 * The Initial Developer of the Original Code is Cisco Systems Inc.
 * Portions created by Cisco Systems Inc. are
 * Copyright (C) Cisco Systems Inc. 2000, 2001.  All Rights Reserved.
 * 
 * Contributor(s): 
 *              Bill May        wmay@cisco.com
 */
/*
 * mpeg4.cpp - implementation with ISO reference codec
 */

#include <stdio.h>
#include <stdlib.h>
#include <fstream.h>
#include "player_mem_bytestream.h"

#include "mpeg4.h"
#include <player_util.h>
#ifndef __GLOBAL_VAR_
#define __GLOBAL_VAR_
#endif

#include <sys/global.hpp>

CMpeg4Codec::CMpeg4Codec(CVideoSync *v, 
			 CInByteStreamBase *pbytestrm, 
			 format_list_t *media_fmt,
			 video_info_t *vinfo,
			 const char *userdata,
			 size_t ud_size) :
  CVideoCodecBase(v, pbytestrm, media_fmt, vinfo, userdata, ud_size)
{
  m_main_short_video_header = FALSE;
  m_bytestream = pbytestrm;
  m_pvodec = new CVideoObjectDecoder(pbytestrm);
  if (vinfo != NULL) {
    m_pvodec->FakeOutVOVOLHead(vinfo->height,
			       vinfo->width,
			       vinfo->frame_rate,
			       &m_bSpatialScalability);
    m_video_sync->config(vinfo->width,
			 vinfo->height,
			 vinfo->frame_rate);

    m_decodeState = DECODE_STATE_NORMAL;
  } else {
    if (media_fmt == NULL || media_fmt->fmt_param == NULL) {
      m_decodeState = DECODE_STATE_VO_SEARCH;
    } else {
      // See if we can decode a passed in vovod header
      if (parse_vovod(media_fmt->fmt_param) == 1) {
	m_decodeState = DECODE_STATE_WAIT_I;
      } else {
	m_decodeState = DECODE_STATE_VO_SEARCH;
      }
    }
  }
  m_dropped_b_frames = 0;
  m_num_wait_i = 0;
  m_num_wait_i_frames = 0;
  m_total_frames = 0;
}


CMpeg4Codec::~CMpeg4Codec()
{
  player_debug_message("MPEG4 codec results:");
  player_debug_message("total frames    : %u", m_total_frames);
  player_debug_message("dropped b frames: %u", m_dropped_b_frames);
  player_debug_message("wait for I times: %u", m_num_wait_i);
  player_debug_message("wait I frames   : %u", m_num_wait_i_frames);
  delete m_pvodec;

}

// Convert a hex character to it's decimal value.
char tohex (char a)
{ 
  if (isdigit(a))
    return (a - '0');
  return (tolower(a) - 'a' + 10);
}

// Parse the format config passed in.  This is the vo vod header
// that we need to get width/height/frame rate
int CMpeg4Codec::parse_vovod (const char *vovod)
{
  char buffer[255];

  char *config = strstr(vovod, "config=");
  if (config == NULL) {
    return 0;
  }
  config += strlen("config=");
  char *end;
  end = config;
  while (isxdigit(*end)) end++;
  if (config == end) {
    return 0;
  }
  // config string will run from config to end
  size_t len = end - config;
  // make sure we have even number of digits to convert to binary
  if ((len % 2) == 1) 
    return 0;
  end = buffer;
  // Convert the config= from ascii to binary
  for (size_t ix = 0; ix < len; ix++) {
    *end = 0;
    *end = (tohex(*config)) << 4;
    config++;
    *end += tohex(*config);
    config++;
    end++;
  }

  // Create a byte stream to take from our buffer.
  CInByteStreamMem *membytestream = new CInByteStreamMem();
  membytestream->set_memory(buffer, len/2);
  // Temporary set of our bytestream
  m_pvodec->set_byte_stream(membytestream);

  // Get the VO/VOL header.  If we fail, set the bytestream back
  try {
    m_pvodec->decodeVOHead();
    //player_debug_message("Found VO in header");
  } catch (const char *err) {
    player_debug_message("Caught exception in VO mem header search %s", err);
    m_pvodec->set_byte_stream(m_bytestream);
    delete membytestream;
    return (0);
  }
  try {
    m_pvodec->decodeVOLHead();
    m_pvodec->postVO_VOLHeadInit(m_pvodec->getWidth(),
				 m_pvodec->getHeight(),
				 &m_bSpatialScalability);
    //player_debug_message("Found VOL in header");
	
    m_video_sync->config(m_pvodec->getWidth(),
			 m_pvodec->getHeight(),
			 m_pvodec->getClockRate());

  } catch (const char *err) {
    player_debug_message("Caught exception in VOL mem header search %s", err);
    m_pvodec->set_byte_stream(m_bytestream);
    delete membytestream;
    return (0);
  }

  // We've found the VO VOL header - that's good.
  // Reset the byte stream back to what it was, delete our temp stream
  m_pvodec->set_byte_stream(m_bytestream);
  delete membytestream;
  //player_debug_message("Decoded vovod header correctly");
  return 1;
}

void CMpeg4Codec::do_pause (void)
{
  m_decodeState = DECODE_STATE_WAIT_I;
}

int CMpeg4Codec::decode (uint64_t ts, int from_rtp)
{
  Int iEof = 1;
  m_total_frames++;
  switch (m_decodeState) {
  case DECODE_STATE_VO_SEARCH:
    try {
      m_pvodec->decodeVOHead();
      m_decodeState = DECODE_STATE_VOL_SEARCH;
      //player_debug_message("Found VO");
    } catch (const char *err) {
      player_debug_message("Caught exception in VO search %s", err);
      return (-1);
    }
    // fall through
  case DECODE_STATE_VOL_SEARCH:
    try {
      m_pvodec->decodeVOLHead();
      m_pvodec->postVO_VOLHeadInit(m_pvodec->getWidth(),
				   m_pvodec->getHeight(),
				   &m_bSpatialScalability);
      //player_debug_message("Found VOL");
	
      m_video_sync->config(m_pvodec->getWidth(),
			   m_pvodec->getHeight(),
			   m_pvodec->getClockRate());

      m_decodeState = DECODE_STATE_WAIT_I;
    } catch (const char *err) {
      player_debug_message("Caught exception in VOL search %s", err);
      return (-1);
    }
    //      return(0);
  case DECODE_STATE_WAIT_I:
    try {
      iEof = m_pvodec->decode(NULL, NULL, TRUE);
      if (iEof == -1) {
	m_num_wait_i_frames++;
	return(0);
      }
      m_decodeState = DECODE_STATE_NORMAL;
      //player_debug_message("Back to normal state");
      m_bCachedRefFrame = FALSE;
      m_bCachedRefFrameCoded = FALSE;
      m_cached_valid = FALSE;
      m_cached_time = 0;
    } catch (const char *err) {
      if (strcmp(err, "DECODE ACROSS TS") != 0) {
	player_debug_message("Caught exception in WAIT_I %s", err);
      }
      return (-1);
    }
    break;
  case DECODE_STATE_NORMAL:
    try {
      iEof = m_pvodec->decode(NULL, NULL, FALSE, m_dropFrame);
      if (m_dropFrame && iEof == -1) {
	//player_debug_message("Dropped b");
	m_dropped_b_frames++;
	return (0);
      }
    } catch (const char *err) {
      // This is because sometimes, the encoder doesn't read all the bytes
      // it should out of the rtp packet.  The rtp bytestream does a read
      // and determines that we're trying to read across bytestreams.
      // If we get this, we don't want to change anything - just fall up
      // to the decoder thread so it gives us a new timestamp.
      if (strcmp(err, "DECODE ACROSS TS") == 0) {
	//player_debug_message("decode across ts");
	return (-1);
      }
      player_debug_message("Normal caught %s", err);
      m_decodeState = DECODE_STATE_WAIT_I;
      return (-1);
    }
    break;
  }

  /*
   * We've got a good frame.  See if we need to display it
   */
  const CVOPU8YUVBA *pvopcQuant = NULL;
  if (m_pvodec->fSptUsage() == 1) {
    player_debug_message("Sprite");
  }
  uint64_t displaytime = 0;
  int cached_ts = 0;
  if (iEof == EOF) {
    if (m_bCachedRefFrame) {
      m_bCachedRefFrame = FALSE;
      if (m_bCachedRefFrameCoded) {
	pvopcQuant = m_pvodec->pvopcRefQLater();
	displaytime = ts;
      }
    }
  } else {
    if (m_pvodec->vopmd().vopPredType == BVOP) {
      if (iEof != FALSE) {
	pvopcQuant = m_pvodec->pvopcReconCurr();
	displaytime = ts;
      } 
    } else {
      if (m_bCachedRefFrame) {
	m_bCachedRefFrame = FALSE;
	if (m_bCachedRefFrameCoded) {
	  pvopcQuant = m_pvodec->pvopcRefQPrev();
	  if (from_rtp) {
	    int old_was_valid = m_cached_valid;
	    displaytime = m_cached_time;
	    cached_ts = 1;
	    // old time stamp wasn't valid - instead of calculating it
	    // ourselves, just punt on it.
	    if (old_was_valid == 0) {
	      return (iEof == EOF ? -1 : 0);
	    }
	  } else {
	    displaytime = ts;
	  }
	}
      }

      m_cached_time = ts;
      m_cached_valid = TRUE;
      m_bCachedRefFrame = TRUE;
      m_bCachedRefFrameCoded = (iEof != FALSE);
    }
  }

  if (pvopcQuant != NULL) {
#if 0
    player_debug_message("frame rtp_ts %llu disp %llu cached %d", 
			 ts, displaytime, cached_ts);
#endif
    /*
     * Get the information to the video sync structure
     */
    const Uint8 *y, *u, *v;
    int pixelw_y, pixelw_uv;
    pixelw_y =  pvopcQuant->getPlane(Y_PLANE)->where().width;
    pixelw_uv = pvopcQuant->getPlane(U_PLANE)->where().width;

    y = pvopcQuant->getPlane(Y_PLANE)->pixels(0,0);
    u = pvopcQuant->getPlane(U_PLANE)->pixels(0,0);
    v = pvopcQuant->getPlane(V_PLANE)->pixels(0,0);
    m_last_time = displaytime;
    //    player_debug_message("Adding video at %llu", displaytime);

    uint64_t rettime;
    int cmp = 
      m_video_sync->set_video_frame(y, 
				    u, 
				    v, 
				    pixelw_y, 
				    pixelw_uv, 
				    displaytime, rettime);
    // disptime is time we've decoded.  Ret is time last buffer was played
    // at.  If we fall behind, we can do 2 things - either nothing  < 3 frames
    // worth - drop B's - up to 10 frames worth, or resync to the next I frame
    if (cmp == 0) {
      if (displaytime < rettime) {
	if (displaytime + (10 * m_pvodec->getClockRate()) < rettime) {
	  player_debug_message("Out of sync - waiting for I %llu %llu",
			       displaytime, rettime);
	  m_decodeState = DECODE_STATE_WAIT_I;
	  m_num_wait_i++;
	} else if (displaytime + (3 * m_pvodec->getClockRate()) < rettime) {
	  m_dropFrame = TRUE;
	} else {
	  m_dropFrame = FALSE;
	}
      } else {
	m_dropFrame = FALSE;
      }
    }
      
  }
  return (iEof == EOF ? -1 : 1);
}

