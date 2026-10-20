#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-1.0
#
# openh264_enc.sh -- Cisco OpenH264 2.4.1 ENCODER (BSD-2-Clause) as a static archive for ARM. Decoder not built.
# FFmpeg here has NO H.264 encoder (decoders only, LGPL; the only FFmpeg H.264 encoder is libx264 = GPL: refused).
# Note: Cisco's patent licence for H.264 covers only the binaries Cisco itself distributes; building from source, as
# this recipe does, leaves the patent question with whoever builds and uses it.
#
# Runs INSIDE a toolchain container (README.md; env.sh lists the environment). ARM only.
#   bash recipes/openh264_enc.sh          extra env: NEON (default 1: the .S NEON kernels), RECONF=1
# Out: $PHOREPOC_BUILD/codecpack/openh264-arm/lib/libopenh264enc.a + include/wels/*.h
set -eu
VER=2.4.1
SHA256=8ffbe944e74043d0d3fb53d4a2a14c94de71f58dbea6a06d0dc92369542958ea
. "$(dirname "$0")/env.sh"
[ "$ARCH" = arm ] || { echo "openh264_enc.sh builds for ARCH=arm only"; exit 1; }
fetch "https://github.com/cisco/openh264/archive/refs/tags/v$VER.tar.gz" "openh264-$VER.tar.gz" "$SHA256"
[ -d "$CODECPACK_WORK/openh264-$VER" ] || tar -xzf "$DL/openh264-$VER.tar.gz" -C "$CODECPACK_WORK" --exclude='*/res/*'
NEON=${NEON:-1}
S=$CODECPACK_WORK/openh264-$VER; OUT=$CP/openh264-arm; B=$CODECPACK_WORK/oh264-arm
CXX=$PP_CXX
[ -x "$CXX" ] || { echo "no $CXX -- write the pure Open C wrappers first (recipes/mk_wrappers.sh, or set PP_WRAPPERS / PP_CXX)"; exit 1; }
[ "${RECONF:-0}" = 1 ] && rm -rf "$B"
mkdir -p "$B/obj" "$OUT/lib" "$OUT/include"
# pthread-only semaphores (Open C has no sem_*), fixed CPU count, no SCHED_FIFO attrs
cat > $B/phe_sem.h <<'EOT'
#ifndef PHE_SEM_H
#define PHE_SEM_H
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { pthread_mutex_t m; pthread_cond_t c; int n; } phe_sem_t;
static inline int phe_sem_init(phe_sem_t* s, int p, unsigned v) { (void)p; s->n = (int)v; pthread_mutex_init(&s->m, 0); return pthread_cond_init(&s->c, 0); }
static inline int phe_sem_destroy(phe_sem_t* s) { pthread_cond_destroy(&s->c); return pthread_mutex_destroy(&s->m); }
static inline int phe_sem_post(phe_sem_t* s) { pthread_mutex_lock(&s->m); ++s->n; pthread_cond_signal(&s->c); pthread_mutex_unlock(&s->m); return 0; }
static inline int phe_sem_wait(phe_sem_t* s) { pthread_mutex_lock(&s->m); while (s->n <= 0) pthread_cond_wait(&s->c, &s->m); --s->n; pthread_mutex_unlock(&s->m); return 0; }
static inline int phe_sem_trywait(phe_sem_t* s) { int r = -1; pthread_mutex_lock(&s->m); if (s->n > 0) { --s->n; r = 0; } pthread_mutex_unlock(&s->m); if (r) errno = EAGAIN; return r; }
static inline int phe_sem_timedwait(phe_sem_t* s, const struct timespec* t) { int r = 0; pthread_mutex_lock(&s->m); while (s->n <= 0 && !r) r = pthread_cond_timedwait(&s->c, &s->m, t); if (!r) --s->n; pthread_mutex_unlock(&s->m); if (r) errno = ETIMEDOUT; return r ? -1 : 0; }
#ifdef __cplusplus
}
#endif
#define sem_t phe_sem_t
#define sem_init phe_sem_init
#define sem_destroy phe_sem_destroy
#define sem_post phe_sem_post
#define sem_wait phe_sem_wait
#define sem_trywait phe_sem_trywait
#define sem_timedwait phe_sem_timedwait
#endif
EOT
python3 - "$S" "$B" <<'EOT'
import sys,re,os
s,b=sys.argv[1:3]
p=s+'/codec/common/src/WelsThreadLib.cpp'
t=open(p,newline='').read().replace('\r\n','\n')
t=t.replace('#if !defined(__ANDROID__) && !defined(__Fuchsia__)\n  err = pthread_attr_setscope','#if 0\n  err = pthread_attr_setscope')
t=t.replace('#else\n\n  size_t len = sizeof (pInfo->ProcessorCount);','#elif defined(PHE_CPUS)\n  pInfo->ProcessorCount = PHE_CPUS;\n  return WELS_THREAD_ERROR_OK;\n#else\n\n  size_t len = sizeof (pInfo->ProcessorCount);')
t=t.replace('  err = pthread_create (thread, &at, routine, arg);','  pthread_attr_setstacksize (&at, 512 * 1024); /* Open C default stack is small */\n  err = pthread_create (thread, &at, routine, arg);')
os.makedirs(b+'/patched',exist_ok=True)
open(b+'/patched/WelsThreadLib.cpp','w',newline='\n').write(t)
EOT
INC="-I$S/codec/api/wels -I$S/codec/common/inc -I$S/codec/encoder/core/inc -I$S/codec/encoder/plus/inc -I$S/codec/processing/interface
 -I$S/codec/processing/src/common -I$S/codec/processing/src/adaptivequantization -I$S/codec/processing/src/downsample
 -I$S/codec/processing/src/scrolldetection -I$S/codec/processing/src/vaacalc"
CXXF="-O3 -DNDEBUG -DPHE_CPUS=4 -fno-exceptions -fno-rtti -std=gnu++14 -include $B/phe_sem.h -w"
ASF="-O2 -marm -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=softfp"
[ "$NEON" = 1 ] && CXXF="$CXXF -DHAVE_NEON -mfpu=neon-vfpv4"
{
  for d in common/src encoder/core/src encoder/plus/src; do ls $S/codec/${d%%/*}/${d#*/}/*.cpp 2>/dev/null; done
  for d in adaptivequantization backgrounddetection common complexityanalysis denoise downsample imagerotate scenechangedetection scrolldetection vaacalc; do
    ls $S/codec/processing/src/$d/*.cpp; done
} | grep -v -E "WelsThreadLib.cpp|DllEntry" > $B/cpp.txt
echo "$S/codec/common/src/WelsThreadLib.cpp" >> $B/cpp.txt
: > $B/asm.txt
if [ "$NEON" = 1 ]; then ls $S/codec/common/arm/*.S $S/codec/encoder/core/arm/*.S $S/codec/processing/src/arm/*.S > $B/asm.txt; fi
build_one() {
  f=$1; o=$B/obj/$(echo "${f#$S/}" | tr '/' '_').o
  [ -f "$o" ] && [ "$o" -nt "$f" ] && return 0
  case $f in
    *.cpp) src=$f; [ "${f##*/}" = WelsThreadLib.cpp ] && src=$B/patched/WelsThreadLib.cpp
           $CXX -c $CXXF $INC -I$S/codec/common/inc "$src" -o "$o" 2> "$o.err" || { echo "FAIL $f"; head -12 "$o.err"; return 1; } ;;
    *.S)   arm-none-eabi-gcc -c $ASF -DHAVE_NEON -I$S/codec/common/arm -I$S/codec/common/inc "$f" -o "$o" 2> "$o.err" || { echo "FAIL $f"; head -12 "$o.err"; return 1; } ;;
  esac
}
export -f build_one; export S B CXX CXXF INC ASF
cat $B/cpp.txt $B/asm.txt | xargs -P "$JOBS" -I{} bash -c 'build_one {}' || { echo "openh264: compile failures above"; exit 1; }
rm -f $OUT/lib/libopenh264enc.a
arm-none-eabi-ar rcs $OUT/lib/libopenh264enc.a $B/obj/*.o
mkdir -p $OUT/include/wels; cp $S/codec/api/wels/*.h $OUT/include/wels/
ls -l $OUT/lib; echo "OpenH264 $VER encoder built (NEON=$NEON) -> $OUT"
