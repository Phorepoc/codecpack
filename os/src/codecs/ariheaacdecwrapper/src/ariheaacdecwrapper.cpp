// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// ariheaacdecwrapper.cpp -- OURS, part of the OPTIONAL AAC codec pack (AAC is a
// grey area -> published as source + build instructions only, never in a prebuilt image; recipes/).
//
// The HE-AAC decoder library the release's Aricent plug-in links (oss.FCL.sf.adapt.codecs.aricent/heaac_dec: only its
// MMF wrapper and this API's header were published). It implements that API (ariheaacdecwrapper.h) with FFmpeg's AAC
// decoder (libavcodec, LGPL 2.1+, fetched and configured by recipes/aac_codecpack.sh).
//
// The contract, as ariheaacdecmmfcodec.cpp drives it:
//   Reset(TAacFrameInfo*)          (re)start with the stream's format (rate, channels, profile, SBR = ext object type)
//   Decode(src, used, dst, outLen)  src = the buffered stream bytes, `used` in = available / out = consumed;
//                                    dst = one frame's PCM space (TAacFrameInfo::iOutFrameSize), outLen out = bytes;
//                                    src == NULL = the stream ended (flush)
//   FormatType / GetFrameInfo       stream probing (the controller does its own; kept for completeness)
// Streams: ADTS (the .aac files the AAC controller plays; FFmpeg's AAC parser frames them). Raw access units without
// framing (MP4/3GP) are not handled yet: the caller gives no AU boundaries.
// Output: interleaved signed 16-bit PCM (FFmpeg decodes to planar float; converted here with clipping).
#include <e32base.h>
#include "ariheaacdecwrapper.h"

// FFmpeg's headers need the C99 <stdint.h> macros in C++ too
#define __STDC_CONSTANT_MACROS
#define __STDC_LIMIT_MACROS
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/log.h"
}
#include <stdio.h>
#include <e32debug.h>

namespace
{
// FFmpeg's default log callback keeps ~4 KB of line buffers on the stack -- the MMF controller thread that runs us
// has 8 KB (KERN-EXEC 3 at the stack guard). Warnings and errors go to the debug port through a small buffer instead.
void LogToDebugPort(void* /*aAvcl*/, int aLevel, const char* aFmt, va_list aArgs)
	{
	if (aLevel > AV_LOG_WARNING)
		return;
	char line[160];
	vsnprintf(line, sizeof(line), aFmt, aArgs);
	RDebug::Printf("[ffmpeg-aac] %s", line);
	}

// ariheaacdecmmfcodec.h's TAacFrameInfo (the layout the plug-in hands us as TAny*)
struct TAacFrameInfo
	{
	TInt iOutFrameSize;
	TInt iNoOfSamples;
	TInt iSamplingFrequency;
	TInt iNoOfChannels;
	TInt iProfile;
	TInt iOutSamplingFrequency;
	TInt iExtObjectType;
	TInt iDownSampledMode;
	};

TInt16 ToS16(float aSample)
	{
	const float v = aSample * 32768.0f;
	return v >= 32767.0f ? 32767 : v <= -32768.0f ? -32768 : TInt16(v);
	}

// FFmpeg runs on a worker thread of ours: the caller (the MMF controller thread, KDefaultStackSize = 8 KB unless the
// controller's ECom data says otherwise) has too little stack for it -- the MDCT set-up alone
// (ff_tx_decompose_length) takes an 8.8 KB frame, parametric stereo's decorrelation as much. The worker shares the
// caller's heap; each call is a synchronous hand-over (one context switch per frame).
const TInt KWorkerStackSize = 0x10000;

class CFfmpegAacDec : public CAriHeAacDecWrapper
	{
public:
	static CFfmpegAacDec* NewL()
		{
		CFfmpegAacDec* self = new (ELeave) CFfmpegAacDec;
		CleanupStack::PushL(self);
		self->ConstructL();
		CleanupStack::Pop(self);
		return self;
		}

	~CFfmpegAacDec()
		{
		if (iWorkerRunning)
			{
			Call(EOpQuit);
			TRequestStatus exited;
			iWorker.Logon(exited);
			User::WaitForRequest(exited);
			}
		iWorker.Close();
		iGo.Close();
		}

	TInt Reset(TAny* /*aFrameInfo*/)
		{
		return Call(EOpReset);
		}

	TInt Decode(TUint8* aSrcBuf, TInt& aSrcUsed, TUint8* aDstBuf, TInt& aOutSize)
		{
		iSrc = aSrcBuf;
		iSrcUsed = &aSrcUsed;
		iDst = aDstBuf;
		iOutSize = &aOutSize;
		return Call(EOpDecode);
		}

	TInt FormatType(TUint8* aData)
		{
		// the Aricent TEAacPlusFormatType values the controller uses: 2 = ADTS, 1 = ADIF, 0 = raw
		if (aData[0] == 0xFF && (aData[1] & 0xF6) == 0xF0)
			return 2;
		if (aData[0] == 'A' && aData[1] == 'D' && aData[2] == 'I' && aData[3] == 'F')
			return 1;
		return 0;
		}

	TInt GetFrameInfo(TUint8* aHeaderBuffer, TInt& aHeaderSize, TAny* aFrameInfo, TInt /*aFormat*/,
					  TUint8* /*aDataBuffer*/, TInt /*aDataSize*/)
		{
		// ADTS header: rate index, channel configuration, profile (7 bytes; the frame length is in it too)
		if (aHeaderSize < 7 || FormatType(aHeaderBuffer) != 2)
			return KErrNotSupported;
		static const TInt KRates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025,
									   8000, 7350 };
		TAacFrameInfo* info = static_cast<TAacFrameInfo*>(aFrameInfo);
		const TInt rateIndex = (aHeaderBuffer[2] >> 2) & 0xF;
		if (rateIndex > 12)
			return KErrCorrupt;
		info->iProfile = aHeaderBuffer[2] >> 6;
		info->iSamplingFrequency = KRates[rateIndex];
		info->iNoOfChannels = ((aHeaderBuffer[2] & 1) << 2) | (aHeaderBuffer[3] >> 6);
		info->iNoOfSamples = 1024;
		info->iOutSamplingFrequency = info->iSamplingFrequency;
		info->iOutFrameSize = 2048 * 2 * 2;			// room for SBR (2048 samples) x 2 channels x 16 bit
		info->iExtObjectType = 0;
		info->iDownSampledMode = 0;
		aHeaderSize = 7;
		return KErrNone;
		}

private:
	enum TOp { EOpReset, EOpDecode, EOpQuit };

	void ConstructL()
		{
		User::LeaveIfError(iGo.CreateLocal(0));
		TName name;
		name.Format(_L("FfmpegAac%08x"), reinterpret_cast<TUint>(this));
		User::LeaveIfError(iWorker.Create(name, WorkerMain, KWorkerStackSize, NULL, this));	// NULL = our heap
		iWorker.SetPriority(RThread().Priority());
		iWorker.Resume();
		iWorkerRunning = ETrue;
		}

	// hand one operation to the worker and wait for it (its rendezvous; or its death -> KErrDied, not a hang)
	TInt Call(TOp aOp)
		{
		TRequestStatus done;
		iWorker.Rendezvous(done);
		iOp = aOp;
		iGo.Signal();
		User::WaitForRequest(done);
		if (iWorker.ExitType() != EExitPending)
			{
			iWorkerRunning = EFalse;
			return KErrDied;
			}
		return iResult;
		}

	static TInt WorkerMain(TAny* aSelf)
		{
		CFfmpegAacDec* self = static_cast<CFfmpegAacDec*>(aSelf);
		for (;;)
			{
			self->iGo.Wait();
			const TOp op = self->iOp;
			if (op == EOpReset)
				self->iResult = self->DoReset();
			else if (op == EOpDecode)
				self->iResult = self->DoDecode(self->iSrc, *self->iSrcUsed, self->iDst, *self->iOutSize);
			else
				self->Close();
			RThread::Rendezvous(KErrNone);
			if (op == EOpQuit)
				return KErrNone;
			}
		}

	TInt DoReset()
		{
		Close();
		av_log_set_callback(LogToDebugPort);
		const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
		if (!codec)
			return KErrNotSupported;
		iCtx = avcodec_alloc_context3(codec);
		iParser = av_parser_init(AV_CODEC_ID_AAC);
		iPacket = av_packet_alloc();
		iFrame = av_frame_alloc();
		if (!iCtx || !iParser || !iPacket || !iFrame)
			return KErrNoMemory;
		return avcodec_open2(iCtx, codec, NULL) < 0 ? KErrGeneral : KErrNone;
		}

	TInt DoDecode(TUint8* aSrcBuf, TInt& aSrcUsed, TUint8* aDstBuf, TInt& aOutSize)
		{
		if (!iCtx && DoReset() != KErrNone)
			return KErrGeneral;
		const TInt capacity = aOutSize;
		aOutSize = 0;
		if (!aSrcBuf)
			{										// end of stream: flush the parser's last frame
			aSrcUsed = 0;
			return Feed(NULL, 0, aDstBuf, capacity, aOutSize);
			}
		// the parser takes as much as it wants and returns a frame once it has one; one frame per call (the caller
		// sized dst for one frame)
		TInt used = 0;
		while (used < aSrcUsed && aOutSize == 0)
			{
			uint8_t* frame = NULL;
			int frameSize = 0;
			const int n = av_parser_parse2(iParser, iCtx, &frame, &frameSize, aSrcBuf + used, aSrcUsed - used,
										   AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
			if (n < 0)
				return KErrCorrupt;
			used += n;
			if (frameSize > 0)
				{
				const TInt r = Feed(frame, frameSize, aDstBuf, capacity, aOutSize);
				if (r != KErrNone)
					return r;
				}
			if (n == 0 && frameSize == 0)
				break;
			}
		aSrcUsed = used;
		return KErrNone;
		}

	// decode one parsed frame (or flush: aData == NULL) and write interleaved s16
	TInt Feed(uint8_t* aData, int aSize, TUint8* aDst, TInt aCapacity, TInt& aOutSize)
		{
		if (aData)
			{
			iPacket->data = aData;
			iPacket->size = aSize;
			if (avcodec_send_packet(iCtx, iPacket) < 0)
				return KErrNone;					// a bad frame: skip it, as a player would
			}
		else if (avcodec_send_packet(iCtx, NULL) < 0)
			return KErrNone;
		while (avcodec_receive_frame(iCtx, iFrame) == 0)
			{
			const TInt ch = iFrame->ch_layout.nb_channels;
			const TInt bytes = iFrame->nb_samples * ch * 2;
			if (iFrame->format == AV_SAMPLE_FMT_FLTP && aOutSize + bytes <= aCapacity)
				{
				TInt16* out = reinterpret_cast<TInt16*>(aDst + aOutSize);
				for (TInt s = 0; s < iFrame->nb_samples; s++)
					for (TInt c = 0; c < ch; c++)
						*out++ = ToS16(reinterpret_cast<const float*>(iFrame->extended_data[c])[s]);
				aOutSize += bytes;
				}
			av_frame_unref(iFrame);
			}
		return KErrNone;
		}

	void Close()
		{
		if (iParser)
			av_parser_close(iParser);
		iParser = NULL;
		av_packet_free(&iPacket);
		av_frame_free(&iFrame);
		avcodec_free_context(&iCtx);
		}

	AVCodecContext* iCtx;					// CBase zero-fills
	AVCodecParserContext* iParser;
	AVPacket* iPacket;
	AVFrame* iFrame;

	RThread iWorker;
	RSemaphore iGo;
	TBool iWorkerRunning;
	TOp iOp;								// the call being handed over, and its arguments / result
	TUint8* iSrc;
	TInt* iSrcUsed;
	TUint8* iDst;
	TInt* iOutSize;
	TInt iResult;
	};
}

EXPORT_C CAriHeAacDecWrapper* CAriHeAacDecWrapper::NewL()
	{
	return CFfmpegAacDec::NewL();
	}

CAriHeAacDecWrapper::~CAriHeAacDecWrapper()
	{
	}
