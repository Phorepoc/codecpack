#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: Apache-2.0
#
# prebuild.sh -- the pdk's package.yml runs it in the toolchain container before the components: the FFmpeg and
# OpenH264 archives (build.sh) for PHOREPOC_ARCH, and for x32 first cxxrt's archives (pheffmpeg_x32 links them).
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
export ARCH=${PHOREPOC_ARCH:-arm} JOBS=${JOBS:-$(nproc)}
if [ "$ARCH" = x32 ]; then
  IFS=: read -r -a repos <<< "${PHOREPOC_ADAPTATIONS:-}"
  IFS=: read -r -a names <<< "${PHOREPOC_ADAPTATION_NAMES:-}"
  cxxrt=
  for i in "${!repos[@]}"; do [ "${names[i]:-}" = cxxrt ] && cxxrt=${repos[i]}; done
  [ -n "$cxxrt" ] || { echo "x32 needs cxxrt in PHOREPOC_ADAPTATIONS"; exit 1; }
  bash "$cxxrt/build.sh"
fi
exec bash "$HERE/build.sh"
