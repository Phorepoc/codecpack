# codecpack

The Phorepoc codec pack: [FFmpeg](https://ffmpeg.org) (audio and video decoders and demuxers) and
[OpenH264](https://github.com/cisco/openh264) (H.264 encoder), ported to Symbian for
[Phorepoc](https://github.com/Phorepoc).

## Why it is a separate repository

- **Licence.** FFmpeg is LGPL 2.1+. Phorepoc is EPL-1.0, so FFmpeg is built as its own replaceable DLL
  (`pheffmpeg.dll`) that the rest of the system loads at run time, and never ends up inside an EPL binary.
- **Patents.** AAC and H.264 are patent-grey. The pack is published as source and build instructions only, is never in a
  Phorepoc image, and is installed later by the user through the download app.

FFmpeg and OpenH264 are not copied here: the recipes fetch the pinned releases (version and SHA-256 are in the scripts)
at build time. FFmpeg is never configured with `--enable-gpl`, `--enable-version3` or `--enable-nonfree`; the recipe
stops if configure does not report `License: LGPL version 2.1 or later`.

## Layout

| path | what |
|---|---|
| `recipes/ffmpeg_media.sh` | FFmpeg 6.1.2 media set (H.264, HEVC, VP8/9, MPEG-2, AAC, MP3, Opus, Vorbis, FLAC; MP4, Matroska, Ogg ...) as static archives, `ARCH=arm` (NEON) or `x32` |
| `recipes/ffmpeg_media_patch.py` | its source patches: FFT tables cut to 4096 points, 256 KB worker stacks |
| `recipes/aac_codecpack.sh` | the small AAC-only FFmpeg tree and the generated mmp for `ariheaacdecwrapper` |
| `recipes/ffmpeg_small_tx.py`, `ffmpeg_small_stack.py` | the AAC tree's patches (static data, VLC scratch on an 8 KB stack) |
| `recipes/openh264_enc.sh` | OpenH264 2.4.1 encoder archive, ARM |
| `recipes/mk_wrappers.sh` | writes the `pp-gcc` / `pp-g++` compiler wrappers (the pdk's `tools/toolchain/mk_pure_wrappers.sh`) |
| `recipes/make_fft_samples.sh` | makes the test clips for `phefftest` (needs a stock `ffmpeg` with x264/libvpx; not part of the build) |
| `recipes/env.sh` | the environment all recipes share |
| `build.sh` | all recipes in order, for one `ARCH` |
| `os/src/pheffmpeg/` | `pheffmpeg.dll`: the C API over FFmpeg's decoders that `phevidctl` (core) loads |
| `os/src/phefftest/` | an Open C test program: demux and decode clips with libavformat/libavcodec |
| `os/src/codecs/ariheaacdecwrapper/` | the HE-AAC decoder library behind the release's Aricent MMF plug-in, over FFmpeg's AAC decoder |
| `os/src/codecs/eaacplusutil/` | the AAC stream-header utility behind the release stub's frozen exports |
| `registry/components_codecpack.py` | the components, merged into core's registry when this repository is a layer |

## Building

The recipes are plain bash for the Phorepoc toolchain images (see
[toolchain](https://github.com/Phorepoc/toolchain)): `ghcr.io/phorepoc/web` for ARM and x32 (it is `base`, which carries
the Arm GNU toolchain and GCC with the x32 runtime, plus CMake and Python). They need core and the
[pdk](https://github.com/Phorepoc/pdk) checked out, the include tree exported (`ph source export -a`), and
`PHOREPOC_BUILD` set to the same build directory core uses. Configuration is all environment: `PHOREPOC_BUILD`,
`CODECPACK_WORK` (scratch, default `$PHOREPOC_BUILD/codecpack-work`), `ARCH` (`arm`|`x32`), `JOBS`.

```sh
docker run --rm -e ARCH=arm -e JOBS=4 -e PHOREPOC_BUILD=/build \
    -e PHOREPOC_CORE=/src/core -e PHOREPOC_PDK=/src/pdk \
    -v "$PWD/../core/build:/build" -v "$PWD/..:/src" -w /src/codecpack \
    ghcr.io/phorepoc/web ./build.sh
```

Output: `$PHOREPOC_BUILD/codecpack/media-<arch>/{lib,include}`, `openh264-arm/` (ARM), and the AAC tree
`ffmpeg-src/` with `ariheaacdecwrapper.mmp`. `build.sh` first writes the compiler wrappers (skipped when
`PP_WRAPPERS` already holds `bin/pp-gcc`).

Then build the components with this repository as a layer, next to your board's adaptation:

```sh
PHOREPOC_ADAPTATIONS=<board adaptations>:<path to codecpack> ph component build -arch arm pheffmpeg phefftest
PHOREPOC_ADAPTATIONS=<board adaptations>:<path to codecpack> ph component build ariheaacdecwrapper \
    ariheaacdecmmfcodec aacdecoderconfig eaacplusutil eaacplusaudiocontrollerutility \
    aacaudioplaycontrollerplugin aacaudioplaycontrollerrecognizer
```

x32 builds `pheffmpeg_x32` (its libswresample comes from the C-only archive set, which `build.sh` makes too).

## Licence

EPL-1.0 for our files (see `LICENSE`), copyright The Phorepoc Authors (`AUTHORS`). FFmpeg is LGPL-2.1+ and OpenH264 is
BSD-2-Clause; `NOTICE` and `LICENSES/` have the details, including the patent note.

Contributions are welcome under the same terms: add yourself to `AUTHORS`.
