# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-1.0

"""ffmpeg_small_stack -- FFmpeg's VLC builder vs a Symbian thread's stack (the AAC codec pack).

The MMF runs a controller -- and so the decoder -- in a thread with KDefaultStackSize (8 KB) unless its ECom data
says otherwise; the release's AAC controller does not. FFmpeg's ff_vlc_init_sparse/_from_lengths keep a 1500-entry
scratch (12 KB) on the stack -> KERN-EXEC 3 at the stack guard in the AAC decoder's static-table init.
FFmpeg already has a heap path for bigger code sets; this makes it the normal one:
  - LOCALBUF_ELEMS 1500 -> 64 (768 bytes on the stack),
  - the static-table path may take the heap path too (its assert dropped),
  - vlc_common_end frees a heap scratch on both paths (it did only for non-static tables).
Idempotent: run by aac_codecpack.sh on every configure.

    python3 ffmpeg_small_stack.py <ffmpeg source dir>
"""
import os
import sys


def patch(path: str, edits) -> int:
    text = open(path, encoding="utf-8").read()
    n = 0
    for old, new in edits:
        if new in text:
            continue
        if old not in text:
            sys.exit(f"ffmpeg_small_stack: {os.path.basename(path)}: pattern not found: {old[:60]!r}")
        text = text.replace(old, new, 1)
        n += 1
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    return n


def main() -> int:
    vlc = os.path.join(sys.argv[1], "libavcodec", "vlc.c")
    n = patch(vlc, [
        ("#define LOCALBUF_ELEMS 1500 // the maximum currently needed is 1296 by rv34",
         "#define LOCALBUF_ELEMS 64 // Phorepoc: 1500 (12 KB) overflowed an 8 KB MMF controller thread; the rest = heap"),
        ("        av_assert0(nb_codes <= LOCALBUF_ELEMS);\n",
         "        /* Phorepoc: bigger static tables take the heap scratch too */\n"),
        ("    int ret = build_table(vlc, nb_bits, nb_codes, codes, flags);\n\n    if (flags & VLC_INIT_USE_STATIC) {",
         "    int ret = build_table(vlc, nb_bits, nb_codes, codes, flags);\n\n"
         "    if (codes != localbuf)   /* Phorepoc: both paths (a static table may have a heap scratch now) */\n"
         "        av_free(codes);\n"
         "    if (flags & VLC_INIT_USE_STATIC) {"),
        ("    } else {\n        if (codes != localbuf)\n            av_free(codes);\n        if (ret < 0) {",
         "    } else {\n        if (ret < 0) {"),
    ])
    print(f"ffmpeg_small_stack: {n} edits to libavcodec/vlc.c")
    return 0


if __name__ == "__main__":
    sys.exit(main())
