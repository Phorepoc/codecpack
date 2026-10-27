#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: Apache-2.0
#
# ffmpeg_media.sh -- the "phone media" FFmpeg 6.1.2 as STATIC ARCHIVES.
# LGPL 2.1+ ONLY (no --enable-gpl/version3/nonfree; the configure licence line is asserted). H.264/HEVC/AAC are patent-grey:
# this is a source + build-instructions item, never part of an OS image (pheffmpeg.dll is installed separately).
#
# Runs INSIDE a toolchain container (README.md), see env.sh for the environment:
#   ARCH=arm bash recipes/ffmpeg_media.sh      the ARMv7 build (needs the arm/web toolchain image + the pp-gcc wrappers)
#   ARCH=x32 bash recipes/ffmpeg_media.sh      the amd64/x32 build
# extra env: FFASM (default 1 on arm = NEON asm, 0 on x32), RECONF=1 (fresh configure + objects)
# Thread/stack/table patches: ffmpeg_media_patch.py (FFT tables <= 4096 points, pthreads on, worker stacks 256 KB).
#
# Out: $PHOREPOC_BUILD/codecpack/media-<arch>/lib/{libavformat,libavcodec,libswscale,libswresample,libavutil}.a + include/
# (linked by the registry's pheffmpeg / phefftest through arm_archives / x32_archives). The source tree and the objects live
# in $CODECPACK_WORK; only the archives + headers are copied out.
set -eu
VER=6.1.2
SHA256=3b624649725ecdc565c903ca6643d41f33bd49239922e45c9b1442c63dca4e38
. "$(dirname "$0")/env.sh"
fetch "https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz" "ffmpeg-$VER.tar.xz" "$SHA256"

# x32 has TWO builds that ship together (pheffmpeg_x32): FFASM=1 -> media-x32 (libavcodec + libavutil with the x86 SIMD kernels) and the default C-only
# one -> media-x32-c (its libswresample: the asm one faulted under VirtualBox). Separate source-build trees too (different configure).
SUF=""; [ "$ARCH" = x32 ] && [ "${FFASM:-0}" != 1 ] && SUF="-c"
OUT=$CP/media-$ARCH$SUF
SRC=$CODECPACK_WORK/ffmpeg-media-src
BLD=$CODECPACK_WORK/ffmpeg-media-$ARCH$SUF
LOG=$OUT/build.log
mkdir -p "$OUT/lib" "$OUT/include"
if [ ! -f "$SRC/configure" ]; then
  rm -rf "$SRC"; mkdir -p "$SRC"
  tar -xf "$DL/ffmpeg-$VER.tar.xz" -C "$SRC" --strip-components=1
fi
python3 "$RECIPES/ffmpeg_media_patch.py" "$SRC"
CC=$PP_CC
[ -x "$CC" ] || { echo "no $CC -- write the pure Open C wrappers first (recipes/mk_wrappers.sh, or set PP_WRAPPERS / PP_CC)"; exit 1; }

DEC="h264 hevc vp8 vp9 mpeg2video mpeg4 aac mp3float opus vorbis flac pcm_s16le"
DEMUX="mov matroska ogg mp3 mpegps mpegts flac wav aac"
PARSE="h264 hevc vp8 vp9 mpegvideo mpeg4video aac aac_latm mpegaudio opus vorbis flac"
BSF="h264_mp4toannexb hevc_mp4toannexb aac_adtstoasc extract_extradata vp9_superframe vp9_superframe_split"
EN=()
for x in $DEC;   do EN+=("--enable-decoder=$x"); done
for x in $DEMUX; do EN+=("--enable-demuxer=$x"); done
for x in $PARSE; do EN+=("--enable-parser=$x"); done
for x in $BSF;   do EN+=("--enable-bsf=$x"); done
EN+=(--enable-protocol=file)

if [ "$ARCH" = arm ]; then
  # pp-gcc forces -march=armv7-a -mthumb -mfpu=vfpv3-d16 softfp (the Symbian EABI boundary) and -includes the Open C prefix
  # header, which a .S file cannot take: NEON assembles through a bare wrapper, same ISA flags but the A53's neon-vfpv4
  # (D0-D31; the kernel saves D16-D31 when CPACR.D32DIS is clear, nkernsmp ncsched.cia). The later -mfpu wins over pp-gcc's.
  AS=$CODECPACK_WORK/bin/pp-as; mkdir -p "$CODECPACK_WORK/bin"
  { echo '#!/bin/sh'; echo 'exec arm-none-eabi-gcc -march=armv7-a -mthumb -mfloat-abi=softfp -mfpu=neon-vfpv4 "$@"'; } > $AS
  chmod +x $AS
  PLAT=(--enable-cross-compile --target-os=linux --arch=arm --cross-prefix=arm-none-eabi- --cc=$CC --as=$AS --disable-runtime-cpudetect
        --extra-ldflags=--specs=nosys.specs --extra-cflags=-DCLOCKS_PER_SEC=1000000 --extra-cflags=-Dmax_align_t=double
        --extra-cflags=-mfpu=neon-vfpv4)
  FFASM=${FFASM:-1}
else
  PLAT=(--enable-cross-compile --target-os=linux --arch=x86_64 --cc=$CC --extra-cflags=-mx32 --extra-cflags=-DCLOCKS_PER_SEC=1000000 --extra-cflags=-Dmax_align_t=double --extra-ldflags=-mx32)
fi
ASM=(--disable-asm --disable-x86asm --disable-inline-asm)
[ "${FFASM:-0}" = 1 ] && ASM=()
[ "$ARCH" = arm ] && [ "${FFASM:-0}" = 1 ] && ASM=(--enable-neon)
# x32 (2026-10-16, VirtualBox MP4 speed): FFASM=1 = the x86 SIMD kernels (nasm -f elfx32; the container has no nasm: it is unpacked once into
# $CODECPACK_WORK/nasm/root/usr/bin/nasm, or set NASM -- `apt-get download nasm; dpkg -x nasm_*.deb $CODECPACK_WORK/nasm/root`). Runtime CPU detection stays on (cpuid; AVX
# needs OSXSAVE, which our kernel never sets, so the guest ends up on SSE2/SSSE3/SSE4). Inline asm stays off.
[ "$ARCH" = x32 ] && [ "${FFASM:-0}" = 1 ] && NASM=${NASM:-$CODECPACK_WORK/nasm/root/usr/bin/nasm}
[ "$ARCH" = x32 ] && [ "${FFASM:-0}" = 1 ] && ASM=(--enable-x86asm --x86asmexe=$NASM --disable-inline-asm)

[ "${RECONF:-0}" = 1 ] && rm -rf "$BLD"
mkdir -p "$BLD"; cd "$BLD"
[ -f config.h ] || "$SRC/configure" "${PLAT[@]}" --disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect \
  --disable-debug --disable-iconv --disable-zlib --disable-bzlib --disable-lzma --disable-avdevice --disable-avfilter \
  --disable-postproc --disable-hwaccels --disable-pthreads --disable-w32threads --disable-os2threads --disable-stripping \
  --enable-static --disable-shared "${ASM[@]}" "${EN[@]}" > "$OUT/configure.log" 2>&1 \
  || { tail -30 "$OUT/configure.log"; exit 1; }
# the LGPL assertion
grep -qx 'License: LGPL version 2.1 or later' "$OUT/configure.log" || { echo "configure licence line is not LGPL 2.1+:"; grep -i '^License' "$OUT/configure.log"; exit 1; }
echo "licence: $(grep '^License' "$OUT/configure.log")"
# configure probed the toolchain's newlib; the board's C library is P.I.P.S./Open C: what it lacks is switched off
# (FFmpeg then uses its own fallbacks: libavutil/libm.h, mem.c's plain malloc)
for h in GETAUXVAL MMAP MEMALIGN POSIX_MEMALIGN SYSCONF ISATTY STRERROR_R LOCALTIME_R GMTIME_R CLOCK_GETTIME \
         NANOSLEEP USLEEP ARC4RANDOM GETRANDOM PRCTL MALLOC_H LOG2F LOG2 FORK DLOPEN SYSCTL GETRUSAGE SETRLIMIT \
         PTHREAD_CANCEL; do
  sed -i "s/^#define HAVE_$h 1$/#define HAVE_$h 0/" config.h
done
# NEON .S: movrel would be movw/movt (R_ARM_THM_MOVW_ABS_NC, which the E32 emitter does not relocate) -> the `ldr rd,=sym`
# literal-pool form (a plain ABS32 word, like -mword-relocations in pp-gcc)
sed -i "s/^#define HAVE_ARMV6T2_EXTERNAL 1$/#define HAVE_ARMV6T2_EXTERNAL 0/" config.h
# pthreads: configure cannot link-test Open C's libpthread from newlib, so it is configured off and switched on here; the
# link is against libpthread.lib (src/phefftest.mmp). SETNAME_NP etc. stay 0 (forced above).
sed -i "s/^#define HAVE_PTHREADS 0$/#define HAVE_PTHREADS 1/; s/^#define HAVE_THREADS 0$/#define HAVE_THREADS 1/" config.h
sed -i "s/^!HAVE_PTHREADS=yes$/HAVE_PTHREADS=yes/; s/^!HAVE_THREADS=yes$/HAVE_THREADS=yes/" ffbuild/config.mak
grep -E '^#define (HAVE_NEON|HAVE_THREADS|HAVE_PTHREADS|CONFIG_THUMB|HAVE_ARMV6T2_EXTERNAL|HAVE_ARMV7A|HAVE_VFPV3) ' config.h || true
if [ "$ARCH" = arm ] && [ "$FFASM" = 1 ]; then grep -qx '#define CONFIG_THUMB 1' config.h || { echo "CONFIG_THUMB is 0 but pp-gcc is -mthumb"; exit 1; }; fi
if [ "$ARCH" = x32 ] && [ "${FFASM:-0}" = 1 ]; then
  # configure picks `-f elf64`; the x32 ABI needs elfx32 (x86inc.asm has the ILP32 paths). AND -DPIC: without it nasm addresses constants with an
  # absolute disp32 (R_X86_64_32S), which the CPU SIGN-extends in 64-bit mode -- our ROM code/rodata lives above 0x80000000, so every such load
  # (`movq 0x3be100,%xmm5` = ff_pw_1) read 0xFFFFFFFF82D9xxxx and faulted. PIC makes x86inc use `default rel` (%rip-relative), which the
  # emitter + prelinker already handle. Objects assembled with other flags are dropped.
  if ! grep -q '^X86ASMFLAGS=-f elfx32 -DPIC' ffbuild/config.mak; then
    sed -i 's/^X86ASMFLAGS=.*/X86ASMFLAGS=-f elfx32 -DPIC/' ffbuild/config.mak
    for o in $(find libavutil libavcodec libswscale libswresample -name '*.o'); do [ -f "$SRC/${o%.o}.asm" ] && rm -f "$o"; done
  fi
fi
if [ "$ARCH" = x32 ] && [ "${FFASM:-0}" = 1 ]; then
  # FFmpeg's asm treats x32 as x64 ("Treat x32 as x64 for now", configure): it hard-codes 8-byte pointers where C has pointer ARRAYS --
  # ff_h264_idct_add8_*(uint8_t **dest, ...) steps `add r, gprsize` through dest[]; with x32's 4-byte pointers it reads garbage and faults
  # (the "first decode crash"; NOT an emitter bug). Keep the C version of that one function (every other kernel takes plain pointers / ints).
  # Audio is handled in pheffmpeg.c (SIMD switched off while the AAC decoder + swresample initialise).
  if grep -q 'c->h264_idct_add8 = ff_' "$SRC/libavcodec/x86/h264dsp_init.c"; then
    sed -i 's/c->h264_idct_add8 = ff_[A-Za-z0-9_]*;/(void)0; \/* PHOREPOC x32: asm idct_add8 walks uint8_t **dest with 8-byte pointers *\//' "$SRC/libavcodec/x86/h264dsp_init.c"
    rm -f libavcodec/x86/h264dsp_init.o
  fi
fi
make -k -j"$JOBS" libavutil/libavutil.a libavcodec/libavcodec.a libavformat/libavformat.a libswscale/libswscale.a \
  libswresample/libswresample.a > "$LOG" 2>&1 || { grep -m10 -B2 -A3 'rror' "$LOG"; echo "make failed, see $LOG"; exit 1; }
for l in libavformat libavcodec libswscale libswresample libavutil; do cp "$l/$l.a" "$OUT/lib/"; done
# headers for the consumers (source tree + the generated config)
rm -rf "$OUT/include"; mkdir -p "$OUT/include"
for l in libavformat libavcodec libswscale libswresample libavutil; do
  mkdir -p "$OUT/include/$l"; cp "$SRC/$l"/*.h "$OUT/include/$l/"
done
cp config.h config_components.h "$OUT/include/" 2>/dev/null || true
cp libavutil/avconfig.h libavutil/ffversion.h "$OUT/include/libavutil/" 2>/dev/null || true
ls -l "$OUT/lib"
echo "FFmpeg $VER media set built for $ARCH -> $OUT"
