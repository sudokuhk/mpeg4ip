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
 * Copyright (C) Cisco Systems Inc. 2000-2002.  All Rights Reserved.
 * 
 * Contributor(s): 
 *		Dave Mackie		dmackie@cisco.com
 */

#include "mp4live.h"
#include "media_source.h"
#include "audio_encoder.h"
#include "video_encoder.h"
#include "video_util_rgb.h"
#include <mp4av.h>
#include "mpeg4ip_byteswap.h"

//#define DEBUG_AUDIO_RESAMPLER 1
//#define DEBUG_SYNC 1
//#define DEBUG_SYNC_DRIFT 1
//#define DEBUG_SYNC_DROPS 1
//#define DEBUG_SYNC_LAG 1
CMediaSource::CMediaSource() 
{
  m_pSinksMutex = SDL_CreateMutex();
  if (m_pSinksMutex == NULL) {
    debug_message("CreateMutex error");
  }
  for (int i = 0; i < MAX_SINKS; i++) {
    m_sinks[i] = NULL;
  }

  m_source = false;
  m_sourceVideo = false;
  m_sourceAudio = false;
  m_maxAheadDuration = TimestampTicks / 20 ;	// 50 msec

  m_videoSource = this;
  m_videoSrcYImage = NULL;
  m_videoDstYImage = NULL;
  m_videoYResizer = NULL;
  m_videoSrcUVImage = NULL;
  m_videoDstUVImage = NULL;
  m_videoUVResizer = NULL;
  m_videoEncoder = NULL;
  m_videoDstPrevImage = NULL;
  m_videoDstPrevReconstructImage = NULL;
  m_videoDstPrevFrame = NULL;

  m_audioPreEncodingBuffer = NULL;
  m_audioEncoder = NULL;
  m_audioResample = NULL;
}

CMediaSource::~CMediaSource() 
{
  SDL_DestroyMutex(m_pSinksMutex);
  m_pSinksMutex = NULL;
	
  if (m_audioResample != NULL) {
    for (int ix = 0; ix < m_audioDstChannels; ix++) {
      free(m_audioResample[ix]);
      m_audioResample[ix] = NULL;
    }
    free(m_audioResample);
    m_audioResample = NULL;
  }
}

bool CMediaSource::AddSink(CMediaSink* pSink) 
{
  bool rc = false;

  if (SDL_LockMutex(m_pSinksMutex) == -1) {
    debug_message("AddSink LockMutex error");
    return rc;
  }
  for (int i = 0; i < MAX_SINKS; i++) {
    if (m_sinks[i] == NULL) {
      m_sinks[i] = pSink;
      rc = true;
      break;
    }
  }
  if (SDL_UnlockMutex(m_pSinksMutex) == -1) {
    debug_message("UnlockMutex error");
  }
  return rc;
}

void CMediaSource::RemoveSink(CMediaSink* pSink) 
{
  if (SDL_LockMutex(m_pSinksMutex) == -1) {
    debug_message("RemoveSink LockMutex error");
    return;
  }
  for (int i = 0; i < MAX_SINKS; i++) {
    if (m_sinks[i] == pSink) {
      int j;
      for (j = i; j < MAX_SINKS - 1; j++) {
	m_sinks[j] = m_sinks[j+1];
      }
      m_sinks[j] = NULL;
      break;
    }
  }
  if (SDL_UnlockMutex(m_pSinksMutex) == -1) {
    debug_message("UnlockMutex error");
  }
}

void CMediaSource::RemoveAllSinks(void) 
{
  if (SDL_LockMutex(m_pSinksMutex) == -1) {
    debug_message("RemoveAllSinks LockMutex error");
    return;
  }
  for (int i = 0; i < MAX_SINKS; i++) {
    if (m_sinks[i] == NULL) {
      break;
    }
    m_sinks[i] = NULL;
  }
  if (SDL_UnlockMutex(m_pSinksMutex) == -1) {
    debug_message("UnlockMutex error");
  }
}

Duration CMediaSource::GetElapsedDuration()
{
  if (m_sourceVideo && m_sourceAudio) {
    return MIN(m_videoSrcElapsedDuration, m_audioSrcElapsedDuration);
  } else if (m_sourceVideo) {
    return m_videoSrcElapsedDuration;
  } else if (m_sourceAudio) {
    return m_audioSrcElapsedDuration;
  }
  return 0;
}

void CMediaSource::ForwardFrame(CMediaFrame* pFrame)
{
  if (SDL_LockMutex(m_pSinksMutex) == -1) {
    debug_message("ForwardFrame LockMutex error");
    return;
  }

  for (int i = 0; i < MAX_SINKS; i++) {
    if (m_sinks[i] == NULL) {
      break;
    }
    m_sinks[i]->EnqueueFrame(pFrame);
  }

  if (SDL_UnlockMutex(m_pSinksMutex) == -1) {
    debug_message("UnlockMutex error");
  }
  if (pFrame->RemoveReference()) delete pFrame;
  return;
}

void CMediaSource::DoStopSource()
{
  if (!m_source) {
    return;
  }

  DoStopVideo();

  DoStopAudio();

  m_source = false;
}

bool CMediaSource::InitVideo(
			     MediaType srcType,
			     bool realTime)
{
  m_sourceRealTime = realTime;
  m_sinkRealTime = m_pConfig->GetBoolValue(CONFIG_RTP_ENABLE);

  m_videoSrcType = srcType;
  m_videoSrcFrameNumber = 0;
  m_audioSrcFrameNumber = 0;	// ensure audio is also at zero

  m_videoDstFrameRate =
    m_pConfig->GetFloatValue(CONFIG_VIDEO_FRAME_RATE);
  m_videoDstFrameDuration = 
    (Duration)(((float)TimestampTicks / m_videoDstFrameRate) + 0.5);
  m_videoDstFrameNumber = 0;
  m_videoEncodedFrames = 0;
  m_videoDstWidth =
    m_pConfig->m_videoWidth;
  m_videoDstHeight =
    m_pConfig->m_videoHeight;
  m_videoDstAspectRatio = 
    (float)m_pConfig->m_videoWidth / (float)m_pConfig->m_videoHeight;
  m_videoDstYSize = m_videoDstWidth * m_videoDstHeight;
  m_videoDstUVSize = m_videoDstYSize / 4;
  m_videoDstYUVSize = (m_videoDstYSize * 3) / 2;

  // intialize encoder
  m_videoEncoder = 
    VideoEncoderCreate(m_pConfig->GetStringValue(CONFIG_VIDEO_ENCODER));
  m_videoDstType = m_videoEncoder->GetFrameType();

  if (!m_videoEncoder) {
    return false;
  }
  if (!m_videoEncoder->Init(m_pConfig, realTime)) {
    delete m_videoEncoder;
    m_videoEncoder = NULL;
    return false;
  }

#ifdef DEBUG_VCODEC_SHADOW
  m_videoEncoderShadow = VideoEncoderCreate("ffmpeg");
  m_videoEncoderShadow->Init(m_pConfig, realTime);
#endif

  m_videoWantKeyFrame = true;
  m_videoEncodingDrift = 0;
  m_videoEncodingMaxDrift = m_videoDstFrameDuration;
  m_videoSrcElapsedDuration = 0;
  m_videoDstElapsedDuration = 0;
  m_otherTotalDrift = 0;
  m_otherLastTotalDrift = 0;

  m_videoDstPrevImage = NULL;
  m_videoDstPrevReconstructImage = NULL;
  m_videoDstPrevFrame = NULL;
  m_videoDstPrevFrameLength = 0;

  return true;
}

void CMediaSource::SetVideoSrcSize(
				   u_int16_t srcWidth,
				   u_int16_t srcHeight,
				   u_int16_t srcStride,
				   bool matchAspectRatios)
{
  // N.B. InitVideo() must be called first

  m_videoSrcWidth = srcWidth;
  m_videoSrcHeight = srcHeight;
  m_videoSrcAspectRatio = (float)srcWidth / (float)srcHeight;
  m_videoMatchAspectRatios = matchAspectRatios;

  SetVideoSrcStride(srcStride);
}

void CMediaSource::SetVideoSrcStride(
				     u_int16_t srcStride)
{
  // N.B. SetVideoSrcSize() should be called once before 

  m_videoSrcYStride = srcStride;
  m_videoSrcUVStride = srcStride / 2;

  // these next three may change below
  m_videoSrcAdjustedHeight = m_videoSrcHeight;
  m_videoSrcYCrop = 0;
  m_videoSrcUVCrop = 0;

  // match aspect ratios
  if (m_videoMatchAspectRatios 
      && fabs(m_videoSrcAspectRatio - m_videoDstAspectRatio) > 0.01) {

    m_videoSrcAdjustedHeight =
      (u_int16_t)(m_videoSrcWidth / m_videoDstAspectRatio);
    if ((m_videoSrcAdjustedHeight % 16) != 0) {
      m_videoSrcAdjustedHeight += 16 - (m_videoSrcAdjustedHeight % 16);
    }

    if (m_videoSrcAspectRatio < m_videoDstAspectRatio) {
      // crop src
      m_videoSrcYCrop = m_videoSrcYStride * 
	((m_videoSrcHeight - m_videoSrcAdjustedHeight) / 2);
      m_videoSrcUVCrop = m_videoSrcYCrop / 4;
    }
  }

  m_videoSrcYSize = m_videoSrcYStride 
    * MAX(m_videoSrcHeight, m_videoSrcAdjustedHeight);
  m_videoSrcUVSize = m_videoSrcYSize / 4;
  m_videoSrcYUVSize = (m_videoSrcYSize * 3) / 2;

  // resizing

  DestroyVideoResizer();

  if (m_videoSrcWidth != m_videoDstWidth 
      || m_videoSrcAdjustedHeight != m_videoDstHeight) {

    m_videoSrcYImage = 
      scale_new_image(m_videoSrcWidth, 
		      m_videoSrcAdjustedHeight, 1);
    m_videoSrcYImage->span = m_videoSrcYStride;
    m_videoDstYImage = 
      scale_new_image(m_videoDstWidth, 
		      m_videoDstHeight, 1);
    m_videoYResizer = 
      scale_image_init(m_videoDstYImage, m_videoSrcYImage, 
		       Bell_filter, Bell_support);

    m_videoSrcUVImage = 
      scale_new_image(m_videoSrcWidth / 2, 
		      m_videoSrcAdjustedHeight / 2, 1);
    m_videoSrcUVImage->span = m_videoSrcUVStride;
    m_videoDstUVImage = 
      scale_new_image(m_videoDstWidth / 2, 
		      m_videoDstHeight / 2, 1);
    m_videoUVResizer = 
      scale_image_init(m_videoDstUVImage, m_videoSrcUVImage, 
		       Bell_filter, Bell_support);
  }
}

void CMediaSource::ProcessVideoYUVFrame(
					u_int8_t* pY,
					u_int8_t* pU,
					u_int8_t* pV,
					u_int16_t yStride,
					u_int16_t uvStride,
					Timestamp srcFrameTimestamp)
{
  Duration temp;
  m_videoSrcPrevElapsedDuration = m_videoSrcElapsedDuration;
  if (m_videoSrcFrameNumber == 0) {
    if (m_audioSrcFrameNumber == 0) {
      m_encodingStartTimestamp = GetTimestamp();
    }
    m_videoStartTimestamp = srcFrameTimestamp;
  }

  m_videoSrcFrameNumber++;
  temp = srcFrameTimestamp - m_videoStartTimestamp;
  if (temp <= m_videoSrcElapsedDuration) {
#ifdef DEBUG_SYNC
    error_message("video duplication in source timestamp %llu %llu",
		  srcFrameTimestamp, temp);
#endif
  }
  m_videoSrcElapsedDuration = temp;

  // drop src frames as needed to match target frame rate
  if (m_videoDstElapsedDuration > m_videoSrcElapsedDuration) {
#ifdef DEBUG_SYNC
    debug_message("Dropping at %llu before %llu", 
		  m_videoSrcElapsedDuration, m_videoDstElapsedDuration);
#endif
    return;
  }

  // if we're running in real-time mode
  if (m_sourceRealTime) {

    // add any external drift (i.e. audio encoding drift)
    // to our drift measurement
#if 0
    debug_message("drift %llu other last %llu %llu", 
		  m_videoEncodingDrift,
		  m_otherLastTotalDrift, m_otherTotalDrift);
#endif
    m_videoEncodingDrift += 
      m_otherTotalDrift - m_otherLastTotalDrift;
    m_otherLastTotalDrift = m_otherTotalDrift;

    // check if we are falling behind
    if (m_videoEncodingDrift >= m_videoEncodingMaxDrift) {
#ifdef DEBUG_SYNC_DROPS
      debug_message("video skipping src frame %llu drift %llu max %llu", 
		    m_videoSrcElapsedDuration,
		    m_videoEncodingDrift, m_videoEncodingMaxDrift);
      if (m_otherLastTotalDrift != 0 || m_otherTotalDrift != 0) {
	debug_message("video other last %llu %llu", 
		      m_otherLastTotalDrift, m_otherTotalDrift);
      }
#endif
      
      Duration diff = m_videoSrcElapsedDuration - m_videoDstElapsedDuration;
#ifdef DEBUG_SYNC_DROPS
      debug_message("video source vs dest is %lld", diff);
#endif
      int removed = 0;
      do {
	m_videoEncodingDrift -= m_videoDstFrameDuration;
	if (m_videoEncodingDrift < 0) {
	  m_videoEncodingDrift = 0;
	}
	m_videoDstFrameNumber++;
	m_videoDstElapsedDuration = VideoDstFramesToDuration();
	diff -= m_videoDstFrameDuration;
	removed++;
      } while (m_videoEncodingDrift > m_videoDstFrameDuration && diff > 0);
#ifdef DEBUG_SYNC_DROPS
      debug_message("dest duration is now %llu - skipped %d frames", m_videoDstElapsedDuration, removed);
#endif
      if (diff > 0) {
	// src is still ahead - we can use it.
	m_videoEncodingDrift = 0;
      } else 
	return;
    }
  }

  Timestamp encodingStartTimestamp = GetTimestamp();

  // this will either never happen (live capture)
  // or just happen once at startup when we discover
  // the stride used by the video decoder
  if (yStride != m_videoSrcYStride) {
    SetVideoSrcSize(m_videoSrcWidth, m_videoSrcHeight, 
		    yStride, m_videoMatchAspectRatios);
  }

  u_int8_t* mallocedYuvImage = NULL;

  // crop to desired aspect ratio (may be a no-op)
  u_int8_t* yImage = pY + m_videoSrcYCrop;
  u_int8_t* uImage = pU + m_videoSrcUVCrop;
  u_int8_t* vImage = pV + m_videoSrcUVCrop;

  // resize image if necessary
  if (m_videoYResizer) {
    u_int8_t* resizedYUV = 
      (u_int8_t*)Malloc(m_videoDstYUVSize);
		
    u_int8_t* resizedY = 
      resizedYUV;
    u_int8_t* resizedU = 
      resizedYUV + m_videoDstYSize;
    u_int8_t* resizedV = 
      resizedYUV + m_videoDstYSize + m_videoDstUVSize;

    m_videoSrcYImage->data = yImage;
    m_videoDstYImage->data = resizedY;
    scale_image_process(m_videoYResizer);

    m_videoSrcUVImage->data = uImage;
    m_videoDstUVImage->data = resizedU;
    scale_image_process(m_videoUVResizer);

    m_videoSrcUVImage->data = vImage;
    m_videoDstUVImage->data = resizedV;
    scale_image_process(m_videoUVResizer);

    // done with the original source image
    // this may be NULL
    free(mallocedYuvImage);

    // switch over to resized version
    mallocedYuvImage = resizedYUV;
    yImage = resizedY;
    uImage = resizedU;
    vImage = resizedV;
    yStride = m_videoDstWidth;
    uvStride = yStride / 2;
  }

  // calculate previous frame duration
  Timestamp dstPrevFrameTimestamp =
    m_videoStartTimestamp + m_videoDstPrevFrameElapsedDuration;
  Duration dstPrevFrameDuration = 
    m_videoDstElapsedDuration - m_videoDstPrevFrameElapsedDuration;
  m_videoDstPrevFrameElapsedDuration = m_videoDstElapsedDuration;

  // calculate the end of this frame
  m_videoEncodedFrames++;
  m_videoDstFrameNumber++;
  m_videoDstElapsedDuration = VideoDstFramesToDuration();

  // if we want encoded video frames
  if (m_pConfig->m_videoEncode) {

    // call video encoder
    bool rc = m_videoEncoder->EncodeImage(
					  yImage, uImage, vImage, 
					  yStride, uvStride,
					  m_videoWantKeyFrame,
					  m_videoDstElapsedDuration);

    if (!rc) {
      debug_message("Can't encode image!");
      free(mallocedYuvImage);
      return;
    }
#ifdef DEBUG_VCODEC_SHADOW
    m_videoEncoderShadow->EncodeImage(
				      yImage, uImage, vImage, 
				      yStride, uvStride,
				      m_videoWantKeyFrame);
    // Note: we don't retrieve encoded frame from shadow
#endif

    // clear want key frame flag
    m_videoWantKeyFrame = false;
  }


#ifdef DEBUG_SYNC
  // Display this before we recalculate elapsed duration
  debug_message("video src frame duration %llu dst %llu prev len %d", 
		m_videoSrcElapsedDuration, m_videoDstElapsedDuration,
		m_videoDstPrevFrameLength);
#endif

  if (m_sourceRealTime && m_videoSrcFrameNumber > 0) {

    // next check our duration against real elasped time
    Duration lag = m_videoSrcElapsedDuration - m_videoDstElapsedDuration;

    if (lag >= m_videoDstFrameDuration) {
      // adjust by integral number of target duration units
#if 0
      debug_message("video Lag dst %llu src %llu frames %llu", 
		    m_videoDstElapsedDuration,
		    m_videoSrcElapsedDuration,
		    lag / m_videoDstFrameDuration);
#endif
      m_videoDstFrameNumber += (lag / m_videoDstFrameDuration) + 1;

      m_videoDstElapsedDuration = VideoDstFramesToDuration();
#ifdef DEBUG_SYNC_LAG
    } else {
      debug_message("video lag is %lld %lld", lag, m_videoDstFrameDuration);
#endif
    }
  }

  // forward encoded video to sinks
  if (m_pConfig->m_videoEncode) {
    if (m_videoDstPrevFrame) {
      CMediaFrame* pFrame = new CMediaFrame(
					    m_videoEncoder->GetFrameType(),
					    m_videoDstPrevFrame, 
					    m_videoDstPrevFrameLength,
					    dstPrevFrameTimestamp, 
					    dstPrevFrameDuration);
      pFrame->SetMediaFreeFunction(m_videoEncoder->GetMediaFreeFunction());
      ForwardFrame(pFrame);
    }

    // hold onto this encoded vop until next one is ready
    m_videoEncoder->GetEncodedImage(
				    &m_videoDstPrevFrame, &m_videoDstPrevFrameLength);
  }

  // forward raw video to sinks
  if (m_pConfig->SourceRawVideo()) {

    if (m_videoDstPrevImage) {
      CMediaFrame* pFrame =
	new CMediaFrame(
			YUVVIDEOFRAME, 
			m_videoDstPrevImage, 
			m_videoDstYUVSize,
			dstPrevFrameTimestamp, 
			dstPrevFrameDuration);
      ForwardFrame(pFrame);
    }

    m_videoDstPrevImage = (u_int8_t*)Malloc(m_videoDstYUVSize);

    imgcpy(m_videoDstPrevImage, 
	   yImage, 
	   m_videoDstWidth,
	   m_videoDstHeight,
	   yStride);
    imgcpy(m_videoDstPrevImage + m_videoDstYSize,
	   uImage, 
	   m_videoDstWidth / 2,
	   m_videoDstHeight / 2,
	   uvStride);
    imgcpy(m_videoDstPrevImage + m_videoDstYSize + m_videoDstUVSize,
	   vImage, 
	   m_videoDstWidth / 2,
	   m_videoDstHeight / 2,
	   uvStride);
  }

  // forward reconstructed video to sinks
  if (m_pConfig->m_videoEncode
      && m_pConfig->GetBoolValue(CONFIG_VIDEO_ENCODED_PREVIEW)) {

    if (m_videoDstPrevReconstructImage) {
      CMediaFrame* pFrame =
	new CMediaFrame(RECONSTRUCTYUVVIDEOFRAME, 
			m_videoDstPrevReconstructImage, 
			m_videoDstYUVSize,
			dstPrevFrameTimestamp, 
			dstPrevFrameDuration);
      ForwardFrame(pFrame);
    }

    m_videoDstPrevReconstructImage = 
      (u_int8_t*)Malloc(m_videoDstYUVSize);

    m_videoEncoder->GetReconstructedImage(
					  m_videoDstPrevReconstructImage,
					  m_videoDstPrevReconstructImage 
					  + m_videoDstYSize,
					  m_videoDstPrevReconstructImage
					  + m_videoDstYSize + m_videoDstUVSize);
  }


  // calculate how we're doing versus target frame rate
  // this is used to decide if we need to drop frames
  if (m_sourceRealTime) {
    // reset skipped frames
    Duration drift = GetTimestamp() - encodingStartTimestamp;
    if (drift > m_videoDstFrameDuration) {
      m_videoEncodingDrift += drift - m_videoDstFrameDuration;
    } else {
      drift = m_videoDstFrameDuration - drift;
      if (m_videoEncodingDrift > drift) {
	m_videoEncodingDrift -= drift;
      } else {
	m_videoEncodingDrift = 0;
      }
    }
#if DEBUG_SYNC_DRIFT
    if (m_videoEncodingDrift > 0) 
      debug_message("drift is %lld - dst duration is %llu total %llu",
		    drift, m_videoDstFrameDuration, m_videoEncodingDrift);
#endif
  }

  free(mallocedYuvImage);
  return;
}

void CMediaSource::DoStopVideo()
{
  DestroyVideoResizer();

  if (m_videoEncoder) {
    m_videoEncoder->Stop();
    delete m_videoEncoder;
    m_videoEncoder = NULL;
  }

  m_sourceVideo = false;
}

void CMediaSource::DestroyVideoResizer()
{
  if (m_videoSrcYImage) {
    scale_free_image(m_videoSrcYImage);
    m_videoSrcYImage = NULL;
  }
  if (m_videoDstYImage) {
    scale_free_image(m_videoDstYImage);
    m_videoDstYImage = NULL;
  }
  if (m_videoYResizer) {
    scale_image_done(m_videoYResizer);
    m_videoYResizer = NULL;
  }
  if (m_videoSrcUVImage) {
    scale_free_image(m_videoSrcUVImage);
    m_videoSrcUVImage = NULL;
  }
  if (m_videoDstUVImage) {
    scale_free_image(m_videoDstUVImage);
    m_videoDstUVImage = NULL;
  }
  if (m_videoUVResizer) {
    scale_image_done(m_videoUVResizer);
    m_videoUVResizer = NULL;
  }
}

bool CMediaSource::InitAudio(
			     bool realTime)
{
  m_sourceRealTime = realTime;
  m_sinkRealTime = m_pConfig->GetBoolValue(CONFIG_RTP_ENABLE);
  m_audioSrcSampleNumber = 0;
  m_audioSrcFrameNumber = 0;
  //m_videoSrcFrameNumber = 0;	// ensure video is also at zero

  // audio destination info
  m_audioDstChannels =
    m_pConfig->GetIntegerValue(CONFIG_AUDIO_CHANNELS);
  m_audioDstSampleRate =
    m_pConfig->GetIntegerValue(CONFIG_AUDIO_SAMPLE_RATE);
  m_audioDstSampleNumber = 0;
  m_audioDstFrameNumber = 0;
  m_audioDstRawSampleNumber = 0;
  m_audioDstRawFrameNumber = 0;

  m_audioSrcElapsedDuration = 0;
  m_audioDstElapsedDuration = 0;

  return true;
}

bool CMediaSource::SetAudioSrc(
			       MediaType srcType,
			       u_int8_t srcChannels,
			       u_int32_t srcSampleRate)
{
  // audio source info 
  m_audioSrcType = srcType;
  m_audioSrcChannels = srcChannels;
  m_audioSrcSampleRate = srcSampleRate;
  m_audioSrcSamplesPerFrame = 0;	// unknown, presumed variable

  // init audio encoder
  delete m_audioEncoder;

  m_audioEncoder = AudioEncoderCreate(
				      m_pConfig->GetStringValue(CONFIG_AUDIO_ENCODER));
  m_audioDstType = m_audioEncoder->GetFrameType();

  if (m_audioEncoder == NULL) {
    return false;
  }

  if (!m_audioEncoder->Init(m_pConfig, m_sourceRealTime)) {
    delete m_audioEncoder;
    m_audioEncoder = NULL;
    return false;
  }

  m_audioDstSamplesPerFrame = 
    m_audioEncoder->GetSamplesPerFrame();

  // if we need to resample
  if (m_audioDstSampleRate != m_audioSrcSampleRate) {
    // create a resampler for each audio destination channel - 
    // we will combine the channels before resampling
    m_audioResample = (resample_t *)malloc(sizeof(resample_t) *
					   m_audioDstChannels);
    for (int ix = 0; ix <= m_audioDstChannels; ix++) {
      m_audioResample[ix] = st_resample_start(m_audioSrcSampleRate, 
					      m_audioDstSampleRate);
    }
  }

  // this calculation doesn't take into consideration the resampling
  // size of the src.  4 times might not be enough - we need most likely
  // 2 times the max of the src samples and the dest samples

  m_audioPreEncodingBufferLength = 0;
  m_audioPreEncodingBufferMaxLength =
    4 * DstSamplesToBytes(m_audioDstSamplesPerFrame);

  m_audioPreEncodingBuffer = (u_int8_t*)realloc(
						m_audioPreEncodingBuffer,
						m_audioPreEncodingBufferMaxLength);
		
  if (m_audioPreEncodingBuffer == NULL) {
    delete m_audioEncoder;
    m_audioEncoder = NULL;
    return false;
  }

  return true;
}

void CMediaSource::AddGapToAudio(Timestamp startTimestamp, Duration silenceDuration)
{
  uint32_t samples = SrcTicksToSamples(silenceDuration);
  uint32_t bytes = SrcSamplesToBytes(samples);
  if (bytes > 0) {
    error_message("Adding audio gap of %lld duration %u bytes", 
		  silenceDuration, bytes);
    uint8_t *pSilence = (uint8_t *)Malloc(bytes);

    memset(pSilence, 0, bytes);
    ProcessAudioFrame(pSilence, bytes, startTimestamp, false);
    free(pSilence);
  }
    
}

void CMediaSource::ProcessAudioFrame(
				     u_int8_t* frameData,
				     u_int32_t frameDataLength,
				     Timestamp srcFrameTimestamp,
				     bool resync)
{
  Duration srcStartDuration;
  //  debug_message("audio - ts %llu bytes %d", srcFrameTimestamp, frameDataLength);
  if (m_audioSrcFrameNumber == 0) {
    if (!m_sourceVideo || m_videoSrcFrameNumber == 0) {
      m_encodingStartTimestamp = GetTimestamp();
      //  debug_message("Setting encoding start ts to %llu", m_encodingStartTimestamp);
    }
    m_audioStartTimestamp = srcFrameTimestamp;
    m_audioSrcElapsedDuration = 0;
  } else {
    Duration diff;
    srcStartDuration = srcFrameTimestamp - m_audioStartTimestamp;
    if (srcStartDuration >= m_audioSrcElapsedDuration) {
      diff = srcStartDuration - m_audioSrcElapsedDuration;
    } else {
      diff = m_audioSrcElapsedDuration - srcStartDuration;
    }
#ifdef DEBUG_SYNC
    if (diff < -1 || diff > 1) {
    debug_message("audio dur should be %llu is %llu - diff %lld", 
		  m_audioSrcElapsedDuration, srcStartDuration, diff);
    }
#endif
    if (diff >= 2000LL) {
      // we have a time differential between audio frames of more
      // than 2 milliseconds.  This is most likely an error
      error_message("audio - missing audio frame found - duration %llu should be about %llu diff %llu", 
		    srcStartDuration, m_audioSrcElapsedDuration, diff);
      if (srcStartDuration > m_audioSrcElapsedDuration) {
	AddGapToAudio(m_audioStartTimestamp + m_audioSrcElapsedDuration, diff);
      }
    }
    m_audioSrcElapsedDuration = srcStartDuration;
  }

  m_audioSrcFrameNumber++;
  m_audioSrcSampleNumber += SrcBytesToSamples(frameDataLength);

  if (resync) {
    // flush preEncodingBuffer
    m_audioPreEncodingBufferLength = 0;

    // change dst sample numbers to account for gap
    m_audioDstSampleNumber =
      m_audioDstRawSampleNumber =
      DstTicksToSamples(m_audioSrcElapsedDuration);
    error_message("Received resync");
  }

  // calculate m_audioSrcElapsedDuration for end of frame - we're only looking for
  // a plus or minus 2 msec here.
  m_audioSrcElapsedDuration += SrcSamplesToTicks(SrcBytesToSamples(frameDataLength));

  bool pcmMalloced = false;
  bool pcmBuffered;
  u_int8_t* pcmData = frameData;
  u_int32_t pcmDataLength = frameDataLength;

  if (m_audioSrcChannels != m_audioDstChannels) {
    // Convert the channels if they don't match
    // we either double the channel info, or combine
    // the left and right
    uint32_t samples = SrcBytesToSamples(frameDataLength);
    uint32_t dstLength = DstSamplesToBytes(samples);
    pcmData = (u_int8_t *)Malloc(dstLength);
    pcmDataLength = dstLength;
    pcmMalloced = true;

    int16_t *src = (int16_t *)frameData;
    int16_t *dst = (int16_t *)pcmData;
    if (m_audioSrcChannels == 1) {
      // 1 channel to 2
      for (uint32_t ix = 0; ix < samples; ix++) {
	*dst++ = *src;
	*dst++ = *src++;
      }
    } else {
      // 2 channels to 1
      for (uint32_t ix = 0; ix < samples; ix++) {
	int32_t sum = *src++;
	sum += *src++;
	sum /= 2;
	if (sum < -32768) sum = -32768;
	else if (sum > 32767) sum = 32767;
	*dst++ = sum & 0xffff;
      }
    }
  }
	  
  // resample audio, if necessary
  if (m_audioSrcSampleRate != m_audioDstSampleRate) {
    ResampleAudio(pcmData, pcmDataLength);

    // resampled data is now available in m_audioPreEncodingBuffer
    pcmBuffered = true;

  } else if (m_audioSrcSamplesPerFrame != m_audioDstSamplesPerFrame) {
    // reframe audio, if necessary
    // e.g. MP3 is 1152 samples/frame, AAC is 1024 samples/frame

    // add samples to end of m_audioBuffer
    // InitAudio() ensures that buffer is large enough
    memcpy(
	   &m_audioPreEncodingBuffer[m_audioPreEncodingBufferLength],
	   pcmData,
	   pcmDataLength);

    m_audioPreEncodingBufferLength += pcmDataLength;

    pcmBuffered = true;

  } else {
    pcmBuffered = false;
  }

  // LATER restructure so as get rid of this label, and goto below
 pcmBufferCheck:

  if (pcmBuffered) {
    u_int32_t samplesAvailable =
      DstBytesToSamples(m_audioPreEncodingBufferLength);

    // not enough samples collected yet to call encode or forward
    if (samplesAvailable < m_audioDstSamplesPerFrame) {
      return;
    }
    if (pcmMalloced) {
      free(pcmData);
      pcmMalloced = false;
    }

    // setup for encode/forward
    pcmData = 
      &m_audioPreEncodingBuffer[0];
    pcmDataLength = 
      DstSamplesToBytes(m_audioDstSamplesPerFrame);
  }

  // encode audio frame
  if (m_pConfig->m_audioEncode) {

    Timestamp encodingStartTimestamp = GetTimestamp();
    Duration frametime = 
      DstSamplesToTicks(DstBytesToSamples(pcmDataLength));
    Duration startOutput = DstSamplesToTicks(m_audioDstSampleNumber);
    if (m_audioDstFrameNumber == 0) {
      m_audioEncodingStartTimestamp = encodingStartTimestamp;
    }
#if 1
    Duration diff = encodingStartTimestamp - m_audioEncodingStartTimestamp;

    if (diff >= (5 * frametime) + startOutput) {
#else
      //      if (false) {
#endif
      error_message("audio - fell behind %llu - skipping %llu", diff,
		    startOutput + (5 * frametime));
      m_audioPreEncodingBufferLength = 0;
      pcmDataLength = 0;
      
      // change dst sample numbers to account for gap
      m_audioDstSampleNumber =
	m_audioDstRawSampleNumber =
	DstTicksToSamples(m_audioSrcElapsedDuration);
      m_audioDstFrameNumber = m_audioDstSampleNumber / m_audioDstSamplesPerFrame;
      // we're going to skip these frames...
    } else {
      bool rc = m_audioEncoder->EncodeSamples(
						(int16_t*)pcmData, 
						m_audioDstSamplesPerFrame,
						m_audioDstChannels);

	if (!rc) {
	  debug_message("failed to encode audio");
	  return;
	}

	Duration encodingTime =
	  (GetTimestamp() - encodingStartTimestamp);

	if (m_sourceRealTime && m_videoSource) {
	  Duration drift;
	  if (frametime <= encodingTime) {
	    drift = encodingTime - frametime;
#ifdef DEBUG_SYNC_DRIFT
	    debug_message("Adding %llu audio drift", drift);
#endif
	    m_videoSource->AddEncodingDrift(drift);
	  } else {
#if 0
	    drift = frametime - encodingTime;
#ifdef DEBUG_SYNC_DRIFT
	    debug_message("Subtracting %llu audio drift", drift);
#endif
	    m_videoSource->SubtractEncodingDrift(drift);
#endif
	  }
	}

	ForwardEncodedAudioFrames();
      }
#ifdef DEBUG_SYNC
    debug_message("audio src duration %llu dst %llu diff %lld", 
		  m_audioSrcElapsedDuration, startOutput, m_audioSrcElapsedDuration - startOutput);
#endif
  }

  // if desired, forward raw audio to sinks
  if (m_pConfig->SourceRawAudio() && pcmDataLength > 0) {

    // make a copy of the pcm data if needed
    u_int8_t* pcmForwardedData;

    if (!pcmMalloced) {
      pcmForwardedData = (u_int8_t*)Malloc(pcmDataLength);
      memcpy(pcmForwardedData, pcmData, pcmDataLength);
    } else {
      pcmForwardedData = pcmData;
      pcmMalloced = false;
    }
#ifndef WORDS_BIGENDIAN
    // swap byte ordering so we have big endian to write into
    // the file.
    uint16_t *pdata = (uint16_t *)pcmForwardedData;
    for (uint32_t ix = 0; 
	 ix < pcmDataLength; 
	 ix += sizeof(uint16_t),pdata++) {
      uint16_t swap = *pdata;
      *pdata = B2N_16(swap);
    }
#endif

    CMediaFrame* pFrame =
      new CMediaFrame(
		      PCMAUDIOFRAME, 
		      pcmForwardedData, 
		      pcmDataLength,
		      m_audioStartTimestamp 
		      + DstSamplesToTicks(m_audioDstRawSampleNumber),
		      DstBytesToSamples(pcmDataLength),
		      m_audioDstSampleRate);
    ForwardFrame(pFrame);

    m_audioDstRawSampleNumber += SrcBytesToSamples(pcmDataLength);
    m_audioDstRawFrameNumber++;
  }

  if (pcmMalloced) {
    free(pcmData);
  }

  if (pcmBuffered) {
    m_audioPreEncodingBufferLength -= pcmDataLength;
    memcpy(
	   &m_audioPreEncodingBuffer[0],
	   &m_audioPreEncodingBuffer[pcmDataLength],
	   m_audioPreEncodingBufferLength);

    goto pcmBufferCheck;
  }
}

void CMediaSource::ResampleAudio(
				 u_int8_t* frameData,
				 u_int32_t frameDataLength)
{
  uint32_t samplesIn;
  uint32_t samplesInConsumed;
  uint32_t outBufferSamplesLeft;
  uint32_t outBufferSamplesWritten;
  uint32_t chan_offset;

  samplesIn = DstBytesToSamples(frameDataLength);

  // so far, record the pre length
  while (samplesIn > 0) {
    outBufferSamplesLeft = 
      DstBytesToSamples(m_audioPreEncodingBufferMaxLength - 
			m_audioPreEncodingBufferLength);
    for (uint8_t chan_ix = 0; chan_ix < m_audioDstChannels; chan_ix++) {
      samplesInConsumed = samplesIn;
      outBufferSamplesWritten = outBufferSamplesLeft;

      chan_offset = chan_ix * (DstSamplesToBytes(1));
#ifdef DEBUG_AUDIO_RESAMPLER
      error_message("resample - chans %d %d, samples %d left %d", 
		    m_audioDstChannels, chan_ix,
		    samplesIn, outBufferSamplesLeft);
#endif

      if (st_resample_flow(m_audioResample[chan_ix],
			   (int16_t *)(frameData + chan_offset),
			   (int16_t *)(&m_audioPreEncodingBuffer[m_audioPreEncodingBufferLength + chan_offset]),
			   &samplesInConsumed, 
			   &outBufferSamplesWritten,
			   m_audioDstChannels) < 0) {
	error_message("resample failed");
      }
#ifdef DEBUG_AUDIO_RESAMPLER
      debug_message("Chan %d consumed %d wrote %d", 
		    chan_ix, samplesInConsumed, outBufferSamplesWritten);
#endif
    }
    if (outBufferSamplesLeft < outBufferSamplesWritten) {
      error_message("Written past end of buffer");
    }
    samplesIn -= samplesInConsumed;
    outBufferSamplesLeft -= outBufferSamplesWritten;
    m_audioPreEncodingBufferLength += DstSamplesToBytes(outBufferSamplesWritten);
    // If we have no room for new output data, and more to process,
    // give us a bunch more room...
    if (outBufferSamplesLeft == 0 && samplesIn > 0) {
      m_audioPreEncodingBufferMaxLength *= 2;
      m_audioPreEncodingBuffer = 
	(u_int8_t*)realloc(m_audioPreEncodingBuffer,
			   m_audioPreEncodingBufferMaxLength);
    }
  } // end while we still have input samples
}

void CMediaSource::ForwardEncodedAudioFrames(void)
{
  u_int8_t* pFrame;
  u_int32_t frameLength;
  u_int32_t frameNumSamples;

  while (m_audioEncoder->GetEncodedFrame(&pFrame, 
					 &frameLength, 
					 &frameNumSamples)) {

    // sanity check
    if (pFrame == NULL || frameLength == 0) {
      break;
    }

    // output has frame start timestamp
    Timestamp output = DstSamplesToTicks(m_audioDstSampleNumber);

    m_audioDstSampleNumber += frameNumSamples;
    m_audioDstFrameNumber++;

    // forward the encoded frame to sinks

#ifdef DEBUG_SYNC
    debug_message("audio forwarding %llu", output);
#endif
    CMediaFrame* pMediaFrame =
      new CMediaFrame(
		      m_audioEncoder->GetFrameType(),
		      pFrame, 
		      frameLength,
		      m_audioStartTimestamp + output,
		      frameNumSamples,
		      m_audioDstSampleRate);
    ForwardFrame(pMediaFrame);
  }
}

void CMediaSource::DoStopAudio()
{
  if (m_audioEncoder) {
    // flush remaining output from audio encoder
    // and forward it to sinks

    m_audioEncoder->EncodeSamples(NULL, 0, m_audioSrcChannels);

    ForwardEncodedAudioFrames();

    m_audioEncoder->Stop();
    delete m_audioEncoder;
    m_audioEncoder = NULL;
  }


  free(m_audioPreEncodingBuffer);
  m_audioPreEncodingBuffer = NULL;

  m_sourceAudio = false;
}
