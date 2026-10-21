#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: Apache-2.0
#
# build.sh -- all of the codec pack, start to end, for one ARCH. Run it INSIDE the toolchain image (README.md):
#
#   docker run --rm -e PHOREPOC_BUILD=/build -e ARCH=arm -v <build dir>:/build -v "$PWD:/src" -w /src \
#       ghcr.io/phorepoc/web ./build.sh
#
# env: PHOREPOC_BUILD (required), ARCH (arm|x32, default arm), JOBS, CODECPACK_WORK, PP_WRAPPERS (see recipes/env.sh)
# Steps:  1. the compiler wrappers (skipped when PP_WRAPPERS holds them; needs PHOREPOC_PDK otherwise)
#         2. FFmpeg media archives + headers        -> $PHOREPOC_BUILD/codecpack/media-<arch>/
#         3. ARCH=arm: OpenH264 encoder archive     -> $PHOREPOC_BUILD/codecpack/openh264-arm/
#         4. the AAC source tree + generated mmp    -> $PHOREPOC_BUILD/codecpack/{ffmpeg-src,ariheaacdecwrapper.mmp}
# x32 also builds the C-only set the registry's pheffmpeg_x32 takes libswresample from (media-x32-c).
set -eu
R=$(cd "$(dirname "$0")/recipes" && pwd)
. "$R/env.sh"
if [ ! -x "$PP_CC" ]; then bash "$R/mk_wrappers.sh"; fi
if [ "$ARCH" = arm ]; then
  bash "$R/ffmpeg_media.sh"
  bash "$R/openh264_enc.sh"
else
  FFASM=1 bash "$R/ffmpeg_media.sh"
  FFASM=0 bash "$R/ffmpeg_media.sh"
fi
bash "$R/aac_codecpack.sh"
echo "codec pack built for $ARCH -> $CP"
