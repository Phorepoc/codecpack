/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: Apache-2.0 */

/* ffmpeg_board.h -- OURS (the optional AAC codec pack): what FFmpeg's C11 sources expect from the C library that
 * P.I.P.S./Open C's headers (C99-era) do not declare. Force-included into the codec pack's files
 * (registry/components_codecpack.py). */
#ifndef FFMPEG_BOARD_H
#define FFMPEG_BOARD_H
#ifndef __cplusplus
/* C11 <stddef.h>: the most-aligned scalar type (libavcodec/refstruct.c); GCC's own definition */
typedef struct {
    long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));
    long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;
#endif
#endif
