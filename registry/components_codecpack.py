# Copyright The Phorepoc Authors
# SPDX-License-Identifier: Apache-2.0

"""components_codecpack -- the codec pack's components (merged into core's registry when this repository is a layer:
PHOREPOC_ADAPTATIONS=<board adaptations>:<path to codecpack>, see the pdk's tools/emitter/paths.py). Paths are
relative to the merged OS tree; ".codecpack/..." resolves to $PHOREPOC_BUILD/codecpack/..., where build.sh puts
the archives.

AAC and H.264 are patent-grey, so none of this is in an OS image: build the archives and the AAC source tree
(build.sh), then build the components you want and install them through the download app.

  AAC (HE-AAC plug-in chain; the Aricent plug-in and the S60 AAC controller are the 2010 release's own code):
    ph component build ariheaacdecwrapper ariheaacdecmmfcodec aacdecoderconfig eaacplusutil \
        eaacplusaudiocontrollerutility aacaudioplaycontrollerplugin aacaudioplaycontrollerrecognizer
  FFmpeg media set as the replaceable DLL pheffmpeg.dll (LGPL 2.1+), the software video decoder plug-in over it, and the test:
    ph component build -arch arm pheffmpeg phevdecsw phefftest       (x32: pheffmpeg_x32 phevdecsw_x32)
"""
import os

from paths import OS_ROOT, SYMBIAN, BUILD  # noqa: E402,F401
_AAC = "oss.FCL.sf.mw.mmmw/mmfenh/advancedaudiocontroller/audiocontrollerpluginsvariant"

COMPONENTS = {
    # OURS: the decoder library behind the Aricent HE-AAC plug-in = FFmpeg's AAC decoder (LGPL 2.1+, fetched and
    # configured by recipes/aac_codecpack.sh into $PHOREPOC_BUILD/codecpack/, which also writes this mmp)
    "ariheaacdecwrapper": dict(mmp=os.path.join(BUILD, "codecpack", "ariheaacdecwrapper.mmp"), cdriver=True,
                               allow_wsd=True,   # FFmpeg's static tables (~300 KB: sine/cbrt/FFT twiddles, VLCs)
                               c_std="gnu11",
                               # FFmpeg's refstruct needs C11 <stdatomic.h>: GCC's own, after everything of ours
                               flags=["-idirafter", "/usr/lib/gcc/x86_64-linux-gnu/13/include",
                                      # the release's ariheaacdecwrapper.h (the API this library implements)
                                      "-I", os.path.join(SYMBIAN, "symbian-2010/oss.FCL.sf.adapt.codecs.aricent/heaac_dec/ariheaacdecwrapper/export_hdr"),
                                      "-include", os.path.join(OS_ROOT, "src", "codecs", "ariheaacdecwrapper", "src",
                                                               "ffmpeg_board.h")],
                               defs=["HAVE_AV_CONFIG_H", "_ISOC99_SOURCE", "_FILE_OFFSET_BITS=64",
                                     "_LARGEFILE_SOURCE"]),
    # the release: the Aricent HE-AAC MMF plug-in (" EAC" -> " P16"), the S60 AAC controller + its utilities
    "ariheaacdecmmfcodec": dict(mmp="oss.FCL.sf.adapt.codecs.aricent/heaac_dec/ariheaacdecmmfcodec/group/ariheaacdecmmfcodec.mmp"),
    "aacdecoderconfig": dict(mmp="oss.FCL.sf.os.mmaudio/devsoundextensions/mmfcustominterfaces/AacDecoderConfig/AacDecoderConfig/group/AacDecoderConfig.mmp"),
    # OURS: the AAC header utility (the release has only a do-nothing stub) behind that stub's frozen exports
    "eaacplusutil": dict(mmp=os.path.join(OS_ROOT, "src", "codecs", "eaacplusutil", "group", "eaacplusutil.mmp"),
                         rtti=True, **{"def": "oss.FCL.sf.os.mmaudio/eaacplusutil_stub/eabi/eaacplusutil_stubu.def"}),
    "eaacplusaudiocontrollerutility": dict(mmp=_AAC + "/AdvancedAudioControllerUtility/Group/EAACPlusAudioControllerUtility.mmp"),
    "aacaudioplaycontrollerplugin": dict(mmp=_AAC + "/aacaudioplaycontroller/Group/AACAudioPlayControllerPlugin.mmp"),
    "aacaudioplaycontrollerrecognizer": dict(mmp=_AAC + "/aacaudioplaycontroller/Group/AACAudioPlayControllerRecognizer.mmp"),
    # OURS (V4 audio): the LGPL FFmpeg AAC decoder as its OWN replaceable DLL (phevidctl loads it with RLibrary; never static-linked
    # into the controller). Archives: ARCH=arm bash recipes/ffmpeg_media.sh (NEON build: RECONF=1 FFASM=1). PHOREPOC_ADAPTATIONS=<board>:<codecpack> ARM_VFP=1 ph component build -arch arm pheffmpeg
    "pheffmpeg": dict(mmp=os.path.join(OS_ROOT, "src/pheffmpeg/pheffmpeg.mmp"), device=True, cdriver=True, allow_wsd=True, static_ctors=True,
                      rtti=False, defs=["__SOFTFP="], vfp=True, rom="pheffmpeg.dll", device_rom=True,
                      **{"def": os.path.join(OS_ROOT, "src/pheffmpeg/pheffmpeg.def")},
                      arm_archives=[f".codecpack/media-arm/lib/{a}.a" for a in ("libavcodec", "libswresample", "libavutil")]),
    # OURS (2026-10-27): phevdecsw.dll, the software DevVideo decoder (src/phevdecsw, calls pheffmpeg's PheVid* ordinals; own
    # UIDs E70157FC / E70157FD, so it sits beside a board's hardware decoder). ARM: MPEG-4 Part 2 only (the A64 VE decoder does
    # H.264 / MPEG-2); x32: H.264, MPEG-2 and MPEG-4 Part 2. Install = sys/bin/phevdecsw.dll + resource/plugins/phevdecsw.rsc.
    # sis=True: installed, so LOADED FROM RAM -- the loader validates the image (export directory inside the code part;
    # without it RLibrary::GetInfo / ECOM's plug-in check answer KErrCorrupt and DevVideo never sees the decoder)
    "phevdecsw": dict(mmp=os.path.join(OS_ROOT, "src/phevdecsw/phevdecsw.mmp"), allow_wsd=True, sis=True),   # (stage-timing statics)
    "phevdecsw_x32": dict(mmp=os.path.join(OS_ROOT, "src/phevdecsw/phevdecsw_x32.mmp"), allow_wsd=True, sis=True),
    # FFmpeg x32 media archives (ARCH=x32 bash recipes/ffmpeg_media.sh) as the replaceable LGPL DLL
    # (`ph component build pheffmpeg_x32 phevdecsw_x32`).
    "pheffmpeg_x32": dict(mmp=os.path.join(OS_ROOT, "src/pheffmpeg/pheffmpeg.mmp"), cdriver=True, allow_wsd=True, static_ctors=True,
                          rtti=False, rom="pheffmpeg.dll", **{"def": os.path.join(OS_ROOT, "src/pheffmpeg/pheffmpeg.def")},
                          x32_archives=[".codecpack/media-x32/lib/libavcodec.a", ".codecpack/media-x32-c/lib/libswresample.a", ".codecpack/media-x32/lib/libavutil.a"]   # swresample from the C-only build (ARCH=x32 ffmpeg_media.sh without FFASM, copied to media-x32-c): its asm faulted under VirtualBox
                          + [f"thirdparty/x86/cxxrt/{a}.a" for a in ("libphcompat", "libgcc")]),
    # FFmpeg media-set test : libavformat + libavcodec (LGPL archives from recipes/ffmpeg_media.sh)
    #   ARCH=arm bash recipes/ffmpeg_media.sh; bash recipes/make_fft_samples.sh; ARM_VFP=1 ph component build -arch arm phefftest
    "phefftest": dict(mmp=os.path.join(OS_ROOT, "src/phefftest/phefftest.mmp"), test=True, device=True, cdriver=True, allow_wsd=True,
                      static_ctors=True, rtti=False, defs=["__SOFTFP="], vfp=True, stack=0x40000,
                      arm_archives=[f".codecpack/media-arm/lib/{a}.a" for a in ("libavformat", "libavcodec", "libswscale", "libswresample", "libavutil")],
                      rom_data=["resource\\phefftest\\s264.mp4=.codecpack/fft-test/s264.mp4",
                                "resource\\phefftest\\svp8.webm=.codecpack/fft-test/svp8.webm",
                                "resource\\phefftest\\s264p.mp4=.codecpack/fft-test/s264p.mp4"]),
}
