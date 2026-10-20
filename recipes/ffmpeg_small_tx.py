# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-1.0

"""ffmpeg_small_tx -- trims FFmpeg's libavutil/tx_template.c for the AAC codec pack (recipes/aac_codecpack.sh).

FFmpeg keeps split-radix twiddle tables for EVERY power-of-two FFT up to 2^21 as static arrays: ~17 MB of writable
static data in each process that loads the DLL. AAC's largest transform is a 2048-point MDCT (a 1024-point FFT), so
the tables, codelets and codelet-list entries above 4096 points are dropped (and the generic in-place codelet's
max_len capped to match). Idempotent; applied to the extracted copy in $PHOREPOC_BUILD/codecpack/ffmpeg-src only.

    python3 ffmpeg_small_tx.py <ffmpeg-src>
"""
import re
import sys

MAX = 4096


def keep(line: str) -> bool:
    m = (re.search(r"SR_TABLE\((\d+)\)", line) or re.match(r"DECL_SR_CODELET\((\d+),", line)
         or re.search(r"ff_tx_fft(\d+)_ns_def", line))
    return not (m and int(m.group(1)) > MAX)


def main(src: str) -> None:
    p = f"{src}/libavutil/tx_template.c"
    text = open(p, encoding="utf-8").read()
    lines = text.split("\n")
    out = [l for l in lines if keep(l)]
    new = "\n".join(out).replace(".max_len    = 65536,", f".max_len    = {MAX},")
    if new != text:
        open(p, "w", encoding="utf-8", newline="\n").write(new)
    print(f"ffmpeg_small_tx: {len(lines) - len(out)} lines dropped (FFT tables/codelets > {MAX} points)")


if __name__ == "__main__":
    main(sys.argv[1])
