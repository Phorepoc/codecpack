// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// phevdec.cpp -- OURS: phevdecsw.dll, the SOFTWARE DevVideo decoder hw device of the codec pack (decoding through the
// separately installed LGPL pheffmpeg.dll). Derived from the A64 VE decoder (adaptation-a64 src/phevdec, VDU rung V4;
// its PHE_SW build, moved here 2026-10-27 so every patent-grey decoder ships with the codec pack). PHE_MP4V (the ARM
// build) = MPEG-4 Part 2 only, beside the VE decoder; without it (x32) = everything pheffmpeg decodes.
#include <e32debug.h>
#include <hal.h>
#include <ecom/implementationproxy.h>
#include "phevdec.h"
#include <mm/mmcleanup.h>

#define PHTRACE(args) do { if (iTrace < 0) { ++iTrace; RDebug::Printf args; } } while (0)

// per-stage time (User::FastCounter ticks), printed when the stream ends: libavcodec send/parse/decode, the peek (frame finished), the
// allocation of a picture buffer, the YUV420P -> NV12 copy-out
static TUint32 gTSend, gTPeek, gTAlloc, gTCopy, gNPic;
static inline TUint32 Tk() { return User::FastCounter(); }

_LIT(KPheManufacturer, "Phorepoc");
#ifdef PHE_MP4V
_LIT(KPheIdentifier, "Software decoder (FFmpeg: MPEG-4 Part 2)");
#else
_LIT(KPheIdentifier, "Software decoder (FFmpeg: H.264 / MPEG-2 / MPEG-4 Part 2)");
#endif
_LIT8(KMimeH264, "video/H264");
_LIT8(KMimeMpeg, "video/mpeg");
_LIT8(KMimeMpeg2, "video/mpeg2");
_LIT8(KMimeMp2v, "video/mp2v");
_LIT8(KMimeMp4v, "video/mp4v-es");

// ---------------------------------------------------------------------------------------------------------------
// CPheVePool


// ---------------------------------------------------------------------------------------------------------------
// construction

CMMFVideoDecodeHwDevice* CPheVideoDecoder::NewL(TAny* /*aInitParams*/)
	{
	CPheVideoDecoder* d = new (ELeave) CPheVideoDecoder;
	CleanupStack::PushL(d);
	d->iStep = new (ELeave) CPheStep(*d);
	CleanupStack::Pop(d);
	return d;			// (single inheritance: the same address as the CMMFVideoDecodeHwDevice)
	}

CPheVideoDecoder::CPheVideoDecoder() : iMaxHeld(8), iDepth(1)
	{
	iMaxInputSize = 256 * 1024;
	iMinBuffers = 4;
	}

CPheVideoDecoder::~CPheVideoDecoder()
	{
	delete iStep;
	if (iVid && iFfClose)
		iFfClose(iVid);
	iFf.Close();
	for (TInt i = 0; i < KMaxOut; ++i)
		User::Free(iOut[i].iMem);
	iStream.Close();
	iChunks.Close();
	for (TInt i = 0; i < KMaxInput; ++i)
		delete iIn[i].iData;
	}

TAny* CPheVideoDecoder::CustomInterface(TUid /*aInterface*/)
	{
	// MMMFVideoSurfaceSupport (0x1028340E, zero-copy surfaces) is not implemented yet: the MMF controller falls back to
	// the raw-picture path (V4 follow-up)
	return NULL;
	}

void CPheVideoDecoder::SetProxy(MMMFDevVideoPlayProxy& aProxy)
	{
	iProxy = &aProxy;
	}

// ---------------------------------------------------------------------------------------------------------------
// information

void CPheVideoDecoder::SetupFormat(TUncompressedVideoFormat& aFormat) const
	{
	Mem::FillZ(&aFormat, sizeof(aFormat));
	aFormat.iDataFormat = EYuvRawData;
	aFormat.iYuvFormat.iCoefficients = EYuvBt601Range0;
	aFormat.iYuvFormat.iPattern = EYuv420Chroma1;
	aFormat.iYuvFormat.iDataLayout = EYuvDataSemiPlanar;
	aFormat.iYuvFormat.iAspectRatioNum = 1;
	aFormat.iYuvFormat.iAspectRatioDenom = 1;
	}

CPostProcessorInfo* CPheVideoDecoder::PostProcessorInfoLC()
	{
	RArray<TUncompressedVideoFormat> formats;
	CleanupClosePushL(formats);
	TUncompressedVideoFormat f;
	SetupFormat(f);
	User::LeaveIfError(formats.Append(f));
	RArray<TUint32> combs;
	CleanupClosePushL(combs);
	User::LeaveIfError(combs.Append(0));				// no post-processing: the picture as decoded
	RArray<TScaleFactor> scales;
	CleanupClosePushL(scales);
	TYuvToRgbCapabilities caps;
	Mem::FillZ(&caps, sizeof(caps));
	CPostProcessorInfo* info = CPostProcessorInfo::NewL(TUid::Uid(KPheVdecImplUid), KPheManufacturer, KPheIdentifier,
		TVersion(1, 0, 0), formats.Array(), combs.Array(), ETrue, EFalse, caps, 0, EFalse, scales.Array(), EFalse);
	CleanupStack::PopAndDestroy(3, &formats);
	CleanupStack::PushL(info);
	return info;
	}

void CPheVideoDecoder::GetOutputFormatListL(RArray<TUncompressedVideoFormat>& aFormats)
	{
	TUncompressedVideoFormat f;
	SetupFormat(f);
	User::LeaveIfError(aFormats.Append(f));
	}

void CPheVideoDecoder::SetOutputFormatL(const TUncompressedVideoFormat& aFormat)
	{
	if (aFormat.iDataFormat != EYuvRawData || aFormat.iYuvFormat.iDataLayout != EYuvDataSemiPlanar)
		User::Leave(KErrNotSupported);
	}

CVideoDecoderInfo* CPheVideoDecoder::VideoDecoderInfoLC()
	{
	RPointerArray<CCompressedVideoFormat> formats;
	CleanupResetAndDestroyPushL(formats);
#ifdef PHE_MP4V
	const TDesC8* mimes[] = { &KMimeMp4v };
#else
	const TDesC8* mimes[] = { &KMimeH264, &KMimeMpeg, &KMimeMpeg2, &KMimeMp2v, &KMimeMp4v };
#endif
	for (TUint i = 0; i < sizeof(mimes) / sizeof(mimes[0]); ++i)
		{
		CCompressedVideoFormat* f = CCompressedVideoFormat::NewL(*mimes[i]);
		CleanupStack::PushL(f);
		User::LeaveIfError(formats.Append(f));
		CleanupStack::Pop(f);
		}
	RArray<TPictureRateAndSize> rates;
	CleanupClosePushL(rates);
	TPictureRateAndSize r;
	r.iPictureRate = 60.0;
	r.iPictureSize = TSize(1920, 1088);
	User::LeaveIfError(rates.Append(r));
	r.iPictureRate = 30.0;
	r.iPictureSize = TSize(4096, 2304);
	User::LeaveIfError(rates.Append(r));
	CVideoDecoderInfo* info = CVideoDecoderInfo::NewL(TUid::Uid(KPheVdecImplUid), KPheManufacturer, KPheIdentifier,
		TVersion(1, 0, 0), formats.Array(), ETrue, EFalse, TSize(4096, 2304), 60 * 1000 * 1000, rates.Array(), EFalse,
		EFalse);
	CleanupStack::PopAndDestroy(2, &formats);
	CleanupStack::PushL(info);
	return info;
	}

// A header from the first data: H.264 -> the first SPS, MPEG-2 -> the sequence header. NULL = not enough data yet.
TVideoPictureHeader* CPheVideoDecoder::GetHeaderInformationL(TVideoDataUnitType /*aDataUnitType*/,
	TVideoDataUnitEncapsulation /*aEncapsulation*/, TVideoInputBuffer* aDataUnit)
	{
	if (!aDataUnit)
		User::Leave(KErrArgument);
	const TUint8* buf = aDataUnit->iData.Ptr();
	const size_t len = aDataUnit->iData.Length();
	Mem::FillZ(&iHeader, sizeof(iHeader));
	iHeader.iProfile = iHeader.iLevel = iHeader.iVersion = -1;
	iHeader.iIsRandomAccessible = ETrue;
	if (iCodec == EMpeg4)
		return NULL;						// (not parsed: the VOL header goes to libavcodec in band)
	if (iCodec != EMpeg2)
		{
		size_t pos = 0, s = 0, e = 0;
		while (h264::NextNal(buf, len, pos, s, e))
			{
			if ((buf[s] & 0x1F) != h264::ENalSps || pos + 3 > len)
				continue;
			h264::TParser* p = new (ELeave) h264::TParser;
			CleanupStack::PushL(p);
			TVideoPictureHeader* result = NULL;
			if (p->ParseSps(buf + s, e - s))
				{
				for (uint32_t id = 0; id < h264::KMaxSps; ++id)
					if (const h264::TSps* sps = p->Sps(id))
						{
						TUint32 ux = 2, uy = 2 * (2 - (sps->iFrameMbsOnly ? 1 : 0));
						iHeader.iProfile = sps->iProfileIdc;
						iHeader.iLevel = sps->iLevelIdc;
						iHeader.iSizeInMemory = TSize(sps->Width(), sps->Height());
						iHeader.iDisplayedRect = TRect(0, 0, sps->Width(), sps->Height());
						if (sps->iFrameCropping)
							iHeader.iDisplayedRect = TRect(sps->iCropLeft * ux, sps->iCropTop * uy,
								sps->Width() - sps->iCropRight * ux, sps->Height() - sps->iCropBottom * uy);
						result = &iHeader;
						break;
						}
				}
			CleanupStack::PopAndDestroy(p);
			if (result)
				{
				iCodec = EH264;
				return result;
				}
			}
		if (iCodec == EH264)
			return NULL;
		}
	// MPEG-2: a sequence header (00 00 01 B3 + 12 bit width, 12 bit height)
	for (size_t i = 0; i + 8 <= len; ++i)
		if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1 && buf[i + 3] == 0xB3)
			{
			const TInt w = (buf[i + 4] << 4) | (buf[i + 5] >> 4), h = ((buf[i + 5] & 0xF) << 8) | buf[i + 6];
			iHeader.iSizeInMemory = TSize((w + 15) & ~15, (h + 15) & ~15);
			iHeader.iDisplayedRect = TRect(0, 0, w, h);
			iCodec = EMpeg2;
			return &iHeader;
			}
	return NULL;
	}

void CPheVideoDecoder::ReturnHeader(TVideoPictureHeader* /*aHeader*/)
	{
	}

void CPheVideoDecoder::ConfigureDecoderL(const TVideoPictureHeader& /*aVideoPictureHeader*/)
	{
	// nothing to configure: the stream's own SPS / sequence header carries everything
	}

void CPheVideoDecoder::SetInputFormatL(const CCompressedVideoFormat& aFormat, TVideoDataUnitType aDataUnitType,
	TVideoDataUnitEncapsulation aEncapsulation, TBool aDataInOrder)
	{
	if (iInitialised)
		User::Leave(KErrNotReady);
	TPtrC8 mime = aFormat.MimeType();
	const TInt semi = mime.Locate(';');
	if (semi != KErrNotFound)
		mime.Set(mime.Left(semi));
	TCodec codec = ENoCodec;
	if (mime.CompareF(KMimeH264) == 0)
		codec = EH264;
	else if (mime.CompareF(KMimeMpeg) == 0 || mime.CompareF(KMimeMpeg2) == 0 || mime.CompareF(KMimeMp2v) == 0)
		codec = EMpeg2;
	else if (mime.CompareF(KMimeMp4v) == 0)
		codec = EMpeg4;						// software only (pheffmpeg)
#ifdef PHE_MP4V
	if (codec != EMpeg4)
		codec = ENoCodec;
#endif
	if (codec == ENoCodec || aEncapsulation != EDuElementaryStream || !aDataInOrder
		|| (aDataUnitType != EDuArbitraryStreamSection && aDataUnitType != EDuCodedPicture))
		User::Leave(KErrNotSupported);
	iCodec = codec;
	iComplete = aDataUnitType == EDuCodedPicture;
	}

void CPheVideoDecoder::SynchronizeDecoding(TBool /*aSynchronize*/)
	{
	// pictures are decoded as soon as data arrives and a free buffer exists; timestamps are passed through
	}

void CPheVideoDecoder::SetBufferOptionsL(const CMMFDevVideoPlay::TBufferOptions& aOptions)
	{
	if (aOptions.iMinNumInputBuffers > (TUint)KMaxInput)
		User::Leave(KErrNotSupported);
	iBufferOptions = aOptions;
	if (aOptions.iMinNumInputBuffers > (TUint)iMinBuffers)
		iMinBuffers = aOptions.iMinNumInputBuffers;
	if (aOptions.iMaxInputBufferSize)
		iMaxInputSize = aOptions.iMaxInputBufferSize;
	}

void CPheVideoDecoder::GetBufferOptions(CMMFDevVideoPlay::TBufferOptions& aOptions)
	{
	aOptions = iBufferOptions;
	aOptions.iMinNumInputBuffers = iMinBuffers;
	aOptions.iMaxInputBufferSize = iMaxInputSize;
	}

void CPheVideoDecoder::SetHrdVbvSpec(THrdVbvSpecification /*aHrdVbvSpec*/, const TDesC8& /*aHrdVbvParams*/)
	{
	}

void CPheVideoDecoder::SetOutputDevice(CMMFVideoPostProcHwDevice* aDevice)
	{
	// a post-processor chained after the decoder (the NGA one, 0x10273417, for surfaces) takes the pictures via
	// WritePictureL: not wired yet -- without it the pictures go to the client (MdvppNewPicture)
	(void)aDevice;
	}

// ---------------------------------------------------------------------------------------------------------------
// post-processing, display, timing: none (raw pictures to the client)

void CPheVideoDecoder::SetClockSource(MMMFClockSource* /*aClock*/) {}
void CPheVideoDecoder::SetVideoDestScreenL(TBool /*aScreen*/) {}

void CPheVideoDecoder::SetPostProcessTypesL(TUint32 aPostProcCombination)
	{
	if (aPostProcCombination != 0)
		User::Leave(KErrNotSupported);
	}

void CPheVideoDecoder::SetInputCropOptionsL(const TRect&) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::SetYuvToRgbOptionsL(const TYuvToRgbOptions&, const TYuvFormat&, TRgbFormat) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::SetYuvToRgbOptionsL(const TYuvToRgbOptions&) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::SetRotateOptionsL(TRotationType) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::SetScaleOptionsL(const TSize&, TBool) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::SetOutputCropOptionsL(const TRect&) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::SetPostProcSpecificOptionsL(const TDesC8&) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::CommitL() {}
void CPheVideoDecoder::Revert() {}
void CPheVideoDecoder::StartDirectScreenAccessL(const TRect&, CFbsScreenDevice&, const TRegion&) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::SetScreenClipRegion(const TRegion&) {}
void CPheVideoDecoder::SetPauseOnClipFail(TBool) {}
void CPheVideoDecoder::AbortDirectScreenAccess() {}
TBool CPheVideoDecoder::IsPlaying() { return iPlaying; }
void CPheVideoDecoder::Redraw() {}
void CPheVideoDecoder::Start() { iPlaying = ETrue; iPaused = EFalse; Schedule(); }
void CPheVideoDecoder::Stop() { iPlaying = EFalse; }
void CPheVideoDecoder::Pause() { iPaused = ETrue; }
void CPheVideoDecoder::Resume() { iPaused = EFalse; iPlaying = ETrue; Schedule(); }
void CPheVideoDecoder::SetPosition(const TTimeIntervalMicroSeconds&) {}
void CPheVideoDecoder::FreezePicture(const TTimeIntervalMicroSeconds&) {}
void CPheVideoDecoder::ReleaseFreeze(const TTimeIntervalMicroSeconds&) {}
TTimeIntervalMicroSeconds CPheVideoDecoder::PlaybackPosition() { return iDecodePos; }
TTimeIntervalMicroSeconds CPheVideoDecoder::DecodingPosition() { return iDecodePos; }

TUint CPheVideoDecoder::PictureBufferBytes()
	{
	TUint n = 0;
	for (TInt i = 0; i < KMaxOut; ++i)
		if (iOut[i].iState)
			n += iOut[i].iPtr.Length();
	return n;
	}

void CPheVideoDecoder::GetPictureCounters(CMMFDevVideoPlay::TPictureCounters& aCounters)
	{
	aCounters = iPicCounters;
	iPicCounters = CMMFDevVideoPlay::TPictureCounters();
	}

void CPheVideoDecoder::SetComplexityLevel(TUint) {}
TUint CPheVideoDecoder::NumComplexityLevels() { return 1; }

void CPheVideoDecoder::GetComplexityLevelInfo(TUint /*aLevel*/, CMMFDevVideoPlay::TComplexityLevelInfo& aInfo)
	{
	Mem::FillZ(&aInfo, sizeof(aInfo));
	}

TBool CPheVideoDecoder::GetSnapshotL(TPictureData&, const TUncompressedVideoFormat&) { return EFalse; }
void CPheVideoDecoder::GetTimedSnapshotL(TPictureData*, const TUncompressedVideoFormat&, const TTimeIntervalMicroSeconds&) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::GetTimedSnapshotL(TPictureData*, const TUncompressedVideoFormat&, const TPictureId&) { User::Leave(KErrNotSupported); }
void CPheVideoDecoder::CancelTimedSnapshot() {}
void CPheVideoDecoder::GetSupportedSnapshotFormatsL(RArray<TUncompressedVideoFormat>&) {}

TUint CPheVideoDecoder::PreDecoderBufferBytes()
	{
	return iStream.Length() - iCursor;
	}

void CPheVideoDecoder::GetBitstreamCounters(CMMFDevVideoPlay::TBitstreamCounters& aCounters)
	{
	aCounters = iBitCounters;
	iBitCounters = CMMFDevVideoPlay::TBitstreamCounters();
	}

// ---------------------------------------------------------------------------------------------------------------
// Initialize: the VE channel, the arena, the decoder core

void CPheVideoDecoder::ConstructStateL()
	{
	if (iCodec == ENoCodec)
		User::Leave(KErrNotReady);
	const TInt lr = iFf.Load(_L("pheffmpeg.dll"));
	if (lr != KErrNone)
		{
		RDebug::Printf("PH: dvp.sw pheffmpeg.dll not loaded (%d): the codec pack is not installed", lr);
		User::Leave(lr);
		}
	iFfOpen = (void* (*)(int, int))iFf.Lookup(6);
	iFfSend = (int (*)(void*, const unsigned char*, int, long long))iFf.Lookup(7);
	iFfRecv = (int (*)(void*, unsigned char*, int, int, int*, int*, long long*))iFf.Lookup(8);
	iFfClose = (void (*)(void*))iFf.Lookup(9);
	if (!iFfOpen || !iFfSend || !iFfRecv || !iFfClose)
		User::Leave(KErrNotSupported);
	const TInt cpus = 1;								// decoder threads: frame threading (2) was slower on the 2-CPU VM (measured, host busy) and adds latency; the flag stays in pheffmpeg
	iVid = iFfOpen(iCodec == EMpeg2 ? 1 : (iCodec == EMpeg4 ? 2 : 0), (iComplete ? 0 : 1) | (cpus << 8));
	if (!iVid)
		User::Leave(KErrNoMemory);
	iMaxHeld = 6;
	iStream.CreateL(64 * 1024);
	return;
	}

void CPheVideoDecoder::Initialize()
	{
	TRAPD(err, ConstructStateL());
	iInitialised = err == KErrNone;
	iProxy->MdvppInitializeComplete(this, err);
	}

// ---------------------------------------------------------------------------------------------------------------
// input

TUint CPheVideoDecoder::NumFreeBuffers()
	{
	TUint n = 0;
	for (TInt i = 0; i < iNumIn; ++i)
		n += iIn[i].iBusy ? 0 : 1;
	return n + (iNumIn < iMinBuffers ? iMinBuffers - iNumIn : 0);
	}

TVideoInputBuffer* CPheVideoDecoder::GetBufferL(TUint aBufferSize)
	{
	if (aBufferSize > (TUint)iMaxInputSize && aBufferSize > (TUint)(4 << 20))
		User::Leave(KErrTooBig);
	if (PreDecoderBufferBytes() > (TUint)KPreDecoderMax)
		{
		iWantBuffers = ETrue;					// flow control: the decoder is far behind; told when it caught up
		return NULL;
		}
	TInBuf* b = NULL;
	for (TInt i = 0; i < iNumIn && !b; ++i)
		if (!iIn[i].iBusy)
			b = &iIn[i];
	if (!b && iNumIn < iMinBuffers)
		b = &iIn[iNumIn++];
	if (!b)
		{
		iWantBuffers = ETrue;				// the client is told (MdvppNewBuffers) when one is free again
		return NULL;
		}
	if (!b->iData || (TUint)b->iData->Des().MaxLength() < aBufferSize)
		{
		RDebug::Printf("PH: dvp input buffer %d: %u B (asked %u, max input %d, backlog %u)", (TInt)(b - iIn), (aBufferSize > (TUint)iMaxInputSize ? aBufferSize : (TUint)iMaxInputSize), aBufferSize, iMaxInputSize, PreDecoderBufferBytes());
		HBufC8* d = HBufC8::NewL((aBufferSize > (TUint)iMaxInputSize ? aBufferSize : (TUint)iMaxInputSize));
		delete b->iData;
		b->iData = d;
		}
	b->iBuf = TVideoInputBuffer();
	TPtr8 p = b->iData->Des();
	p.SetLength(0);
	b->iBuf.iData.Set(p);
	b->iBuf.iUser = b;
	b->iBusy = ETrue;
	return &b->iBuf;
	}

void CPheVideoDecoder::WriteCodedDataL(TVideoInputBuffer* aBuffer)
	{
	if (!iInitialised || iFailed)
		User::Leave(KErrNotReady);
	if (!aBuffer || !aBuffer->iUser)
		User::Leave(KErrArgument);
	if (iFinal)
		User::Leave(KErrNotReady);
	TInBuf* b = (TInBuf*)aBuffer->iUser;
	PHTRACE(("PH: dvp.pl write b=%x len=%d stream=%d/%d", (TUint)b, aBuffer->iData.Length(), iStream.Length(), iStream.MaxLength()));
	const TInt add = aBuffer->iData.Length();
	if (add > 0)
		{
		if (iStream.Length() + add > iStream.MaxLength())
			iStream.ReAllocL(Max(iStream.Length() + add, iStream.MaxLength() * 2));
		TChunk c;
		c.iAbs = iStreamAbs + iStream.Length();
		c.iPts = (aBuffer->iOptions & TVideoInputBuffer::EPresentationTimestamp) ? aBuffer->iPresentationTimestamp.Int64()
			: (aBuffer->iOptions & TVideoInputBuffer::EDecodingTimestamp) ? aBuffer->iDecodingTimestamp.Int64() : -1;
		if (iChunks.Count() == 0 || iChunks[iChunks.Count() - 1].iPts != c.iPts || c.iPts >= 0)
			User::LeaveIfError(iChunks.Append(c));
		iStream.Append(aBuffer->iData);
		++iBitCounters.iTotalPackets;
		}
	b->iBusy = EFalse;
	Schedule();
	}

void CPheVideoDecoder::InputEnd()
	{
	iFinal = ETrue;
	Schedule();
	}

TInt64 CPheVideoDecoder::PtsAt(TInt64 aAbs) const
	{
	TInt64 pts = -1;
	for (TInt i = 0; i < iChunks.Count() && iChunks[i].iAbs <= aAbs; ++i)
		pts = iChunks[i].iPts;
	return pts;
	}

// forget chunks wholly behind the cursor (keep the one containing it)
void CPheVideoDecoder::PruneChunks()
	{
	const TInt64 cur = iStreamAbs + iCursor;
	TInt keep = 0;
	while (keep + 1 < iChunks.Count() && iChunks[keep + 1].iAbs <= cur)
		++keep;
	while (keep-- > 0)
		iChunks.Remove(0);
	}

void CPheVideoDecoder::Compact()
	{
	if (iCursor >= 256 * 1024 || (iCursor > 0 && iCursor == iStream.Length()))
		{
		iStream.Delete(0, iCursor);
		iStreamAbs += iCursor;
		iCursor = 0;
		PruneChunks();
		}
	}

// ---------------------------------------------------------------------------------------------------------------
// output

CPheVideoDecoder::TOutPic* CPheVideoDecoder::NewOut()
	{
	for (TInt i = 0; i < KMaxOut; ++i)
		if (!iOut[i].iState)
			{
			TOutPic* o = &iOut[i];
			Mem::FillZ(&o->iPic, sizeof(o->iPic));
			o->iState = 1;
			o->iFrame = -1;
			o->iBus = 0;
			return o;
			}
	return NULL;
	}

TInt CPheVideoDecoder::HeldCount() const
	{
	TInt n = 0;
	for (TInt i = 0; i < KMaxOut; ++i)
		n += iOut[i].iState ? 1 : 0;
	return n;
	}


void CPheVideoDecoder::Deliver(TOutPic* aOut)
	{
	aOut->iState = 2;
	TVideoPicture& p = aOut->iPic;
	p.iOptions |= TVideoPicture::ETimestamp;
	if (aOut->iPts >= 0)
		{
		p.iTimestamp = TTimeIntervalMicroSeconds(aOut->iPts);
		iSynthPts = aOut->iPts;
		iHavePts = ETrue;
		}
	else
		{
		iSynthPts += 40000;							// no input timestamps: 25 fps in display order
		p.iTimestamp = TTimeIntervalMicroSeconds(iSynthPts);
		}
	iDecodePos = p.iTimestamp;
	++iPicCounters.iPicturesDecoded;
	++iPicCounters.iPicturesDisplayed;
	++iPicCounters.iTotalPictures;
	PHTRACE(("PH: dvp.pl deliver poc %d ts %d bus %x", aOut->iPoc, (TInt)aOut->iPts, aOut->iBus));
	iProxy->MdvppNewPicture(&p);		// (the client may return the picture from inside this call)
	}

void CPheVideoDecoder::ReturnPicture(TVideoPicture* aPicture)
	{
	TOutPic* o = aPicture ? (TOutPic*)aPicture->iUser : NULL;
	PHTRACE(("PH: dvp.pl return %x", (TUint)o));
	if (!o || o < &iOut[0] || o >= &iOut[KMaxOut] || o->iState != 2)
		return;
	o->iState = 0;
	o->iFrame = -1;
	if (iBlocked)
		{
		iBlocked = EFalse;
		Schedule();
		}
	}


void CPheVideoDecoder::Fatal(TInt aError)
	{
	if (iFailed)
		return;
	iFailed = ETrue;
	iProxy->MdvppFatalError(this, aError);
	}

// ---------------------------------------------------------------------------------------------------------------
// decoding

void CPheVideoDecoder::Schedule()
	{
	if (iStep && iInitialised && !iFailed)
		iStep->Go();
	}

void CPheVideoDecoder::Step()
	{
	PHTRACE(("PH: dvp.pl step len=%d cur=%d final=%d held=%d", iStream.Length(), iCursor, iFinal, HeldCount()));
	if (iFailed || !iInitialised)
		return;
	if (iWantBuffers && NumFreeBuffers() > 0 && PreDecoderBufferBytes() < (TUint)(KPreDecoderMax / 2))
		{
		iWantBuffers = EFalse;
		iProxy->MdvppNewBuffers();			// a buffer is available again (the client's GetBufferL returned NULL)
		if (iFailed)
			return;
		}
	if (iPaused)
		return;
	if (HeldCount() >= iMaxHeld && !(iFinal && iStream.Length() == iCursor))
		{
		iBlocked = ETrue;				// the client holds the pictures: ReturnPicture() resumes
		return;
		}
	const TBool more = StepSw();
	Compact();
	if (iFailed)
		return;
	if (more)
		Schedule();
	else if (iFinal && !iEndSent && !iBlocked)
		FinishStream();
	}

void CPheVideoDecoder::FinishStream()
	{
	if (!iDrainSent)
		{
		iDrainSent = ETrue;
		iFfSend(iVid, NULL, 0, -1);
		}
	ReceiveSw();
	if (iBlocked || iFailed)
		return;						// the client still holds pictures: ReturnPicture() brings us back
	iEndSent = ETrue;
	{
	TInt f = 0;
	HAL::Get(HAL::EFastCounterFrequency, f);
	RDebug::Printf("PH: dvp.sw stages (FastCounter ticks at %d Hz; %d pictures): libavcodec send+decode %u, frame wait %u, picture alloc %u, YUV420P->NV12 copy-out %u",
		f, gNPic, gTSend, gTPeek, gTAlloc, gTCopy);
	gTSend = gTPeek = gTAlloc = gTCopy = gNPic = 0;
	}
	iProxy->MdvppStreamEnd();
	return;
	}

// ---------------------------------------------------------------------------------------------------------------
// software decoding (pheffmpeg.dll)


// Feed the stream up to the end of the chunk at the cursor (one client buffer = one pts), then take every picture that is ready.
TBool CPheVideoDecoder::StepSw()
	{
	const TInt len = iStream.Length();
	if (!iPreallocDone && len > 0)
		{
		// pre-allocate (and touch) the picture buffers the first frames will use, BEFORE the first picture exists (the controller starts its clock at the first
		// picture): growing the heap during playback cost 100-800 ms per picture for the first ~7 (VirtualBox stage timing: "picture alloc"). The size is read
		// from the SPS in the first buffer.
		iPreallocDone = ETrue;
		TVideoInputBuffer tmp;
		tmp.iData.Set(TPtr8((TUint8*)iStream.Ptr(), len, len));
		TVideoPictureHeader* hdr = NULL;
		TRAPD(he, hdr = GetHeaderInformationL(EDuCodedPicture, EDuElementaryStream, &tmp));
		if (he == KErrNone && hdr && iHeader.iSizeInMemory.iWidth > 0 && iHeader.iSizeInMemory.iHeight > 0)
			{
			const TInt pitch = (iHeader.iSizeInMemory.iWidth + 15) & ~15, rows = (iHeader.iSizeInMemory.iHeight + 15) & ~15, size = pitch * rows * 3 / 2;
			TInt n = 0;
			for (TInt i = 0; i < iMaxHeld + 2 && i < KMaxOut; ++i)
				{
				iOut[i].iMem = (TUint8*)User::Alloc(size);
				if (!iOut[i].iMem)
					break;
				iOut[i].iMemSize = size;
				Mem::FillZ(iOut[i].iMem, size);
				++n;
				}
			RDebug::Printf("PH: dvp.sw preallocated %d picture buffers of %d KB (%dx%d)", n, size >> 10, iHeader.iSizeInMemory.iWidth, iHeader.iSizeInMemory.iHeight);
			}
		else
			RDebug::Printf("PH: dvp.sw no SPS in the first buffer (%d): buffers allocated on first use", he);
		}
	TBool fed = EFalse;
	if (iCursor < len)
		{
		const TInt64 abs = iStreamAbs + iCursor;
		TInt end = len;
		for (TInt i = 0; i < iChunks.Count(); ++i)
			if (iChunks[i].iAbs > abs)
				{
				end = (TInt)(iChunks[i].iAbs - iStreamAbs);
				break;
				}
		const TUint32 t0 = Tk();
		TInt r = iFfSend(iVid, iStream.Ptr() + iCursor, end - iCursor, PtsAt(abs));
		gTSend += Tk() - t0;
		if (r < 0)
			{
			if (iErrors < 4)
				RDebug::Printf("PH: dvp.sw send error %d (%d bytes at %d)", r, end - iCursor, iCursor);
			if (iErrors++ == 0)
				iFirstError = r;
			++iBitCounters.iLostPackets;
			}
		iCursor = end;
		fed = ETrue;
		}
	ReceiveSw();
	return fed && !iBlocked && !iFailed;
	}

// Deliver every decoded picture (display order, as libavcodec reorders) until none is left or the client holds too many.
void CPheVideoDecoder::ReceiveSw()
	{
	for (;;)
		{
		if (HeldCount() >= iMaxHeld)
			{
			iBlocked = ETrue;
			return;
			}
		int w = 0, h = 0;
		long long pts = -1;
		// peek: the frame size first (NULL destination keeps the frame inside the decoder)
		const TUint32 t0 = Tk();
		TInt r = iFfRecv(iVid, NULL, 0, 0, &w, &h, &pts);
		gTPeek += Tk() - t0;
		if (r < 0)
			{
			if (iErrors < 4)
				RDebug::Printf("PH: dvp.sw receive error %d", r);
			if (iErrors++ == 0)
				iFirstError = r;
			++iBitCounters.iLostPackets;
			return;
			}
		if (r == 0)
			return;
		const TInt pitch = (w + 15) & ~15, rows = (h + 15) & ~15, size = pitch * rows * 3 / 2;
		TOutPic* o = NewOut();
		if (!o)
			{
			Fatal(KErrOverflow);
			return;
			}
		const TUint32 t1 = Tk();
		if (o->iMemSize != size)
			{
			User::Free(o->iMem);
			o->iMem = (TUint8*)User::Alloc(size);
			o->iMemSize = o->iMem ? size : 0;
			}
		if (!o->iMem)
			{
			o->iState = 0;
			Fatal(KErrNoMemory);
			return;
			}
		const TUint32 t2 = Tk();
		gTAlloc += t2 - t1;
		r = iFfRecv(iVid, o->iMem, pitch, rows, &w, &h, &pts);
		gTCopy += Tk() - t2;
		++gNPic;
		if (r != 1)
			{
			o->iState = 0;
			if (iErrors++ == 0)
				iFirstError = r;
			++iBitCounters.iLostPackets;
			return;
			}
		o->iPts = pts;
		o->iPtr.Set(o->iMem, size, size);
		TVideoPicture& vp = o->iPic;
		vp.iData.iDataFormat = EYuvRawData;
		vp.iData.iDataSize = TSize(pitch, rows);
		vp.iData.iRawData = &o->iPtr;
		vp.iOptions = TVideoPicture::ECropRect;
		vp.iCropRect = TRect(0, 0, w, h);
		vp.iUser = o;
		Deliver(o);
		if (iFailed)
			return;
		}
	}

// ---------------------------------------------------------------------------------------------------------------
// ECOM

const TImplementationProxy KImplementationTable[] =
	{
	IMPLEMENTATION_PROXY_ENTRY(KPheVdecImplUid, CPheVideoDecoder::NewL)
	};

EXPORT_C const TImplementationProxy* ImplementationGroupProxy(TInt& aTableCount)
	{
	aTableCount = sizeof(KImplementationTable) / sizeof(TImplementationProxy);
	return KImplementationTable;
	}
