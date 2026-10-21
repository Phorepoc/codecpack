/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later */

/* pheffmpeg.c -- OURS: a small C API over FFmpeg's AAC decoder for phevidctl (see pheffmpeg.mmp). No writable static data of
   our own (all state is in the heap object; FFmpeg's own tables are why the DLL is EPOCALLOWDLLDATA).
     PheFfVersion()                                  -> 1
     PheAacOpen(asc, ascLen)                         -> decoder handle or NULL
     PheAacDecode(h, pkt, len, pcm, maxFrames, &rate, &ch)
                                                     -> PCM frames (per channel) written, 0 = none yet, <0 = error
                                                        pcm = interleaved S16, ch = 1 or 2 (channels > 2: first two kept)
     PheAacSetOutput(h, rate, ch)  (ordinal 5)       -> 0 ok: from now on PheAacDecode returns rate/ch interleaved S16 converted with
                                                        libswresample (any source rate/layout; the A64 sound path takes 48000 Hz stereo)
     PheAacClose(h)                                                                                                   */
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libavutil/cpu.h>

typedef struct PheAac
{
    AVCodecContext *cx;
    AVFrame *fr;
    AVPacket *pk;
    SwrContext *swr;
    int outRate, outCh;
} PheAac;

int PheFfVersion(void)
{
    return 1;
}

void PheAacClose(PheAac *h);

PheAac *PheAacOpen(const unsigned char *asc, int ascLen)
{
    const AVCodec *cd = avcodec_find_decoder(AV_CODEC_ID_AAC);
    PheAac *h;
    int n;
    if (!cd || ascLen <= 0)
        return NULL;
    h = (PheAac *)calloc(1, sizeof(*h));
    if (!h)
        return NULL;
    h->cx = avcodec_alloc_context3(cd);
    h->fr = av_frame_alloc();
    h->pk = av_packet_alloc();
    if (!h->cx || !h->fr || !h->pk)
        goto fail;
    h->cx->extradata = (uint8_t *)av_mallocz(ascLen + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!h->cx->extradata)
        goto fail;
    memcpy(h->cx->extradata, asc, ascLen);
    h->cx->extradata_size = ascLen;
    h->cx->thread_count = 1;
    av_force_cpu_flags(0);          /* x32: FFmpeg's x86 asm assumes 8-byte pointers in pointer arrays -- audio stays on the C paths */
    n = avcodec_open2(h->cx, cd, NULL);
    av_force_cpu_flags(-1);
    if (n < 0)
        goto fail;
    return h;
fail:
    PheAacClose(h);
    return NULL;
}

static short Clip16(float v)
{
    int i = (int)(v * 32768.0f + (v < 0 ? -0.5f : 0.5f));
    return (short)(i > 32767 ? 32767 : i < -32768 ? -32768 : i);
}

int PheAacDecode(PheAac *h, const unsigned char *pkt, int len, short *pcm, int maxFrames, int *rate, int *ch)
{
    int n, c, i, outCh;
    if (!h || !pcm)
        return -1;
    if (pkt && len > 0)
    {
        /* the packet borrows the caller's bytes (decode copies what it keeps); padding is not needed for AAC's bit reader
           beyond what av_packet gives, so copy into a padded packet buffer */
        if (av_new_packet(h->pk, len) < 0)
            return -2;
        memcpy(h->pk->data, pkt, len);
        n = avcodec_send_packet(h->cx, h->pk);
        av_packet_unref(h->pk);
        if (n < 0)
            return n;
    }
    n = avcodec_receive_frame(h->cx, h->fr);
    if (n == AVERROR(EAGAIN) || n == AVERROR_EOF)
        return 0;
    if (n < 0)
        return n;
    c = h->fr->ch_layout.nb_channels;
    if (h->outRate > 0)
    {
        /* converted output: libswresample (format, channel layout and rate in one stateful pass) */
        uint8_t *dst[1];
        AVChannelLayout ol;
        if (c < 1)
        {
            av_frame_unref(h->fr);
            return -3;
        }
        if (!h->swr)
        {
            av_channel_layout_default(&ol, h->outCh);
            i = swr_alloc_set_opts2(&h->swr, &ol, AV_SAMPLE_FMT_S16, h->outRate, &h->fr->ch_layout,
                                    (enum AVSampleFormat)h->fr->format, h->fr->sample_rate, 0, NULL);
            if (i >= 0 && c == 1 && h->outCh == 2)
            {
                static const double mono2st[2] = { 1.0, 1.0 };   /* mono -> L = R = mono (swresample defaults to -3 dB per side) */
                i = swr_set_matrix(h->swr, mono2st, 1);
            }
            av_force_cpu_flags(0);
            n = i < 0 ? -1 : swr_init(h->swr);
            av_force_cpu_flags(-1);
            if (i < 0 || n < 0)
            {
                swr_free(&h->swr);
                av_frame_unref(h->fr);
                return -5;
            }
        }
        dst[0] = (uint8_t *)pcm;
        n = swr_convert(h->swr, dst, maxFrames, (const uint8_t **)h->fr->extended_data, h->fr->nb_samples);
        av_frame_unref(h->fr);
        if (n < 0)
            return -6;
        if (rate)
            *rate = h->outRate;
        if (ch)
            *ch = h->outCh;
        return n;
    }
    outCh = c >= 2 ? 2 : 1;
    n = h->fr->nb_samples;
    if (n > maxFrames || c < 1)
    {
        av_frame_unref(h->fr);
        return -3;
    }
    if (h->fr->format == AV_SAMPLE_FMT_FLTP)
    {
        for (i = 0; i < n; i++)
        {
            pcm[i * outCh] = Clip16(((const float *)h->fr->extended_data[0])[i]);
            if (outCh == 2)
                pcm[i * outCh + 1] = Clip16(((const float *)h->fr->extended_data[1])[i]);
        }
    }
    else if (h->fr->format == AV_SAMPLE_FMT_FLT)
    {
        for (i = 0; i < n; i++)
        {
            pcm[i * outCh] = Clip16(((const float *)h->fr->extended_data[0])[i * c]);
            if (outCh == 2)
                pcm[i * outCh + 1] = Clip16(((const float *)h->fr->extended_data[0])[i * c + 1]);
        }
    }
    else
    {
        av_frame_unref(h->fr);
        return -4;
    }
    if (rate)
        *rate = h->fr->sample_rate;
    if (ch)
        *ch = outCh;
    av_frame_unref(h->fr);
    return n;
}

void PheAacClose(PheAac *h)
{
    if (!h)
        return;
    swr_free(&h->swr);
    av_packet_free(&h->pk);
    av_frame_free(&h->fr);
    avcodec_free_context(&h->cx);
    free(h);
}

int PheAacSetOutput(PheAac *h, int rate, int ch)
{
    if (!h || rate < 8000 || rate > 192000 || (ch != 1 && ch != 2))
        return -1;
    swr_free(&h->swr);
    h->outRate = rate;
    h->outCh = ch;
    return 0;
}

/* ------------------------------------------------------------------------------------------------------------------
   Software video decode (2026-10-16, Path C x86: no hardware video engine there). phevdec's PHE_SW build (src/phevdec) feeds
   Annex B H.264 / MPEG-2 here and takes NV12 pictures back, in DISPLAY order (libavcodec's own reorder).
     PheVidOpen(codec, flags)       codec 0 = H.264, 1 = MPEG-2; flags bit 0 = the input is an arbitrary byte-stream chunking
                                    (av_parser splits access units), 0 = every buffer already is one coded picture;
                                    bits 8-15 = decoder threads (frame threading), 0/1 = one
     PheVidSend(h, data, len, pts)  pts in microseconds (-1 = none); data == NULL, len == 0 = end of stream (drain)
                                    -> 0 ok, <0 error
     PheVidReceive(h, dst, pitch, maxRows, &w, &h, &pts)   (dst == NULL: only report size + pts of the next picture)
                                    -> 1 = one picture written as NV12 (Y rows then interleaved UV rows at `pitch`, chroma
                                       starting at row `maxRows`), 0 = none now, <0 = error (-7: not 4:2:0 8-bit)
     PheVidClose(h)                                                                                                    */
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>

typedef struct PheVid
{
    AVCodecContext *cx;
    AVCodecParserContext *ps;
    AVFrame *fr;
    AVPacket *pk;
    int draining, pending;
} PheVid;

void PheVidClose(PheVid *h);

PheVid *PheVidOpen(int codec, int flags)
{
    int useParser = flags & 1, threads = (flags >> 8) & 0xFF;
    const AVCodec *cd = avcodec_find_decoder(codec == 1 ? AV_CODEC_ID_MPEG2VIDEO : AV_CODEC_ID_H264);
    PheVid *h;
    if (!cd)
        return NULL;
    h = (PheVid *)calloc(1, sizeof(*h));
    if (!h)
        return NULL;
    h->cx = avcodec_alloc_context3(cd);
    h->fr = av_frame_alloc();
    h->pk = av_packet_alloc();
    if (!h->cx || !h->fr || !h->pk)
        goto fail;
    if (useParser)
    {
        h->ps = av_parser_init(cd->id);
        if (!h->ps)
            goto fail;
    }
    h->cx->thread_count = threads > 1 ? threads : 1;     /* frame threading (pthread_frame.c over Open C pthreads); adds threads-1 frames of latency */
    h->cx->thread_type = FF_THREAD_FRAME;
    h->cx->flags2 |= AV_CODEC_FLAG2_FAST;
    if (avcodec_open2(h->cx, cd, NULL) < 0)
        goto fail;
    return h;
fail:
    PheVidClose(h);
    return NULL;
}

static int PheVidSendPkt(PheVid *h, const unsigned char *data, int len, long long pts)
{
    int n;
    if (av_new_packet(h->pk, len) < 0)
        return -2;
    memcpy(h->pk->data, data, len);
    h->pk->pts = pts < 0 ? AV_NOPTS_VALUE : pts;
    h->pk->dts = h->pk->pts;
    n = avcodec_send_packet(h->cx, h->pk);
    av_packet_unref(h->pk);
    return n == AVERROR(EAGAIN) ? 0 : n;
}

int PheVidSend(PheVid *h, const unsigned char *data, int len, long long pts)
{
    int n;
    if (!h)
        return -1;
    if (!data || len <= 0)
    {
        if (h->ps)                              /* the parser may hold the last access unit */
        {
            uint8_t *od = NULL;
            int os = 0;
            av_parser_parse2(h->ps, h->cx, &od, &os, NULL, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (od && os > 0)
                PheVidSendPkt(h, od, os, pts);
        }
        if (!h->draining)
        {
            h->draining = 1;
            avcodec_send_packet(h->cx, NULL);
        }
        return 0;
    }
    if (!h->ps)
        return PheVidSendPkt(h, data, len, pts);
    while (len > 0)
    {
        uint8_t *od = NULL;
        int os = 0;
        int used = av_parser_parse2(h->ps, h->cx, &od, &os, data, len, pts < 0 ? AV_NOPTS_VALUE : pts, AV_NOPTS_VALUE, 0);
        if (used < 0)
            return used;
        data += used;
        len -= used;
        if (od && os > 0)
        {
            n = PheVidSendPkt(h, od, os, h->ps->pts == AV_NOPTS_VALUE ? pts : h->ps->pts);
            if (n < 0)
                return n;
        }
        else if (used == 0)
            break;
    }
    return 0;
}

int PheVidReceive(PheVid *h, unsigned char *dst, int pitch, int maxRows, int *w, int *ht, long long *pts)
{
    int n, y, x, cw, chh;
    const AVFrame *f;
    if (!h)
        return -1;
    if (!h->pending)
    {
        n = avcodec_receive_frame(h->cx, h->fr);
        if (n == AVERROR(EAGAIN) || n == AVERROR_EOF)
            return 0;
        if (n < 0)
            return n;
        h->pending = 1;
    }
    f = h->fr;
    if (!dst)                                   /* peek: the size and pts of the next picture; it stays inside */
    {
        if (w)
            *w = f->width;
        if (ht)
            *ht = f->height;
        if (pts)
            *pts = f->best_effort_timestamp == AV_NOPTS_VALUE ? -1 : f->best_effort_timestamp;
        return 1;
    }
    h->pending = 0;
    if ((f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) || f->width > pitch || f->height > maxRows)
    {
        av_frame_unref(h->fr);
        return f->width > pitch || f->height > maxRows ? -3 : -7;
    }
    for (y = 0; y < f->height; y++)
        memcpy(dst + (size_t)y * pitch, f->data[0] + (size_t)y * f->linesize[0], f->width);
    cw = (f->width + 1) / 2;
    chh = (f->height + 1) / 2;
    for (y = 0; y < chh; y++)
    {
        const uint8_t *u = f->data[1] + (size_t)y * f->linesize[1];
        const uint8_t *v = f->data[2] + (size_t)y * f->linesize[2];
        uint8_t *d = dst + (size_t)(maxRows + y) * pitch;
        for (x = 0; x < cw; x++)
        {
            d[2 * x] = u[x];
            d[2 * x + 1] = v[x];
        }
    }
    if (w)
        *w = f->width;
    if (ht)
        *ht = f->height;
    if (pts)
        *pts = f->best_effort_timestamp == AV_NOPTS_VALUE ? -1 : f->best_effort_timestamp;
    av_frame_unref(h->fr);
    return 1;
}

void PheVidClose(PheVid *h)
{
    if (!h)
        return;
    if (h->ps)
        av_parser_close(h->ps);
    av_packet_free(&h->pk);
    av_frame_free(&h->fr);
    avcodec_free_context(&h->cx);
    free(h);
}
