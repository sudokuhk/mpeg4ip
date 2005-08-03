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
 * Copyright (C) Cisco Systems Inc. 2003-2005.  All Rights Reserved.
 * 
 * Contributor(s): 
 *              Waqar Mohsin            wmohsin@cisco.com
 */

#ifndef __VIDEO_V4L2_SOURCE_H__
#define __VIDEO_V4L2_SOURCE_H__

#ifndef __VIDEO_V4L_SOURCE_H__
#error Please include video_v4l_source.h instead of video_v4l2_source.h
#endif

class CV4LVideoSource : public CMediaSource {
 public:
  CV4LVideoSource() : CMediaSource() {
    m_videoDevice = -1;
    m_buffers = NULL;
    m_decimate_filter = false;
    m_use_alternate_release = false;
  }

  static bool InitialVideoProbe(CLiveConfig* pConfig);

  bool IsDone() {
    return false;	// live capture is inexhaustible
  }

  float GetProgress() {
    return 0.0;		// live capture device is inexhaustible
  }

  bool SetPictureControls();

  void IndicateReleaseFrame(uint8_t index) {
    // indicate which index needs to be released before next acquire
    SDL_LockMutex(m_v4l_mutex);
    m_release_index_mask |= (1 << index);
    SDL_UnlockMutex(m_v4l_mutex);
    if (m_waiting_frames_return)
      SDL_SemPost(m_myMsgQueueSemaphore);
  };

 protected:
  int ThreadMain(void);
  void DoStartCapture(void);
  void DoStopCapture(void);
  bool Init(void);
  bool InitDevice(void);
  void ReleaseDevice(void);
  void ProcessVideo(void);
  int8_t AcquireFrame(Timestamp &frameTimestamp);
  void ReleaseFrames(void);
  void SetVideoAudioMute(bool mute);

  u_int8_t m_maxPasses;
  int m_videoDevice;

  typedef struct {
    void* start;
    __u32 length;
  } capture_buffer_t;

  capture_buffer_t* m_buffers;
  uint32_t m_buffers_count;
  
  Timestamp m_videoCaptureStartTimestamp;
  float m_videoSrcFrameRate;
  bool m_decimate_filter;
  SDL_mutex *m_v4l_mutex;
  uint32_t m_release_index_mask;
  bool m_use_alternate_release;
  bool m_waiting_frames_return;
  uint32_t m_format;
  uint32_t m_u_offset;
  uint32_t m_v_offset;
};


#endif /* __VIDEO_V4L2_SOURCE_H__ */
