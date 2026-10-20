#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-1.0
#
# env.sh -- sourced by every recipe. Plain bash, meant to run INSIDE a Phorepoc toolchain container (see README.md);
# nothing here calls docker. Configuration, all by environment:
#
#   PHOREPOC_BUILD   (required) the build root shared with the pdk/core: archives + headers go to $PHOREPOC_BUILD/codecpack/
#   CODECPACK_WORK   scratch for sources and objects            (default $PHOREPOC_BUILD/codecpack-work)
#   ARCH             arm | x32                                  (default arm)
#   JOBS             parallel compile jobs                      (default 4)
#   PP_WRAPPERS      directory holding bin/pp-gcc and bin/pp-g++, the "pure Open C" compiler wrappers that the pdk's
#                    tools/toolchain/mk_pure_wrappers.sh writes (default $CODECPACK_WORK/lsc)
#   PP_CC / PP_CXX   override the two wrapper paths individually
: "${PHOREPOC_BUILD:?set PHOREPOC_BUILD (the output root; archives go to \$PHOREPOC_BUILD/codecpack/)}"
ARCH=${ARCH:-arm}
JOBS=${JOBS:-${CPUS:-4}}
CODECPACK_WORK=${CODECPACK_WORK:-$PHOREPOC_BUILD/codecpack-work}
CP=$PHOREPOC_BUILD/codecpack          # the output tree the registry points at (".codecpack/..." in pdk paths.resolve)
DL=$CODECPACK_WORK/dl                 # downloaded tarballs
PP_WRAPPERS=${PP_WRAPPERS:-$CODECPACK_WORK/lsc}
PP_CC=${PP_CC:-$PP_WRAPPERS/bin/pp-gcc}
PP_CXX=${PP_CXX:-$PP_WRAPPERS/bin/pp-g++}
RECIPES=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
case "$ARCH" in arm|x32) ;; *) echo "ARCH must be arm or x32 (got '$ARCH')"; exit 1;; esac
mkdir -p "$CP" "$CODECPACK_WORK" "$DL"

# fetch URL FILE SHA256 -- download into $DL once, verify always
fetch() {
  local url=$1 file=$2 sha=$3
  if [ ! -f "$DL/$file" ]; then
    # curl when the image has it, else Python (every toolchain image has python3)
    if command -v curl >/dev/null; then curl -sSfL -o "$DL/$file.part" "$url"
    else python3 -c 'import sys, urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])' "$url" "$DL/$file.part"; fi
    mv "$DL/$file.part" "$DL/$file"
  fi
  echo "$sha *$DL/$file" | sha256sum -c - >/dev/null || { echo "$file: checksum mismatch"; exit 1; }
}
