#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-1.0
#
# mk_wrappers.sh -- write the "pure Open C" compiler wrappers (pp-gcc, pp-g++) the FFmpeg / OpenH264 recipes compile with.
# They are core's: core/os/tools/phorepoc-x86/cxxrt/mk_pure_wrappers.sh, which expects its tree under /repo and the
# pdk's openc_cflags.py next to it. This script lays that out with symlinks (inside the container) and runs it.
# env: PHOREPOC_CORE, PHOREPOC_PDK (checkouts, mounted in the container), plus env.sh's. Needs the include tree exported
# first (`ph source export -a`, PHOREPOC_BUILD/epoc32). Skip it if PP_WRAPPERS already holds the wrappers.
set -eu
. "$(dirname "$0")/env.sh"
: "${PHOREPOC_CORE:?set PHOREPOC_CORE}"; : "${PHOREPOC_PDK:?set PHOREPOC_PDK}"
if [ -x "$PP_CC" ] && [ -x "$PP_CXX" ]; then echo "wrappers present: $PP_CC"; exit 0; fi
mkdir -p /repo/tools/phorepoc-x86/netsurf
ln -sfn "$PHOREPOC_CORE/os/tools/phorepoc-x86/cxxrt" /repo/tools/phorepoc-x86/cxxrt
ln -sfn "$PHOREPOC_PDK/tools/emitter" /repo/tools/emitter
ln -sf "$PHOREPOC_PDK/tools/openc_cflags.py" /repo/tools/phorepoc-x86/netsurf/openc_cflags.py
W=$PP_WRAPPERS ARCH=$ARCH bash /repo/tools/phorepoc-x86/cxxrt/mk_pure_wrappers.sh
