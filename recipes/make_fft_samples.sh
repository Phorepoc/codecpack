#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later
#
# make_fft_samples.sh -- the phefftest samples into $PHOREPOC_BUILD/codecpack/fft-test/:
# 2 s of ffmpeg's testsrc/sine, 320x240 25 fps: s264.mp4 (H.264 + AAC-LC) and svp8.webm (VP8 + Vorbis);
# s264p.mp4 = 2 s 360x640 H.264 Main (the phone-screen size) for the multi-thread speed test.
# Needs a stock `ffmpeg` on PATH built with libx264, libvpx and libvorbis (e.g. `apk add ffmpeg` in alpine:3.20, or your
# distribution's package). The encoders are GPL/external, so they are only ever used here, to MAKE test files: never in
# our build, and the samples are not part of this repository.
set -eu
. "$(dirname "$0")/env.sh"
O=$CP/fft-test
mkdir -p "$O"; cd "$O"
command -v ffmpeg >/dev/null || { echo "no ffmpeg on PATH (see the header of this script)"; exit 1; }
ffmpeg -y -loglevel error -f lavfi -i testsrc=size=320x240:rate=25:duration=2 -f lavfi -i sine=frequency=440:duration=2:sample_rate=44100 \
  -c:v libx264 -profile:v baseline -pix_fmt yuv420p -g 25 -c:a aac -b:a 64k -shortest -movflags +faststart s264.mp4
ffmpeg -y -loglevel error -f lavfi -i testsrc=size=320x240:rate=25:duration=2 -f lavfi -i sine=frequency=440:duration=2:sample_rate=44100 \
  -c:v libvpx -b:v 300k -pix_fmt yuv420p -c:a libvorbis -b:a 64k -shortest svp8.webm
ffmpeg -y -loglevel error -f lavfi -i testsrc2=size=360x640:rate=25:duration=2 -c:v libx264 -profile:v main -pix_fmt yuv420p -g 25 \
  -movflags +faststart s264p.mp4
ls -l
