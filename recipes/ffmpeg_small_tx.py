# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later

"""ffmpeg_small_tx -- trims FFmpeg's libavutil/tx_template.c for the AAC codec pack (recipes/aac_codecpack.sh).

FFmpeg keeps split-radix twiddle tables for EVERY power-of-two FFT up to 2^21 as static arrays: ~17 MB of writable
static data in each process that loads the DLL. AAC's largest transform is a 2048-point MDCT (a 1024-point FFT), so
the tables, codelets and codelet-list entries above 4096 points are dropped (and the generic in-place codelet's
max_len capped to match). Idempotent; applied to the extracted copy in $PHOREPOC_BUILD/codecpack/ffmpeg-src only.

    python3 ffmpeg_small_tx.py <ffmpeg-src> [<max-table-points>]

The optional second argument keeps the twiddle TABLES up to that many points (the codelets and the codelet list still stop at
4096). The phone media FFmpeg passes 131072: its x86 SIMD FFT kernels (libavutil/x86/tx_float.asm, the x32 FFASM=1 build)
name ff_tx_tab_8192_float .. ff_tx_tab_131072_float directly, and without them those five data symbols were link stubs
(pdk stub_audit). Cost: ~250 KB of static data.
"""
import re
import sys

MAX = 4096


def keep(line: str, tables: int = MAX) -> bool:
    t = re.search(r"SR_TABLE\((\d+)\)", line)
    if t:
        return int(t.group(1)) <= tables
    m = re.match(r"DECL_SR_CODELET\((\d+),", line) or re.search(r"ff_tx_fft(\d+)_ns_def", line)
    return not (m and int(m.group(1)) > MAX)


def main(src: str, tables: int = MAX) -> None:
    p = f"{src}/libavutil/tx_template.c"
    text = open(p, encoding="utf-8").read()
    lines = text.split("\n")
    out = [l for l in lines if keep(l, tables)]
    new = "\n".join(out).replace(".max_len    = 65536,", f".max_len    = {MAX},")
    if new != text:
        open(p, "w", encoding="utf-8", newline="\n").write(new)
    print(f"ffmpeg_small_tx: {len(lines) - len(out)} lines dropped (FFT codelets > {MAX} points, tables > {tables})")


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else MAX)
