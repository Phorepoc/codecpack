# Copyright The Phorepoc Authors
# SPDX-License-Identifier: EPL-1.0

"""ffmpeg_media_patch -- source patches for the phone media FFmpeg (recipes/ffmpeg_media.sh). Idempotent.

  1. libavutil/tx_template.c: FFT twiddle tables above 4096 points dropped (ffmpeg_small_tx.py, the AAC pack's cut):
     they are ~17 MB of static (bss) data per process otherwise. 4096 covers every transform of the enabled codecs
     (AAC 1024, Vorbis <= 2048, Opus/CELT <= 960, MP3 has its own MDCT; H.264/VP8/VP9/HEVC use no FFT).
  2. libavutil/thread.h + libavcodec/pthread_frame.c + libavutil/slicethread.c: worker threads get an explicit
     256 KB stack (Open C's default thread stack is small).

    python3 ffmpeg_media_patch.py <ffmpeg-src>
"""
import os
import subprocess
import sys

STACK = 256 * 1024

HELPER = """#include <pthread.h>
/* PHOREPOC: worker threads get a 256 KB stack (Open C's default is small) */
static inline int ph_thread_create(pthread_t *t, const pthread_attr_t *attr_unused, void *(*fn)(void *), void *arg)
{
    pthread_attr_t a;
    int r;
    (void)attr_unused;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, %d);
    r = pthread_create(t, &a, fn, arg);
    pthread_attr_destroy(&a);
    return r;
}
""" % STACK


def put(path: str, text: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def patch_threads(src: str) -> None:
    p = f"{src}/libavutil/thread.h"
    t = open(p, encoding="utf-8").read()
    if "ph_thread_create" not in t:
        assert t.count("#include <pthread.h>\n") == 1
        put(p, t.replace("#include <pthread.h>\n", HELPER))
        print("ffmpeg_media_patch: libavutil/thread.h: ph_thread_create (256 KB stack)")
    for f in ("libavcodec/pthread_frame.c", "libavutil/slicethread.c"):
        p = f"{src}/{f}"
        t = open(p, encoding="utf-8").read()
        if "pthread_create(" in t:
            put(p, t.replace("pthread_create(", "ph_thread_create("))
            print(f"ffmpeg_media_patch: {f}: pthread_create -> ph_thread_create")


def patch_offsets(src: str) -> None:
    # the Open C prefix header's offsetof is pointer arithmetic (not a constant expression): the compile-time check of the
    # offsets hard-coded in the ARM .S files needs the builtin
    p = f"{src}/libavutil/internal.h"
    t = open(p, encoding="utf-8").read()
    old = "int x_##o[offsetof(s, m) == o? 1: -1];"
    if old in t:
        put(p, t.replace(old, "int x_##o[__builtin_offsetof(s, m) == o? 1: -1];"))
        print("ffmpeg_media_patch: libavutil/internal.h: AV_CHECK_OFFSET uses __builtin_offsetof")


def patch_mem(src: str) -> None:
    # Open C's malloc is 8-byte aligned and the memalign family is switched off (ffmpeg_media.sh), but the NEON .S code uses
    # 128-bit aligned vld1/vst1 (:128) on av_malloc'd buffers -> alignment fault. av_malloc/av_realloc/av_free get a 16-byte
    # aligned block with a header (original pointer, size) just below it.
    p = f"{src}/libavutil/mem.c"
    t = open(p, encoding="utf-8").read()
    if "ph_amalloc" in t:
        return
    helpers = """
/* PHOREPOC: 16-byte aligned malloc over an 8-byte aligned libc malloc; header = {orig ptr, size} just below the block */
static void *ph_amalloc(size_t size)
{
    char *o = malloc(size + 32), *a;
    if (!o) return NULL;
    a = (char *)(((uintptr_t)o + 32) & ~(uintptr_t)15);
    ((void **)a)[-1] = o;
    ((size_t *)a)[-2] = size;
    return a;
}
static void ph_afree(void *p)
{
    if (p) free(((void **)p)[-1]);
}
static void *ph_arealloc(void *p, size_t size)
{
    void *n;
    size_t old;
    if (!p) return ph_amalloc(size);
    old = ((size_t *)p)[-2];
    if (size <= old) { ((size_t *)p)[-2] = size; return p; }
    n = ph_amalloc(size);
    if (!n) return NULL;
    memcpy(n, p, old);
    ph_afree(p);
    return n;
}

void *av_malloc(size_t size)
"""
    NL = chr(10)
    for old, new in (
        (NL + "void *av_malloc(size_t size)" + NL, helpers),
        ("    ptr = malloc(size);" + NL + "#endif", "    ptr = ph_amalloc(size);" + NL + "#endif"),
        ("    ret = realloc(ptr, size + !size);", "    ret = ph_arealloc(ptr, size + !size);"),
        ("    free(ptr);" + NL + "#endif" + NL + "}", "    ph_afree(ptr);" + NL + "#endif" + NL + "}"),
    ):
        assert t.count(old) == 1, old
        t = t.replace(old, new, 1)
    put(p, t)
    print("ffmpeg_media_patch: libavutil/mem.c: 16-byte aligned av_malloc (ph_amalloc)")


def main(src: str) -> None:
    here = os.path.dirname(os.path.abspath(__file__))
    subprocess.check_call([sys.executable, os.path.join(here, "ffmpeg_small_tx.py"), src])
    patch_threads(src)
    patch_offsets(src)
    patch_mem(src)


if __name__ == "__main__":
    main(sys.argv[1])
