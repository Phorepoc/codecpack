#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: Apache-2.0
#
# sis.sh -- the pdk's package.yml runs it after the components: the codec pack SIS for PHOREPOC_ARCH
# ($PHOREPOC_BUILD/sis/codecpack-<arch>.sis).
set -eu
ARCH=${PHOREPOC_ARCH:-arm} exec bash "$(dirname "$0")/make_codecpack_sis.sh"
