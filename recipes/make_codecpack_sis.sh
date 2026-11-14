#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later

# make_codecpack_sis.sh -- the codec pack as one installable SIS: the software video decoder plug-in (phevdecsw.dll + its
# ECOM registration) and the LGPL FFmpeg DLL (pheffmpeg.dll), with the LGPL and EPL-2.0 texts, NOTICE and the source offer. Nothing of it is in an OS image;
# without the pack video falls back cleanly (DevVideo finds no decoder, the player reports the error).
#
#   ARCH=arm|x32 bash recipes/make_codecpack_sis.sh       (inside the toolchain container, after the components are built:
#                                                          phe component build [-arch arm] phevdecsw pheffmpeg / phevdecsw_x32 pheffmpeg_x32,
#                                                          phe component resources phevdecsw[_x32])
# out: $PHOREPOC_BUILD/sis/codecpack-<arch>.sis
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
ARCH=${ARCH:-x32}
if [ "$ARCH" = arm ]; then KEYS="phevdecsw pheffmpeg"; AFL=--arm; else KEYS="phevdecsw_x32 pheffmpeg_x32"; AFL=; fi
# The licences of what the SIS carries -- FFmpeg (LGPL-2.1-or-later) and our own code (EPL-2.0 OR GPL-2.0-or-later); no Nokia EPL-1.0 or
# OpenH264 code is in it -- the NOTICE and the source offer (FFmpeg is LGPL: its source offer travels with the binary).
LIC=
for f in "$HERE/LICENSES/LGPL-2.1.txt" "$HERE/LICENSES/EPL-2.0.txt" "$HERE/NOTICE" "$HERE/SOURCE-OFFER.txt"; do
  b=$(basename "$f"); case $b in *.txt) ;; *) b=$b.txt;; esac
  LIC="$LIC --file $f=!:/resource/codecpack/$b"
done
python3 "$PHOREPOC_PDK/tools/make_sis.py" $KEYS $AFL --unsigned --version "${PHOREPOC_PACKAGE_VERSION:-1.0.0}" \
  --name "Phorepoc codec pack" --vendor Phorepoc $LIC \
  --out "$PHOREPOC_BUILD/sis/codecpack-$ARCH.sis"
