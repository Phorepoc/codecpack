// Copyright The Phorepoc Authors
// SPDX-License-Identifier: EPL-1.0

// eaacplusutil.cpp -- OURS (the optional AAC codec pack): the AAC stream-header utility the S60
// AAC controller uses (mmaudio_plat/eaacplusutil_api). The release ships only a stub (os.mmaudio/eaacplusutil_stub:
// every call returns KErrNone and fills nothing in); this parses the headers for real, behind that stub's frozen
// export list. Header parsing only -- decoding is the Aricent plug-in + ariheaacdecwrapper.
//
// ADTS (ISO 14496-3 1.A.2): 7-byte header (9 with CRC); profile, sampling-frequency index, channel configuration,
// the 13-bit frame length. ADIF: detected, not parsed (rare; the controller then reports it unsupported).
#include <e32base.h>
#include <eaacplusutil.h>

namespace
{
const TInt KRates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

TBool IsAdts(const TUint8* aBuf, TInt aLen)
	{
	return aLen >= 7 && aBuf[0] == 0xFF && (aBuf[1] & 0xF6) == 0xF0;	// sync 0xFFF, layer 00
	}
}

CEAacPlusUtil::CEAacPlusUtil()
	{
	}

void CEAacPlusUtil::ConstructL()
	{
	iFormat = EAacPlusFormatRaw;
	}

EXPORT_C CEAacPlusUtil* CEAacPlusUtil::NewL()
	{
	CEAacPlusUtil* self = new (ELeave) CEAacPlusUtil;
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
	}

EXPORT_C CEAacPlusUtil::~CEAacPlusUtil()
	{
	}

EXPORT_C TInt CEAacPlusUtil::GetAacFormatType(const TUint8* aBuf, TInt aBufLen, TEAacPlusFormatType& aFormatType)
	{
	if (aBufLen >= 4 && aBuf[0] == 'A' && aBuf[1] == 'D' && aBuf[2] == 'I' && aBuf[3] == 'F')
		aFormatType = EAacPlusFormatAdif;
	else if (IsAdts(aBuf, aBufLen))
		aFormatType = EAacPlusFormatAdts;
	else
		aFormatType = EAacPlusFormatRaw;
	iFormat = aFormatType;
	return KErrNone;
	}

EXPORT_C TInt CEAacPlusUtil::GetAacFrameInfo(const TUint8* aHdrBuf, TInt& aHdrLen, TEAacPlusFrameInfo& aFrameInfo)
	{
	if (!IsAdts(aHdrBuf, aHdrLen))
		return KErrNotSupported;
	const TInt rateIndex = (aHdrBuf[2] >> 2) & 0xF;
	if (rateIndex > 12)
		return KErrCorrupt;
	const TInt channels = ((aHdrBuf[2] & 1) << 2) | (aHdrBuf[3] >> 6);
	aFrameInfo.iObjectType = (aHdrBuf[2] >> 6) + 1;			// ADTS profile = object type - 1 (2 = AAC-LC)
	aFrameInfo.iSamplingFrequency = KRates[rateIndex];
	aFrameInfo.iOutSamplingFrequency = KRates[rateIndex];
	aFrameInfo.iNoOfChannels = channels ? channels : 2;
	aFrameInfo.iNoOfSamples = 1024;
	// room for implicit SBR (HE-AAC doubles the samples per frame; not signalled in ADTS)
	aFrameInfo.iOutFrameSize = 2048 * aFrameInfo.iNoOfChannels * 2;
	aFrameInfo.iDownSampledMode = 0;
	aFrameInfo.iSbr = 0;
	aFrameInfo.iPs = 0;
	aHdrLen = (aHdrBuf[1] & 1) ? 7 : 9;						// protection_absent = 1 -> no CRC
	return KErrNone;
	}

EXPORT_C TInt CEAacPlusUtil::GetAacFrameInfo(const TUint8* aHdrBuf, TInt& aHdrLen, TEAacPlusFrameInfo& aFrameInfo,
											 TUint8* /*aDataBuf*/, TInt /*aDataBufLen*/)
	{
	return GetAacFrameInfo(aHdrBuf, aHdrLen, aFrameInfo);
	}

EXPORT_C TInt CEAacPlusUtil::GetAacFrameLength(const TUint8* aBuf, TInt aBufLen, TInt& aFrameLen)
	{
	if (!IsAdts(aBuf, aBufLen))
		return KErrNotSupported;
	aFrameLen = ((aBuf[3] & 0x3) << 11) | (aBuf[4] << 3) | (aBuf[5] >> 5);
	return aFrameLen >= 7 ? KErrNone : KErrCorrupt;
	}

EXPORT_C TInt CEAacPlusUtil::Config(TEAacPlusFrameLenConfig& aFrameLenConfig)
	{
	iFormat = aFrameLenConfig.iFormatType;
	return KErrNone;
	}
