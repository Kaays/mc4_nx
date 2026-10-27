/* mc4_video_ff.h -- the FFmpeg side of the video player (mc4_video_ff.c), as
 * plain C with no enum in it.
 *
 * Why the split: devkitARM builds with -fshort-enums (enums as small as their
 * values), and libnx32, Mesa and the runtime are built that way. FFmpeg
 * assumes int-sized enums (it returns AVERROR codes through enum-typed
 * functions, and its public structs hold enums), so it and the one file that
 * touches its structs are built with -fno-short-enums (Makefile,
 * tools/build_ffmpeg_in.sh). Nothing of either enum size crosses this header.
 * MIT. */
#ifndef MC4_VIDEO_FF_H
#define MC4_VIDEO_FF_H
#include <stddef.h>
#include <stdint.h>

typedef struct Mc3Vid Mc3Vid;

/* A decoded picture, YUV 4:2:0, valid until the next mc4vid_next. */
typedef struct {
  const uint8_t *plane[3];
  int linesize[3];
  int w, h;
  double sar;      /* sample aspect ratio (1.0 when unknown) */
  int64_t pts_us;  /* from the first picture */
} Mc3VidPic;

/* s16 stereo frames at `rate` Hz. */
typedef void (*Mc3VidAudioFn)(const int16_t *pcm, int frames, int rate, void *ud);

/* NULL when the file cannot be played; info gets a line for the log. */
Mc3Vid *mc4vid_open(const char *path, char *info, size_t info_cap);
int mc4vid_has_audio(const Mc3Vid *v);
/* Decodes on until a picture is ready (1, in *pic) or the end (0); sound
 * met on the way goes to audio(). -1 on an error that ends the video. */
int mc4vid_next(Mc3Vid *v, Mc3VidPic *pic, Mc3VidAudioFn audio, void *ud);
void mc4vid_close(Mc3Vid *v);

#endif
