/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: Apache-2.0 */

// phevdec.h -- OURS: phevdecsw.dll, the codec pack's SOFTWARE DevVideo decoder hardware device. An ECOM plug-in on
// interface 0x101FB4BE (KUidDevVideoDecoderHwDevice) implementing CMMFVideoDecodeHwDevice, found and driven by
// CMMFDevVideoPlay (devvideo.dll) like any 2010 decoder; the decoding is pheffmpeg.dll's (LGPL FFmpeg, PheVid* ordinals).
// Installed with the codec pack, never part of an OS image (H.264 / MPEG-2 / MPEG-4 Part 2 are patent-grey).
//
// Formats (SetInputFormatL; the ARM build, PHE_MP4V, takes only video/mp4v-es -- the A64 VE decoder does the others):
//   video/H264                    Annex B byte stream, SPS + PPS in band
//   video/mpeg, video/mpeg2, video/mp2v   MPEG-2 video elementary stream
//   video/mp4v-es                 MPEG-4 Part 2 elementary stream, the VOL header in band before the first picture
//   Data unit: EDuElementaryStream with
//     EDuArbitraryStreamSection   any chunking of the byte stream (libavcodec's parser splits it)
//     EDuCodedPicture             each buffer holds whole access unit(s)
//   aDataInOrder must be ETrue (decoding order).
// Output: TVideoPicture, iData.iDataFormat = EYuvRawData, NV12 (a Y plane, then interleaved U V at the same pitch) in a
//   heap buffer owned by the device, iDataSize = (pitch, 16-aligned height), iCropRect = the displayed area; DISPLAY
//   order (libavcodec reorders), timestamp = the presentation timestamp of the input. Return each with ReturnPicture().
//
// Threading: the device runs in the creating (client) thread, one decode step per active-object run. Needs an active
// scheduler. If pheffmpeg.dll is missing, Initialize() reports KErrNotFound (no codec pack installed).
#ifndef PHEVDEC_H
#define PHEVDEC_H

#include <e32base.h>
#include <mmf/devvideo/videoplayhwdevice.h>
// The software decoder: the same DevVideo decoder device as the VE one, decoding through the separate LGPL
// pheffmpeg.dll (PheVid* ordinals). Nothing VE-specific (rphevde / ve_h264 / ve_mpeg2 / the arena pool).
#include "h264_parser.h"

const TInt KPheVdecImplUid = 0xE70157FD;		// phevdecsw.dll (UID3 0xE70157FC), both architectures


class CPheStep;

class CPheVideoDecoder : public CMMFVideoDecodeHwDevice
	{
public:
	static CMMFVideoDecodeHwDevice* NewL(TAny* aInitParams);
	~CPheVideoDecoder();

	// from CMMFVideoHwDevice
	TAny* CustomInterface(TUid aInterface);

	// from CMMFVideoPlayHwDevice
	CPostProcessorInfo* PostProcessorInfoLC();
	void GetOutputFormatListL(RArray<TUncompressedVideoFormat>& aFormats);
	void SetOutputFormatL(const TUncompressedVideoFormat& aFormat);
	void SetClockSource(MMMFClockSource* aClock);
	void SetVideoDestScreenL(TBool aScreen);
	void SetPostProcessTypesL(TUint32 aPostProcCombination);
	void SetInputCropOptionsL(const TRect& aRect);
	void SetYuvToRgbOptionsL(const TYuvToRgbOptions& aOptions, const TYuvFormat& aYuvFormat, TRgbFormat aRgbFormat);
	void SetYuvToRgbOptionsL(const TYuvToRgbOptions& aOptions);
	void SetRotateOptionsL(TRotationType aRotationType);
	void SetScaleOptionsL(const TSize& aTargetSize, TBool aAntiAliasFiltering);
	void SetOutputCropOptionsL(const TRect& aRect);
	void SetPostProcSpecificOptionsL(const TDesC8& aOptions);
	void Initialize();
	void CommitL();
	void Revert();
	void StartDirectScreenAccessL(const TRect& aVideoRect, CFbsScreenDevice& aScreenDevice, const TRegion& aClipRegion);
	void SetScreenClipRegion(const TRegion& aRegion);
	void SetPauseOnClipFail(TBool aPause);
	void AbortDirectScreenAccess();
	TBool IsPlaying();
	void Redraw();
	void Start();
	void Stop();
	void Pause();
	void Resume();
	void SetPosition(const TTimeIntervalMicroSeconds& aPlaybackPosition);
	void FreezePicture(const TTimeIntervalMicroSeconds& aTimestamp);
	void ReleaseFreeze(const TTimeIntervalMicroSeconds& aTimestamp);
	TTimeIntervalMicroSeconds PlaybackPosition();
	TUint PictureBufferBytes();
	void GetPictureCounters(CMMFDevVideoPlay::TPictureCounters& aCounters);
	void SetComplexityLevel(TUint aLevel);
	TUint NumComplexityLevels();
	void GetComplexityLevelInfo(TUint aLevel, CMMFDevVideoPlay::TComplexityLevelInfo& aInfo);
	void ReturnPicture(TVideoPicture* aPicture);
	TBool GetSnapshotL(TPictureData& aPictureData, const TUncompressedVideoFormat& aFormat);
	void GetTimedSnapshotL(TPictureData* aPictureData, const TUncompressedVideoFormat& aFormat,
		const TTimeIntervalMicroSeconds& aPresentationTimestamp);
	void GetTimedSnapshotL(TPictureData* aPictureData, const TUncompressedVideoFormat& aFormat,
		const TPictureId& aPictureId);
	void CancelTimedSnapshot();
	void GetSupportedSnapshotFormatsL(RArray<TUncompressedVideoFormat>& aFormats);
	void InputEnd();

	// from CMMFVideoDecodeHwDevice
	CVideoDecoderInfo* VideoDecoderInfoLC();
	TVideoPictureHeader* GetHeaderInformationL(TVideoDataUnitType aDataUnitType,
		TVideoDataUnitEncapsulation aEncapsulation, TVideoInputBuffer* aDataUnit);
	void ReturnHeader(TVideoPictureHeader* aHeader);
	void SetInputFormatL(const CCompressedVideoFormat& aFormat, TVideoDataUnitType aDataUnitType,
		TVideoDataUnitEncapsulation aEncapsulation, TBool aDataInOrder);
	void SynchronizeDecoding(TBool aSynchronize);
	void SetBufferOptionsL(const CMMFDevVideoPlay::TBufferOptions& aOptions);
	void GetBufferOptions(CMMFDevVideoPlay::TBufferOptions& aOptions);
	void SetHrdVbvSpec(THrdVbvSpecification aHrdVbvSpec, const TDesC8& aHrdVbvParams);
	void SetOutputDevice(CMMFVideoPostProcHwDevice* aDevice);
	TTimeIntervalMicroSeconds DecodingPosition();
	TUint PreDecoderBufferBytes();
	void GetBitstreamCounters(CMMFDevVideoPlay::TBitstreamCounters& aCounters);
	TUint NumFreeBuffers();
	TVideoInputBuffer* GetBufferL(TUint aBufferSize);
	void WriteCodedDataL(TVideoInputBuffer* aBuffer);
	void ConfigureDecoderL(const TVideoPictureHeader& aVideoPictureHeader);
	void SetProxy(MMMFDevVideoPlayProxy& aProxy);

	// the decode step (CPheStep::RunL)
	void Step();

private:
	enum TCodec { ENoCodec, EH264, EMpeg2, EMpeg4 };
	// software: the coded backlog lives in the CLIENT's heap (iStream doubles as it grows): a 60 s clip fed at once into
	// a game's heap failed to grow 8 -> 16 MB (KErrNoMemory, Let's Golf 2 intro, 2026-10-27) -- keep it small
	enum { KMaxInput = 12, KMaxOut = 40, KPreDecoderMax = 1 << 20, KSliceBuf = 1 << 20, KArenaMb = 64 };
	struct TInBuf
		{
		TVideoInputBuffer iBuf;
		HBufC8* iData;
		TBool iBusy;
		};
	struct TChunk
		{
		TInt64 iAbs;			// absolute stream offset of the chunk's first byte
		TInt64 iPts;			// microseconds, -1 = none
		};
	struct TOutPic
		{
		TVideoPicture iPic;
		TPtr8 iPtr;
		TInt iState;			// 0 free, 1 queued for reordering, 2 delivered to the client
		TUint8* iMem;			// PHE_SW: the NV12 picture (heap, owned)
		TInt iMemSize;
		TUint32 iBus;			// H.264: the luma bus address (the pool key)
		TInt iFrame;			// MPEG-2: the frame buffer index
		TUint32 iPeriod;
		TInt32 iPoc;
		TInt64 iPts;
		TOutPic() : iPtr(NULL, 0, 0), iMem(NULL), iMemSize(0) {}
		};

	CPheVideoDecoder();
	void ConstructStateL();
	void Schedule();
	TBool StepSw();
	void ReceiveSw();
	void FinishStream();
	TInt64 PtsAt(TInt64 aAbs) const;
	void PruneChunks();
	void Compact();
	TOutPic* NewOut();
	TInt HeldCount() const;
	void Deliver(TOutPic* aOut);
	void Fatal(TInt aError);
	void SetupFormat(TUncompressedVideoFormat& aFormat) const;

	MMMFDevVideoPlayProxy* iProxy;
	CPheStep* iStep;
	TCodec iCodec;
	TBool iComplete;				// EDuCodedPicture: each buffer holds complete units
	TBool iInitialised, iFailed, iFinal, iEndSent, iPaused, iPlaying, iBlocked, iWantBuffers;
	// the software decoder (pheffmpeg.dll, loaded on Initialize)
	RLibrary iFf;
	void* iVid;
	TBool iDrainSent;
	TBool iPreallocDone;
	void* (*iFfOpen)(int, int);
	int (*iFfSend)(void*, const unsigned char*, int, long long);
	int (*iFfRecv)(void*, unsigned char*, int, int, int*, int*, long long*);
	void (*iFfClose)(void*);
	// input
	TInBuf iIn[KMaxInput];
	TInt iNumIn;
	RBuf8 iStream;
	TInt64 iStreamAbs;
	TInt iCursor;
	RArray<TChunk> iChunks;
	TInt iMinBuffers, iMaxInputSize;
	// output
	TOutPic iOut[KMaxOut];
	TInt iMaxHeld, iDepth;
	TInt64 iCurPts, iSynthPts;
	TBool iHavePts;
	TRect iCurCrop;
	TInt iPicSeq;
	TOutPic* iM2Pending;
	TUint32 iLastPeriod;
	// statistics
	CMMFDevVideoPlay::TPictureCounters iPicCounters;
	CMMFDevVideoPlay::TBitstreamCounters iBitCounters;
	TInt iErrors, iFirstError, iTrace;
	TTimeIntervalMicroSeconds iDecodePos;
	TVideoPictureHeader iHeader;
	CMMFDevVideoPlay::TBufferOptions iBufferOptions;
	};

class CPheStep : public CActive
	{
public:
	CPheStep(CPheVideoDecoder& aOwner) : CActive(EPriorityLow), iOwner(aOwner) { CActiveScheduler::Add(this); }
	~CPheStep() { Cancel(); }
	void Go()
		{
		if (!IsActive())
			{
			iStatus = KRequestPending;
			SetActive();
			TRequestStatus* s = &iStatus;
			User::RequestComplete(s, KErrNone);
			}
		}
private:
	void RunL() { iOwner.Step(); }
	void DoCancel() {}
	CPheVideoDecoder& iOwner;
	};

#endif
