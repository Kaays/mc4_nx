/* mc4_video_ff.c -- demux and decode a briefing video with FFmpeg.
 *
 * Built with -fno-short-enums, like FFmpeg itself (see mc4_video_ff.h): it
 * includes nothing from libnx, the runtime or Mesa, only libc and FFmpeg.
 * MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mc4_video_ff.h"

#if MC4_VIDEO

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>

void debugPrintf(const char *fmt, ...); /* util.c (no enums in it) */

struct Mc3Vid {
  AVFormatContext *fmt;
  AVCodecContext *vd, *ad;
  int vi, ai;
  AVPacket *pkt;
  AVFrame *frame, *pic; /* pic: the picture handed out */
  AVRational vtb;
  int64_t first_pts;
  int eof, bad_fmt;
  int16_t *pcm;
  size_t pcm_cap;
};

static AVCodecContext *open_decoder(AVStream *st) {
  const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
  if (!c)
    return NULL;
  AVCodecContext *ctx = avcodec_alloc_context3(c);
  if (!ctx)
    return NULL;
  if (avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, c, NULL) < 0) {
    avcodec_free_context(&ctx);
    return NULL;
  }
  return ctx;
}

void mc4vid_close(Mc3Vid *v) {
  if (!v)
    return;
  av_frame_free(&v->frame);
  av_frame_free(&v->pic);
  av_packet_free(&v->pkt);
  avcodec_free_context(&v->vd);
  avcodec_free_context(&v->ad);
  avformat_close_input(&v->fmt);
  free(v->pcm);
  free(v);
}

Mc3Vid *mc4vid_open(const char *path, char *info, size_t info_cap) {
  if (info_cap)
    info[0] = 0;
  Mc3Vid *v = calloc(1, sizeof *v);
  if (!v)
    return NULL;
  v->first_pts = AV_NOPTS_VALUE;
  if (avformat_open_input(&v->fmt, path, NULL, NULL) < 0) {
    snprintf(info, info_cap, "cannot open");
    free(v);
    return NULL;
  }
  if (avformat_find_stream_info(v->fmt, NULL) < 0) {
    snprintf(info, info_cap, "no stream info");
    mc4vid_close(v);
    return NULL;
  }
  v->vi = av_find_best_stream(v->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  v->ai = av_find_best_stream(v->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  v->vd = v->vi >= 0 ? open_decoder(v->fmt->streams[v->vi]) : NULL;
  v->ad = v->ai >= 0 ? open_decoder(v->fmt->streams[v->ai]) : NULL;
  v->pkt = av_packet_alloc();
  v->frame = av_frame_alloc();
  v->pic = av_frame_alloc();
  if (!v->vd || !v->pkt || !v->frame || !v->pic) {
    const AVCodecParameters *p = v->vi >= 0 ? v->fmt->streams[v->vi]->codecpar : NULL;
    snprintf(info, info_cap, "no decoder for its picture (%s)",
             p ? avcodec_get_name(p->codec_id) : "no video stream");
    mc4vid_close(v);
    return NULL;
  }
  v->vtb = v->fmt->streams[v->vi]->time_base;
  snprintf(info, info_cap, "%dx%d %s%s%s", v->vd->width, v->vd->height, v->vd->codec->name,
           v->ad ? ", sound " : ", no sound", v->ad ? v->ad->codec->name : "");
  return v;
}

int mc4vid_has_audio(const Mc3Vid *v) { return v && v->ad; }

static float sample_at(const AVFrame *f, int ch, int i) {
  int nch = f->ch_layout.nb_channels;
  switch (f->format) {
  case AV_SAMPLE_FMT_FLTP: return ((const float *)f->extended_data[ch])[i];
  case AV_SAMPLE_FMT_FLT: return ((const float *)f->extended_data[0])[i * nch + ch];
  case AV_SAMPLE_FMT_S16P: return ((const int16_t *)f->extended_data[ch])[i] / 32768.0f;
  case AV_SAMPLE_FMT_S16: return ((const int16_t *)f->extended_data[0])[i * nch + ch] / 32768.0f;
  default: return 0.0f;
  }
}

static void emit_audio(Mc3Vid *v, const AVFrame *f, Mc3VidAudioFn audio, void *ud) {
  int n = f->nb_samples, nch = f->ch_layout.nb_channels;
  if (n <= 0 || nch <= 0 || !audio)
    return;
  size_t need = (size_t)n * 2;
  if (need > v->pcm_cap) {
    free(v->pcm);
    v->pcm = malloc(need * sizeof *v->pcm);
    v->pcm_cap = v->pcm ? need : 0;
    if (!v->pcm)
      return;
  }
  for (int i = 0; i < n; i++)
    for (int c = 0; c < 2; c++) {
      float s = sample_at(f, nch > 1 ? c : 0, i) * 32767.0f;
      v->pcm[i * 2 + c] = (int16_t)(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : s);
    }
  audio(v->pcm, n, f->sample_rate, ud);
}

/* 1 with a picture in v->pic, 0 when the decoder has none now */
static int take_picture(Mc3Vid *v, Mc3VidPic *pic) {
  while (avcodec_receive_frame(v->vd, v->frame) == 0) {
    if (v->frame->format != AV_PIX_FMT_YUV420P && v->frame->format != AV_PIX_FMT_YUVJ420P) {
      if (!v->bad_fmt++)
        debugPrintf("[video] pixel format %s not handled: pictures skipped\n",
                    av_get_pix_fmt_name(v->frame->format));
      continue;
    }
    av_frame_unref(v->pic);
    av_frame_move_ref(v->pic, v->frame);
    int64_t pts = v->pic->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE)
      pts = v->first_pts == AV_NOPTS_VALUE ? 0 : v->first_pts;
    if (v->first_pts == AV_NOPTS_VALUE)
      v->first_pts = pts;
    for (int i = 0; i < 3; i++) {
      pic->plane[i] = v->pic->data[i];
      pic->linesize[i] = v->pic->linesize[i];
    }
    pic->w = v->pic->width;
    pic->h = v->pic->height;
    pic->sar = v->pic->sample_aspect_ratio.num > 0 ? av_q2d(v->pic->sample_aspect_ratio) : 1.0;
    pic->pts_us = (int64_t)(av_q2d(v->vtb) * (double)(pts - v->first_pts) * 1e6);
    return 1;
  }
  return 0;
}

int mc4vid_next(Mc3Vid *v, Mc3VidPic *pic, Mc3VidAudioFn audio, void *ud) {
  for (;;) {
    if (take_picture(v, pic))
      return 1;
    if (v->eof)
      return 0;
    int r = av_read_frame(v->fmt, v->pkt);
    if (r < 0) {
      v->eof = 1;
      avcodec_send_packet(v->vd, NULL); /* drain: the last pictures come out */
      if (v->ad) {
        avcodec_send_packet(v->ad, NULL);
        while (avcodec_receive_frame(v->ad, v->frame) == 0)
          emit_audio(v, v->frame, audio, ud);
      }
      continue;
    }
    if (v->pkt->stream_index == v->vi) {
      avcodec_send_packet(v->vd, v->pkt);
    } else if (v->ad && v->pkt->stream_index == v->ai) {
      if (avcodec_send_packet(v->ad, v->pkt) == 0)
        while (avcodec_receive_frame(v->ad, v->frame) == 0)
          emit_audio(v, v->frame, audio, ud);
    }
    av_packet_unref(v->pkt);
  }
}

#endif /* MC4_VIDEO */
