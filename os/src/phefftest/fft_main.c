/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: Apache-2.0 */

/* fft_main.c -- OURS: open an in-ROM sample with libavformat over a custom AVIOContext,
   decode every video + audio packet in software with libavcodec, print "PH: fft" lines (streams, codecs, frame counts, a
   checksum of the first decoded video frame, decode ms). Samples: recipes/make_fft_samples.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/cpu.h>

extern void ph_puts(const char* s);
extern long long ph_us(void);

static void plog(const char* fmt, ...)
{
    char b[220];
    int n;
    va_list ap;
    memcpy(b, "PH: fft ", 8);
    va_start(ap, fmt);
    n = vsnprintf(b + 8, sizeof b - 8, fmt, ap);
    va_end(ap);
    (void)n;
    ph_puts(b);
}

static void av_cb(void* p, int level, const char* fmt, va_list vl)
{
    char b[200];
    (void)p;
    if (level > AV_LOG_WARNING) return;
    memcpy(b, "PH: fft log ", 12);
    vsnprintf(b + 12, sizeof b - 12, fmt, vl);
    ph_puts(b);
}

static int rd(void* o, uint8_t* buf, int n)
{
    size_t r = fread(buf, 1, (size_t)n, (FILE*)o);
    return r ? (int)r : AVERROR_EOF;
}

static int64_t sk(void* o, int64_t off, int whence)
{
    FILE* f = (FILE*)o;
    if (whence == AVSEEK_SIZE) {
        long cur = ftell(f), end;
        fseek(f, 0, SEEK_END);
        end = ftell(f);
        fseek(f, cur, SEEK_SET);
        return end;
    }
    if (fseek(f, (long)off, whence & 3) != 0) return -1;
    return ftell(f);
}

static uint32_t fnv(uint32_t h, const uint8_t* p, int n)
{
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

static int run(const char* path)
{
    FILE* f = fopen(path, "rb");
    AVFormatContext* fc;
    AVIOContext* io;
    AVCodecContext* cx[8];
    const AVCodec* cd;
    AVPacket* pk;
    AVFrame* fr;
    unsigned i;
    long vf = 0, af = 0, vp = 0, ap = 0;
    long long t0, t_all;
    uint32_t sum = 0;
    int have_sum = 0, rc, vi = -1, ai = -1;
    long samples = 0;

    if (!f) { plog("%s: fopen failed", path); return 1; }
    memset(cx, 0, sizeof cx);
    fc = avformat_alloc_context();
    io = avio_alloc_context((uint8_t*)av_malloc(32768), 32768, 0, f, rd, NULL, sk);
    fc->pb = io;
    t0 = ph_us();
    rc = avformat_open_input(&fc, path, NULL, NULL);
    if (rc < 0) { plog("%s: open_input %d", path, rc); return 2; }
    rc = avformat_find_stream_info(fc, NULL);
    if (rc < 0) { plog("%s: find_stream_info %d", path, rc); return 3; }
    plog("%s: format %s, %u streams, probe %lld ms", path, fc->iformat->name, fc->nb_streams, (ph_us() - t0) / 1000);
    for (i = 0; i < fc->nb_streams && i < 8; i++) {
        AVCodecParameters* cp = fc->streams[i]->codecpar;
        cd = avcodec_find_decoder(cp->codec_id);
        if (cp->codec_type == AVMEDIA_TYPE_VIDEO)
            plog("stream %u: video %s %dx%d %s", i, cd ? cd->name : "?", cp->width, cp->height, av_get_pix_fmt_name((enum AVPixelFormat)cp->format));
        else if (cp->codec_type == AVMEDIA_TYPE_AUDIO)
            plog("stream %u: audio %s %d Hz %d ch", i, cd ? cd->name : "?", cp->sample_rate, cp->ch_layout.nb_channels);
        else { plog("stream %u: other type %d", i, (int)cp->codec_type); continue; }
        if (!cd) continue;
        cx[i] = avcodec_alloc_context3(cd);
        avcodec_parameters_to_context(cx[i], cp);
        rc = avcodec_open2(cx[i], cd, NULL);
        if (rc < 0) { plog("stream %u: avcodec_open2 %d", i, rc); avcodec_free_context(&cx[i]); continue; }
        if (cp->codec_type == AVMEDIA_TYPE_VIDEO && vi < 0) vi = (int)i;
        if (cp->codec_type == AVMEDIA_TYPE_AUDIO && ai < 0) ai = (int)i;
    }
    pk = av_packet_alloc();
    fr = av_frame_alloc();
    t_all = ph_us();
    while (av_read_frame(fc, pk) >= 0) {
        int si = pk->stream_index;
        if (si < 8 && cx[si]) {
            if (si == vi) vp++; else if (si == ai) ap++;
            if (avcodec_send_packet(cx[si], pk) >= 0) {
                while (avcodec_receive_frame(cx[si], fr) >= 0) {
                    if (si == vi) {
                        vf++;
                        if (!have_sum) {
                            int y;
                            sum = 2166136261u;
                            for (y = 0; y < fr->height; y++) sum = fnv(sum, fr->data[0] + (long)y * fr->linesize[0], fr->width);
                            have_sum = 1;
                        }
                    } else { af++; samples += fr->nb_samples; }
                }
            }
        }
        av_packet_unref(pk);
    }
    /* drain */
    for (i = 0; i < 8; i++) {
        if (!cx[i]) continue;
        avcodec_send_packet(cx[i], NULL);
        while (avcodec_receive_frame(cx[i], fr) >= 0) {
            if ((int)i == vi) vf++; else { af++; samples += fr->nb_samples; }
        }
    }
    t_all = ph_us() - t_all;
    plog("%s: packets v%ld a%ld, frames v%ld a%ld (%ld samples)", path, vp, ap, vf, af, samples);
    if (have_sum) plog("%s: first video frame Y fnv1a %08lx", path, (unsigned long)sum);
    plog("%s: decode %lld ms", path, t_all / 1000);
    for (i = 0; i < 8; i++) if (cx[i]) avcodec_free_context(&cx[i]);
    av_frame_free(&fr);
    av_packet_free(&pk);
    avformat_close_input(&fc);
    av_freep(&io->buffer);
    avio_context_free(&io);
    fclose(f);
    return (vf > 0 && af > 0) ? 0 : 4;
}

/* F2 speed test: decode the video stream of `path` `reps` times with `threads` decode threads; ms per frame, fps and a
   checksum of every frame's Y plane (identical across thread counts and between NEON and the C-only build). */
static int speed(const char* path, int threads, int reps, uint32_t* sumout)
{
    int rep, w = 0, h = 0, used = 0, ok = 1;
    long frames = 0;
    long long dec_us = 0;
    uint32_t sum = 2166136261u;
    const char* ttype = "";

    for (rep = 0; rep < reps && ok; rep++) {
        FILE* f = fopen(path, "rb");
        AVFormatContext* fc = avformat_alloc_context();
        AVIOContext* io;
        AVCodecContext* cx = NULL;
        const AVCodec* cd;
        AVPacket* pk;
        AVFrame* fr;
        int vi, rc;
        long long t0;
        AVDictionary* opt = NULL;

        if (!f || !fc) { plog("speed %s: open failed", path); return 1; }
        io = avio_alloc_context((uint8_t*)av_malloc(32768), 32768, 0, f, rd, NULL, sk);
        fc->pb = io;
        if (avformat_open_input(&fc, path, NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0) { plog("speed %s: probe failed", path); return 2; }
        vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, &cd, 0);
        if (vi < 0) { plog("speed %s: no video", path); return 3; }
        cx = avcodec_alloc_context3(cd);
        avcodec_parameters_to_context(cx, fc->streams[vi]->codecpar);
        cx->thread_count = threads;
        rc = avcodec_open2(cx, cd, &opt);
        if (rc < 0) { plog("speed %s: avcodec_open2 %d", path, rc); return 4; }
        w = cx->width; h = cx->height; used = cx->thread_count;
        ttype = cx->active_thread_type == FF_THREAD_FRAME ? "frame" : cx->active_thread_type == FF_THREAD_SLICE ? "slice" : cx->active_thread_type ? "frame+slice" : "none";
        pk = av_packet_alloc();
        fr = av_frame_alloc();
        t0 = ph_us();
        while (av_read_frame(fc, pk) >= 0) {
            if (pk->stream_index == vi && avcodec_send_packet(cx, pk) >= 0) {
                while (avcodec_receive_frame(cx, fr) >= 0) {
                    frames++;
                    if (rep == 0) {
                        int y;
                        for (y = 0; y < fr->height; y++) sum = fnv(sum, fr->data[0] + (long)y * fr->linesize[0], fr->width);
                    }
                }
            }
            av_packet_unref(pk);
        }
        avcodec_send_packet(cx, NULL);
        while (avcodec_receive_frame(cx, fr) >= 0) {      /* the delayed frames (frame threads) are part of the output */
            frames++;
            if (rep == 0) {
                int y;
                for (y = 0; y < fr->height; y++) sum = fnv(sum, fr->data[0] + (long)y * fr->linesize[0], fr->width);
            }
        }
        dec_us += ph_us() - t0;
        av_frame_free(&fr);
        av_packet_free(&pk);
        avcodec_free_context(&cx);
        avformat_close_input(&fc);
        av_freep(&io->buffer);
        avio_context_free(&io);
        fclose(f);
    }
    *sumout = sum;
    if (frames == 0) { plog("speed %s: no frames", path); return 5; }
    plog("speed %s %dx%d: threads=%d (%s) %dx%ld frames, %lld ms total, %lld.%03lld ms/frame, %lld fps, Y-sum %08lx",
         path, w, h, used, ttype, reps, frames / reps, dec_us / 1000,
         dec_us / frames / 1000, dec_us / frames % 1000, 1000000LL * frames / (dec_us ? dec_us : 1), (unsigned long)sum);
    return 0;
}

int main(void)
{
    static const char* const files[] = {
        "z:/resource/phefftest/s264.mp4", "z:/resource/phefftest/svp8.webm" };
    static const char* const hd[] = { "z:/resource/phefftest/s264.mp4", "z:/resource/phefftest/s264p.mp4" };
    static const int tc[] = { 1, 2, 4 };
    int i, j, bad = 0;
    uint32_t s, s1;
    av_log_set_callback(av_cb);
    av_cpu_force_count(4);     /* no sysconf(_SC_NPROCESSORS_ONLN) in Open C: the PinePhone has 4 cores */
    plog("start, libavcodec %s, %s, %d cpus, neon %d", av_version_info(), avcodec_license(), av_cpu_count(),
         av_get_cpu_flags() & AV_CPU_FLAG_NEON ? 1 : 0);
    for (i = 0; i < 2; i++) bad += run(files[i]) != 0;
    for (i = 0; i < 2; i++) {          /* the 320x240 clip, then the 360x640 one (if staged) */
        for (j = 0; j < 3; j++) {
            int rc = speed(hd[i], tc[j], 10, &s);
            if (rc == 1 && i == 1) break;     /* optional clip missing */
            bad += rc != 0;
            if (j == 0) s1 = s;
            else if (rc == 0 && s != s1) { plog("speed %s: checksum differs with %d threads (%08lx vs %08lx)", hd[i], tc[j], (unsigned long)s, (unsigned long)s1); bad++; }
        }
    }
    plog("done, %d failed", bad);
    return bad;
}
