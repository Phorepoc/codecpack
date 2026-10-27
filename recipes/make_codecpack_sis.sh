#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: Apache-2.0

# make_codecpack_sis.sh -- the codec pack as one installable SIS: the software video decoder plug-in (phevdecsw.dll + its
# ECOM registration) and the LGPL FFmpeg DLL (pheffmpeg.dll), with the LGPL text and NOTICE. Nothing of it is in an OS image;
# without the pack video falls back cleanly (DevVideo finds no decoder, the player reports the error).
#
#   ARCH=arm|x32 bash recipes/make_codecpack_sis.sh       (inside the toolchain container, after the components are built:
#                                                          ph component build [-arch arm] phevdecsw pheffmpeg / phevdecsw_x32 pheffmpeg_x32,
#                                                          ph component resources phevdecsw[_x32])
# out: $PHOREPOC_BUILD/sis/codecpack-<arch>.sis
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
ARCH=${ARCH:-x32}
if [ "$ARCH" = arm ]; then KEYS="phevdecsw pheffmpeg"; AFL=--arm; else KEYS="phevdecsw_x32 pheffmpeg_x32"; AFL=; fi
python3 "$PHOREPOC_PDK/tools/make_sis.py" $KEYS $AFL --unsigned --name "Phorepoc codec pack" --vendor Phorepoc \
  --out "$PHOREPOC_BUILD/sis/codecpack-$ARCH.sis" \
  --file "$HERE/LICENSES/LGPL-2.1.txt=!:/resource/codecpack/LGPL-2.1.txt" \
  --file "$HERE/NOTICE=!:/resource/codecpack/NOTICE.txt"
