/* mc4.h -- what the port's files share. MIT. */
#ifndef MC4_H
#define MC4_H
#include <stdint.h>

#include "so_util.h"

extern so_module g_mc4_mod;

/* mc4_setup.c: the setup steps -- the APK's assets/ (for the NDK asset
 * manager, mc4_asset.c) and the logo and briefing videos out of the main OBB. */
#include "dcr_setup.h"
void mc4_setup_assets(RtSetupCtx *ctx);
void mc4_setup_obb(RtSetupCtx *ctx);
extern int g_mc4_trace_files; /* every opened path to debug.log */

/* mc4_kuser.c: kernel helper pointers in data -> kuser.S (staged image). */
int mc4_fix_kuser_data(so_module *m);

/* mc4_audio.c: AudioTrack.write -> audout. frames of `chans` (1 or 2)
 * interleaved s16 at `rate` Hz; blocks while the output is full. */
void mc4_audio_write(const int16_t *pcm, int frames, int chans, int rate);
void mc4_audio_pause(int paused);
/* A briefing video takes the output (the game's sound dropped meanwhile). */
void mc4_audio_video_owner(int on);
void mc4_audio_write_video(const int16_t *pcm, int frames, int chans, int rate);
void mc4_audio_shutdown(void);

/* mc4_gl.c: the EGL window context, made before the game's first GL call. */
int mc4_gl_init(void);
void mc4_gl_swap(void);
void mc4_gl_set_interval(int interval);

/* mc4_input.c: controllers and the touch screen -> the engine. */
void mc4_input_init(void);
void mc4_input_poll(int play); /* mc4_play_state(): 1 play, 0 menu, -1 unknown */

/* mc4_main.c: the Flash menu on top (MC4_HUD_MENU in gameplay), or NULL. */
const char *mc4_menu_name(void);
#define MC4_HUD_MENU "HUD"
/* mc4_main.c: GL2JNILib.nativeSetDoubleUp / Dn (sprint / crouch). */
void mc4_set_double_up(int on);
void mc4_set_double_dn(int on);
/* mc4_main.c: 1 in play, 0 in a menu (pause, death, main...), -1 unknown. */
int mc4_play_state(void);
/* The gamepad mode the game is told (MC4.java: 5, a "Generic X-Box" pad). */
#define MC4_PAD_MODE 5

/* mc4_main.c: a briefing video was asked for (mc4_jni.c). */
void mc4_video_requested(const char *name);

/* mc4_video.c: play a video (an Android path) to the end, or until A, B or
 * + skips it; on the thread that owns the GL context, between frames.
 * 0 played, -1 could not (no FFmpeg in this build, unreadable file...). */
int mc4_video_play(const char *android_path);

/* mc4_config.c: config.ini. */
typedef struct {
  int fps60;          /* 1: 60 fps, 0: 30 */
  float look_speed;   /* right stick multiplier */
  int invert_y;       /* right stick Y inverted */
  int touch;          /* touch screen passed to the game */
  int generation;     /* MC4.getGeneration: the engine's quality tier (9, 1, 0) */
  const char *lang;   /* the device language the game is told ("en", "tr"...) */
} Mc4Config;
extern Mc4Config g_mc4_cfg;

#endif /* MC4_H */
