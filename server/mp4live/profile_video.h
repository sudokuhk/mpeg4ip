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
 * Copyright (C) Cisco Systems Inc. 2005.  All Rights Reserved.
 * 
 * Contributor(s): 
 *		Bill May 		wmay@cisco.com
 */
#ifndef __PROFILE_VIDEO__
#define __PROFILE_VIDEO__ 1
#include "config_list.h"
#include "mp4.h"

#define VIDEO_ENCODER_XVID "xvid"
#define VIDEO_ENCODING_MPEG4 "MPEG4"

#define VIDEO_NTSC_FRAME_RATE	((float)29.97)
#define VIDEO_PAL_FRAME_RATE	((float)25.00)

#define VIDEO_STD_ASPECT_RATIO 	((float)4.0 / 3.0)	// standard 4:3
#define VIDEO_LB1_ASPECT_RATIO 	((float)2.35)	// typical "widescreen" format
#define VIDEO_LB2_ASPECT_RATIO 	((float)1.85)	// alternate widescreen format
#define VIDEO_LB3_ASPECT_RATIO 	((float)16.0 / 9.0)	// hdtv 16:9


DECLARE_CONFIG(CFG_VIDEO_PROFILE_NAME);
DECLARE_CONFIG(CFG_VIDEO_ENCODER);
DECLARE_CONFIG(CFG_VIDEO_ENCODING);
DECLARE_CONFIG(CFG_VIDEO_WIDTH);
DECLARE_CONFIG(CFG_VIDEO_HEIGHT);
DECLARE_CONFIG(CFG_VIDEO_FRAME_RATE);
DECLARE_CONFIG(CFG_VIDEO_FILTER);
DECLARE_CONFIG(CFG_VIDEO_KEY_FRAME_INTERVAL);
DECLARE_CONFIG(CFG_VIDEO_BIT_RATE);
DECLARE_CONFIG(CFG_VIDEO_FORCE_PROFILE_ID);
DECLARE_CONFIG(CFG_VIDEO_PROFILE_ID);
DECLARE_CONFIG(CFG_VIDEO_TIMEBITS);
DECLARE_CONFIG(CFG_VIDEO_H261_QUALITY);
DECLARE_CONFIG(CFG_VIDEO_H261_QUALITY_ADJ_FRAMES);
DECLARE_CONFIG(CFG_VIDEO_MPEG4_PAR_WIDTH);
DECLARE_CONFIG(CFG_VIDEO_MPEG4_PAR_HEIGHT);
DECLARE_CONFIG(CFG_VIDEO_CROP_ASPECT_RATIO);
DECLARE_CONFIG(CFG_XVID10_VIDEO_QUALITY);
DECLARE_CONFIG(CFG_XVID10_USE_GMC);
DECLARE_CONFIG(CFG_XVID10_USE_QPEL);
DECLARE_CONFIG(CFG_XVID10_USE_LUMIMASK);
DECLARE_CONFIG(CFG_XVID10_USE_INTERLACING);

#ifdef DECLARE_CONFIG_VARIABLES
static SConfigVariable VideoProfileConfigVariables[] = {
  CONFIG_STRING(CFG_VIDEO_PROFILE_NAME, "name", NULL),
  CONFIG_STRING(CFG_VIDEO_ENCODER, "videoEncoder", VIDEO_ENCODER_XVID),
  CONFIG_STRING(CFG_VIDEO_ENCODING, "videoEncoding", VIDEO_ENCODING_MPEG4),

  CONFIG_INT(CFG_VIDEO_WIDTH, "videoWidth", 320),
  CONFIG_INT(CFG_VIDEO_HEIGHT, "videoHeight", 240),
  CONFIG_FLOAT(CFG_VIDEO_FRAME_RATE, "videoFrameRate", 
	       VIDEO_NTSC_FRAME_RATE),
  CONFIG_FLOAT(CFG_VIDEO_KEY_FRAME_INTERVAL, "videoKeyFrameInterval", 
	       2.0),
  CONFIG_FLOAT(CFG_VIDEO_CROP_ASPECT_RATIO, "videoCropAspectRatio",
	       VIDEO_STD_ASPECT_RATIO),
  CONFIG_INT(CFG_VIDEO_BIT_RATE, "videoBitRate",500),
  CONFIG_BOOL(CFG_VIDEO_FORCE_PROFILE_ID, "videoForceProfileId", false),
  CONFIG_INT(CFG_VIDEO_PROFILE_ID, "videoProfileId",MPEG4_SP_L3),
  CONFIG_INT(CFG_VIDEO_TIMEBITS, "videoTimebits", 0),
  CONFIG_INT(CFG_VIDEO_H261_QUALITY, "videoH261Quality", 10),
  CONFIG_INT(CFG_VIDEO_H261_QUALITY_ADJ_FRAMES, "videoH261QualityAdjFrames", 8),
  CONFIG_STRING(CFG_VIDEO_FILTER, "videoFilter", "none"),
  CONFIG_INT(CFG_VIDEO_MPEG4_PAR_WIDTH, "videoMpeg4ParWidth", 0),
  CONFIG_INT(CFG_VIDEO_MPEG4_PAR_HEIGHT, "videoMpeg4ParHeight", 0),

  CONFIG_INT(CFG_XVID10_VIDEO_QUALITY, "xvid10VideoQuality", 6),
  CONFIG_BOOL(CFG_XVID10_USE_GMC, "xvid10UseGmc", false),
  CONFIG_BOOL(CFG_XVID10_USE_QPEL, "xvid10UseQpel", false),
  CONFIG_BOOL(CFG_XVID10_USE_LUMIMASK, "xvid10UseLumimask", false),
  CONFIG_BOOL(CFG_XVID10_USE_INTERLACING, "xvid10UseInterlacing", false),
};
#endif

class CVideoProfile : public CConfigEntry
{
 public:
  CVideoProfile(const char *filename, CConfigEntry *next) :
    CConfigEntry(filename, "video", next) {
    m_videoMpeg4Config = NULL;
    m_videoMpeg4ConfigLength = 0;
  };
  ~CVideoProfile(void) {
    CHECK_AND_FREE(m_videoMpeg4Config);
  };
  void LoadConfigVariables(void);
  void Update(void);
  uint8_t m_videoMpeg4ProfileId;
  uint8_t *m_videoMpeg4Config;
  uint32_t m_videoMpeg4ConfigLength;
  uint32_t m_videoWidth, m_videoHeight;
  uint32_t m_ySize, m_uvSize, m_yuvSize;
  uint32_t m_videoMaxVopSize;
  u_int8_t	m_videoTimeIncrBits;
};

class CVideoProfileList : public CConfigList
{
  public:
  CVideoProfileList(const char *directory) :
    CConfigList(directory, "video") {
  };

  ~CVideoProfileList(void) {
  };
  CVideoProfile *FindProfile(const char *name) {
    return (CVideoProfile *)FindConfigInt(name);
  };
 protected:
  CConfigEntry *CreateConfigInt(const char *fname, CConfigEntry *next) {
    CVideoProfile *ret = new CVideoProfile(fname, next);
    ret->LoadConfigVariables();
    return ret;
  };
};

void GenerateMpeg4VideoConfig(CVideoProfile * pConfig);

#endif