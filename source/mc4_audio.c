/* mc4_audio.c -- the game's AudioTrack stream on audout.
 *
 * The engine's mixer thread writes 16-bit PCM (44.1 kHz stereo on Android,
 * whatever the AudioTrack was made with) and expects write() to block while
 * the device's buffer is full: that is what paces it. audout takes 48 kHz
 * stereo s16 in blocks of RT_AUDOUT_FRAMES (rt_audout.c), and its submit
 * blocks the same way. So each write is resampled (linear, the phase kept
 * across writes) into a block that is submitted whenever it fills.
 *
 * Only the mixer thread calls in; pause/stop come from the game's thread
 * (AudioTrack.pause/stop), and only set a flag. MIT.
 */
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <switch.h>

#include "rt_audout.h"
#include "util.h"

#include "mc4.h"

static int16_t g_block[RT_AUDOUT_FRAMES * 2];
static int g_fill;            /* frames in g_block */
static int g_open = -1;       /* -1 not tried, 0 open, 1 failed */
static atomic_int g_paused;

/* resampler: position in source frames, as 16.16 fixed point, relative to
 * the first frame of the current write; prev = the last frame of the last
 * write (the left end of the first interval). */
static uint32_t g_pos;
static int16_t g_prev[2];

static void put(int16_t l, int16_t r) {
  g_block[g_fill * 2] = l;
  g_block[g_fill * 2 + 1] = r;
  if (++g_fill == RT_AUDOUT_FRAMES) {
    rt_audout_submit(g_block);
    g_fill = 0;
  }
}

/* The game's stream and a briefing video's sound share the output: while a
 * video plays (mc4_audio_video_owner(1)) the game's writes are dropped,
 * paced as if played, and the video's go out instead. g_lock keeps the two
 * threads out of the block and the resampler at the same time. */
static Mutex g_lock;
static atomic_int g_video_owner;

static void resample_out(const int16_t *pcm, int frames, int chans, int rate);

void mc4_audio_write(const int16_t *pcm, int frames, int chans, int rate) {
  if (frames <= 0)
    return;
  if (atomic_load(&g_paused) || atomic_load(&g_video_owner)) {
    /* Paused, or a video has the output: drop the sound but take as long as
     * it would play, so the mixer thread is still paced (it writes in a
     * loop). */
    svcSleepThread((int64_t)frames * 1000000000LL / (rate > 0 ? rate : 44100));
    return;
  }
  mutexLock(&g_lock);
  resample_out(pcm, frames, chans, rate);
  mutexUnlock(&g_lock);
}

void mc4_audio_video_owner(int on) { atomic_store(&g_video_owner, on ? 1 : 0); }

void mc4_audio_write_video(const int16_t *pcm, int frames, int chans, int rate) {
  if (frames <= 0)
    return;
  mutexLock(&g_lock);
  resample_out(pcm, frames, chans, rate);
  mutexUnlock(&g_lock);
}

static void resample_out(const int16_t *pcm, int frames, int chans, int rate) {
  if (g_open < 0) {
    g_open = rt_audout_open() == 0 ? 0 : 1;
    debugPrintf("[mc4] audio: %d Hz %s in, %u Hz out%s\n", rate, chans == 1 ? "mono" : "stereo",
                rt_audout_rate(), g_open ? " (audout failed: silent)" : "");
  }
  if (g_open)
    return;
  unsigned out_rate = rt_audout_rate();
  if (rate <= 0)
    rate = 44100;
  /* step in source frames per output frame, 16.16 */
  uint32_t step = (uint32_t)(((uint64_t)rate << 16) / out_rate);
  /* interval i spans source frames i-1 .. i (frame -1 is g_prev) */
  for (;;) {
    uint32_t i = g_pos >> 16;            /* right end: source frame i */
    if (i >= (uint32_t)frames)
      break;
    uint32_t t = g_pos & 0xffff;
    int16_t a[2], b[2];
    if (i == 0) {
      a[0] = g_prev[0];
      a[1] = g_prev[1];
    } else {
      a[0] = pcm[(i - 1) * chans];
      a[1] = pcm[(i - 1) * chans + (chans - 1)];
    }
    b[0] = pcm[i * chans];
    b[1] = pcm[i * chans + (chans - 1)];
    int32_t l = a[0] + (int32_t)(((int64_t)(b[0] - a[0]) * t) >> 16);
    int32_t r = a[1] + (int32_t)(((int64_t)(b[1] - a[1]) * t) >> 16);
    put((int16_t)l, (int16_t)r);
    g_pos += step;
  }
  g_pos -= (uint32_t)frames << 16;
  g_prev[0] = pcm[(frames - 1) * chans];
  g_prev[1] = pcm[(frames - 1) * chans + (chans - 1)];
}

void mc4_audio_pause(int paused) { atomic_store(&g_paused, paused ? 1 : 0); }

void mc4_audio_shutdown(void) {
  rt_audout_cancel(1);
}
