#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later
#
# mk_wrappers.sh -- write the "pure Open C" compiler wrappers (pp-gcc, pp-g++) the FFmpeg / OpenH264 recipes compile
# with: the pdk's tools/toolchain/mk_pure_wrappers.sh. env: PHOREPOC_PDK (checkout, mounted in the container) plus
# env.sh's; the include tree must be exported first (`ph source export -a`, PHOREPOC_BUILD/epoc32). Skipped when
# PP_WRAPPERS already holds the wrappers.
set -eu
. "$(dirname "$0")/env.sh"
: "${PHOREPOC_PDK:?set PHOREPOC_PDK}"
if [ -x "$PP_CC" ] && [ -x "$PP_CXX" ]; then echo "wrappers present: $PP_CC"; exit 0; fi
W=$PP_WRAPPERS ARCH=$ARCH bash "$PHOREPOC_PDK/tools/toolchain/mk_pure_wrappers.sh"
