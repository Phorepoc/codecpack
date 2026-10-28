# codecpack

The codec pack brings modern audio and video formats to [Phorepoc](https://github.com/Phorepoc). It's
[FFmpeg](https://ffmpeg.org) and [OpenH264](https://github.com/cisco/openh264) ported to Symbian, so the phone can play
H.264, HEVC, VP8 and VP9 video and AAC, MP3, Opus, Vorbis and FLAC audio in MP4, Matroska and Ogg files. Without it,
games and apps with MP4 video play silently.

You don't build it into an image. Users install it from the Catalogue when they want it.

## Why it lives on its own

There are two reasons. The first is the license. FFmpeg is LGPL, so it's built as its own library (`pheffmpeg.dll`) that
the system loads at run time. It never ends up inside any of Phorepoc's own binaries, and you can swap it for your own
build.

The second is patents. AAC and H.264 are in a grey area, so we only publish the source and the recipe. The pack is
never part of a Phorepoc image, and it's up to each user whether to install it.

We don't keep copies of FFmpeg or OpenH264 here. The recipes download the exact releases they name and check them
against a SHA-256. FFmpeg is never configured with `--enable-gpl`, `--enable-version3` or `--enable-nonfree`, and the
build stops if FFmpeg reports any license other than LGPL 2.1 or later.

## Building

The recipes run in the toolchain's `ghcr.io/phorepoc/web` image. Check out core and the
[pdk](https://github.com/Phorepoc/pdk) next to this repository and run `ph source export -a` once. Then:

```sh
docker run --rm -e ARCH=arm -e JOBS=4 -e PHOREPOC_BUILD=/build \
    -e PHOREPOC_CORE=/src/core -e PHOREPOC_PDK=/src/pdk \
    -v "$PWD/../core/build:/build" -v "$PWD/..:/src" -w /src/codecpack \
    ghcr.io/phorepoc/web ./build.sh
```

Use `ARCH=x32` for the PC. The libraries land in `$PHOREPOC_BUILD/codecpack/`.

Next, build the Symbian components with this repository added to `PHOREPOC_ADAPTATIONS`, after your device's own
repositories. On the PC, `pheffmpeg` also needs [cxxrt](https://github.com/Phorepoc/cxxrt), so build that first and add
it too.

```sh
PHOREPOC_ADAPTATIONS=<device repositories>:<path to codecpack> ph component build -arch arm pheffmpeg phefftest
PHOREPOC_ADAPTATIONS=<device repositories>:<path to codecpack> ph component build ariheaacdecwrapper \
    ariheaacdecmmfcodec aacdecoderconfig eaacplusutil eaacplusaudiocontrollerutility \
    aacaudioplaycontrollerplugin aacaudioplaycontrollerrecognizer
```

## What's in here

- `recipes/` downloads and builds FFmpeg and OpenH264. `build.sh` runs them all in order.
- `os/src/pheffmpeg/` is the library the video player loads, a small C API over FFmpeg's decoders.
- `os/src/phevdecsw/` is the software video decoder plug-in (`phevdecsw.dll`) the video player finds through DevVideo; it
  decodes through `pheffmpeg.dll`. On ARM it only takes MPEG-4 Part 2, since the board's video engine does the rest.
- `os/src/codecs/` has the AAC decoder behind Symbian's own audio framework, now running on FFmpeg.
- `os/src/phefftest/` is a test program that opens and decodes sample clips.
- `registry/components_codecpack.py` lists the components.

## License

Our code is Apache-2.0, and the Symbian files we've changed are EPL-2.0 with GPL-2.0-or-later as a Secondary License.
FFmpeg is LGPL-2.1 or later and OpenH264 is BSD-2-Clause. `NOTICE` and `LICENSES/` have the details, including the
note on patents.

Contributions are welcome under the same terms. Add your name to `AUTHORS` in your first pull request.
