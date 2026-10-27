/* mc4_video.c -- the briefing videos (an activity, video/MyVideoView, on
 * Android), played in the game's own window.
 *
 * When the game starts the activity (mc4_jni.c: startActivity, the path in
 * the Intent's "video_name"), the frame loop (mc4_main.c) calls
 * mc4_video_play between two engine frames, then tells the game the video
 * finished (MyVideoView.SetFinished), as the activity did when it closed.
 *
 * Decoding: a small FFmpeg (tools/build_ffmpeg.sh: the MP4 demuxer, H.264,
 * MPEG-4 and AAC), single-threaded. Picture: the decoder's YUV 4:2:0 planes
 * go to three luminance textures, drawn letterboxed by a BT.601 shader.
 * Sound: the AAC frames, as s16 stereo, take the game's output meanwhile
 * (mc4_audio.c). Pacing: the clock starts at the first picture; each picture
 * waits for its time; the sound's blocking writes keep it at most a few
 * buffers ahead. A, B or + (or a tap) skips. The game's GL state is saved
 * before and put back after: the engine keeps its own copy of it.
 *
 * Without FFmpeg (MC4_VIDEO unset: no portlibs32/lib/libavcodec.a) it only
 * says so; the game then goes on as if the video had played. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

#include "mc4.h"

#if !MC4_VIDEO

int mc4_video_play(const char *android_path) {
  debugPrintf("[video] %s: skipped (built without FFmpeg: tools/build_ffmpeg.sh)\n", android_path);
  return -1;
}

#else /* MC4_VIDEO */

#include <stdlib.h>
#include <switch.h>
#include <GLES2/gl2.h>

#include "dcr_path.h"
#include "rt_applet.h"
#include "rt_window.h"

#include "mc4_video_ff.h"

/* ---------------------------------------------------------------- GL */
static const char *k_vs =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

/* BT.601, limited range (what H.264 encoders of the time wrote). */
static const char *k_fs =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D t_y, t_u, t_v;\n"
    "void main() {\n"
    "  float y = 1.1643 * (texture2D(t_y, v_uv).r - 0.0625);\n"
    "  float u = texture2D(t_u, v_uv).r - 0.5;\n"
    "  float v = texture2D(t_v, v_uv).r - 0.5;\n"
    "  gl_FragColor = vec4(y + 1.5958 * v, y - 0.39173 * u - 0.81290 * v, y + 2.017 * u, 1.0);\n"
    "}\n";

static GLuint g_prog, g_tex[3];
static GLint g_a_pos, g_a_uv;

static GLuint compile(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[256];
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    debugPrintf("[video] shader: %s\n", log);
  }
  return s;
}

static int gl_setup(void) {
  if (g_prog)
    return 0;
  GLuint vs = compile(GL_VERTEX_SHADER, k_vs), fs = compile(GL_FRAGMENT_SHADER, k_fs);
  g_prog = glCreateProgram();
  glAttachShader(g_prog, vs);
  glAttachShader(g_prog, fs);
  glLinkProgram(g_prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    debugPrintf("[video] program does not link\n");
    glDeleteProgram(g_prog);
    g_prog = 0;
    return -1;
  }
  g_a_pos = glGetAttribLocation(g_prog, "a_pos");
  g_a_uv = glGetAttribLocation(g_prog, "a_uv");
  glGenTextures(3, g_tex);
  for (int i = 0; i < 3; i++) {
    glBindTexture(GL_TEXTURE_2D, g_tex[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  return 0;
}

/* What the engine may have cached and the player changes. */
typedef struct {
  GLint prog, active_tex, tex[3], array_buf, elem_buf, viewport[4], unpack;
  GLboolean blend, depth, cull, scissor, stencil;
  GLint attr_on[2];
  GLfloat clear[4];
} GlState;

static void gl_save(GlState *s) {
  glGetIntegerv(GL_CURRENT_PROGRAM, &s->prog);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &s->active_tex);
  for (int i = 0; i < 3; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex[i]);
  }
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->array_buf);
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &s->elem_buf);
  glGetIntegerv(GL_VIEWPORT, s->viewport);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &s->unpack);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, s->clear);
  s->blend = glIsEnabled(GL_BLEND);
  s->depth = glIsEnabled(GL_DEPTH_TEST);
  s->cull = glIsEnabled(GL_CULL_FACE);
  s->scissor = glIsEnabled(GL_SCISSOR_TEST);
  s->stencil = glIsEnabled(GL_STENCIL_TEST);
  glGetVertexAttribiv((GLuint)g_a_pos, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s->attr_on[0]);
  glGetVertexAttribiv((GLuint)g_a_uv, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s->attr_on[1]);
}

static void set_cap(GLenum cap, GLboolean on) {
  if (on)
    glEnable(cap);
  else
    glDisable(cap);
}

static void gl_restore(const GlState *s) {
  for (int i = 0; i < 3; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, (GLuint)s->tex[i]);
  }
  glActiveTexture((GLenum)s->active_tex);
  /* The engine may have deleted its program while it was current: once the
   * video's program replaced it, the name is gone (hardware: GL_INVALID_VALUE
   * in glUseProgram after a briefing). */
  glUseProgram(s->prog && glIsProgram((GLuint)s->prog) ? (GLuint)s->prog : 0);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s->array_buf);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)s->elem_buf);
  glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
  glPixelStorei(GL_UNPACK_ALIGNMENT, s->unpack);
  glClearColor(s->clear[0], s->clear[1], s->clear[2], s->clear[3]);
  set_cap(GL_BLEND, s->blend);
  set_cap(GL_DEPTH_TEST, s->depth);
  set_cap(GL_CULL_FACE, s->cull);
  set_cap(GL_SCISSOR_TEST, s->scissor);
  set_cap(GL_STENCIL_TEST, s->stencil);
  if (s->attr_on[0])
    glEnableVertexAttribArray((GLuint)g_a_pos);
  else
    glDisableVertexAttribArray((GLuint)g_a_pos);
  if (s->attr_on[1])
    glEnableVertexAttribArray((GLuint)g_a_uv);
  else
    glDisableVertexAttribArray((GLuint)g_a_uv);
}

/* One plane: rows copied tight when the decoder pads them (GLES 2 has no
 * GL_UNPACK_ROW_LENGTH). */
static uint8_t *g_tight;
static size_t g_tight_cap;

static void upload(int i, const uint8_t *data, int linesize, int w, int h, int realloc_tex) {
  const uint8_t *src = data;
  if (linesize != w) {
    size_t need = (size_t)w * (size_t)h;
    if (need > g_tight_cap) {
      free(g_tight);
      g_tight = malloc(need);
      g_tight_cap = g_tight ? need : 0;
      if (!g_tight)
        return;
    }
    for (int y = 0; y < h; y++)
      memcpy(g_tight + (size_t)y * w, data + (size_t)y * linesize, (size_t)w);
    src = g_tight;
  }
  glActiveTexture(GL_TEXTURE0 + i);
  glBindTexture(GL_TEXTURE_2D, g_tex[i]);
  if (realloc_tex)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, src);
  else
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE, src);
}

static void draw(const Mc3VidPic *f, int realloc_tex) {
  int ww = 1280, wh = 720;
  dcr_window_size(&ww, &wh);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  upload(0, f->plane[0], f->linesize[0], f->w, f->h, realloc_tex);
  upload(1, f->plane[1], f->linesize[1], (f->w + 1) / 2, (f->h + 1) / 2, realloc_tex);
  upload(2, f->plane[2], f->linesize[2], (f->w + 1) / 2, (f->h + 1) / 2, realloc_tex);

  /* letterbox: the picture's aspect (sample aspect included) in the window */
  double pic = (double)f->w * f->sar / (double)f->h, win = (double)ww / (double)wh;
  float sx = 1.0f, sy = 1.0f;
  if (pic > win)
    sy = (float)(win / pic);
  else
    sx = (float)(pic / win);
  const GLfloat quad[] = {-sx, -sy, 0, 1, sx, -sy, 1, 1, -sx, sy, 0, 0, sx, sy, 1, 0};

  glViewport(0, 0, ww, wh);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(g_prog);
  glUniform1i(glGetUniformLocation(g_prog, "t_y"), 0);
  glUniform1i(glGetUniformLocation(g_prog, "t_u"), 1);
  glUniform1i(glGetUniformLocation(g_prog, "t_v"), 2);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glEnableVertexAttribArray((GLuint)g_a_pos);
  glEnableVertexAttribArray((GLuint)g_a_uv);
  glVertexAttribPointer((GLuint)g_a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), quad);
  glVertexAttribPointer((GLuint)g_a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), quad + 2);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray((GLuint)g_a_pos);
  glDisableVertexAttribArray((GLuint)g_a_uv);
}

/* ---------------------------------------------------------------- sound
 * MP4s interleave their sound in runs (half a second of AAC packets in a
 * row): written straight from the read loop, each blocking write would hold
 * the pictures back. So the decoded sound goes into a ring, and a thread of
 * its own plays it at the device's pace (mc4_audio_write_video blocks). */
#define RING_FRAMES (48000 * 2) /* two seconds of stereo frames */
static int16_t g_ring[RING_FRAMES * 2];
static volatile unsigned g_ring_r, g_ring_w; /* frame counts; w - r = queued */
static volatile int g_ring_rate, g_audio_run, g_audio_flush;
static Thread g_audio_thread;

static void audio_thread(void *arg) {
  (void)arg;
  static int16_t chunk[1024 * 2];
  while (g_audio_run) {
    unsigned q = g_ring_w - g_ring_r;
    if (q == 0) {
      if (g_audio_flush)
        break;
      svcSleepThread(5000000LL);
      continue;
    }
    unsigned n = q > 1024 ? 1024 : q;
    for (unsigned i = 0; i < n; i++) {
      unsigned k = (g_ring_r + i) % RING_FRAMES;
      chunk[i * 2] = g_ring[k * 2];
      chunk[i * 2 + 1] = g_ring[k * 2 + 1];
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    g_ring_r += n;
    mc4_audio_write_video(chunk, (int)n, 2, g_ring_rate);
  }
}

static int audio_start(void) {
  g_ring_r = g_ring_w = 0;
  g_audio_run = 1;
  g_audio_flush = 0;
  if (R_FAILED(threadCreate(&g_audio_thread, audio_thread, NULL, NULL, 0x8000, 0x2A, -2)))
    return -1;
  if (R_FAILED(threadStart(&g_audio_thread))) {
    threadClose(&g_audio_thread);
    return -1;
  }
  return 0;
}

/* played to the end (flush) or cut off */
static void audio_stop(int flush) {
  if (flush)
    g_audio_flush = 1;
  else
    g_audio_run = 0;
  threadWaitForExit(&g_audio_thread);
  threadClose(&g_audio_thread);
  g_audio_run = 0;
}

/* Into the ring (mc4vid_next's sound callback); waits, rarely, while full. */
static void queue_audio(const int16_t *pcm, int n, int rate, void *ud) {
  (void)ud;
  g_ring_rate = rate;
  for (int i = 0; i < n; i++) {
    while (g_ring_w - g_ring_r >= RING_FRAMES && g_audio_run)
      svcSleepThread(2000000LL);
    unsigned k = g_ring_w % RING_FRAMES;
    g_ring[k * 2] = pcm[i * 2];
    g_ring[k * 2 + 1] = pcm[i * 2 + 1];
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    g_ring_w++;
  }
}

/* ---------------------------------------------------------------- skip */
static PadState g_vpad;
static int g_vpad_ready;

static int skip_pressed(void) {
  if (!g_vpad_ready) {
    padInitializeDefault(&g_vpad);
    g_vpad_ready = 1;
  }
  padUpdate(&g_vpad);
  if (padGetButtonsDown(&g_vpad) & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
    return 1;
  static int was_touching;
  HidTouchScreenState t;
  int touching = hidGetTouchScreenStates(&t, 1) && t.count > 0;
  int tap = was_touching && !touching;
  was_touching = touching;
  return tap;
}

/* ---------------------------------------------------------------- play */
static uint64_t now_us(void) { return armTicksToNs(armGetSystemTick()) / 1000; }

int mc4_video_play(const char *android_path) {
  char real_buf[DCR_PATH_MAX], info[160];
  const char *real = dcr_translate_path(android_path, real_buf, sizeof real_buf);
  const char *name = strrchr(real, '/') ? strrchr(real, '/') + 1 : real;
  /* "file:" in front: FFmpeg would take "sdmc:" for a protocol and refuse
   * it (hardware: "Briefing_M01.mp4: cannot open"). */
  char url[DCR_PATH_MAX + 8];
  snprintf(url, sizeof url, "file:%s", real);
  Mc3Vid *v = mc4vid_open(url, info, sizeof info);
  if (!v || gl_setup() != 0) {
    debugPrintf("[video] %s: %s\n", name, v ? "no GL program" : info);
    mc4vid_close(v);
    return -1;
  }
  debugPrintf("[video] %s: %s\n", name, info);

  int sound = mc4vid_has_audio(v);
  if (sound && audio_start() != 0) {
    debugPrintf("[video] no sound thread: playing without sound\n");
    sound = 0;
  }
  if (sound)
    mc4_audio_video_owner(1);
  GlState saved;
  gl_save(&saved);

  Mc3VidPic pic;
  uint64_t t0 = 0;
  int tex_w = 0, tex_h = 0, shown = 0, late = 0, skipped = 0, ended = 0;
  while (!rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll();
    if (skip_pressed()) {
      skipped = 1;
      break;
    }
    int r = mc4vid_next(v, &pic, sound ? queue_audio : NULL, NULL);
    if (r <= 0) {
      ended = 1;
      break;
    }
    if (!shown && !late)
      t0 = now_us(); /* the clock starts at the first picture */
    uint64_t due = t0 + (uint64_t)pic.pts_us, now = now_us();
    if (now + 1000 < due) {
      svcSleepThread((int64_t)(due - now) * 1000);
    } else if (now > due + 100000) { /* over 100 ms late: decoded, not drawn */
      late++;
      continue;
    }
    int realloc_tex = pic.w != tex_w || pic.h != tex_h;
    tex_w = pic.w;
    tex_h = pic.h;
    draw(&pic, realloc_tex);
    mc4_gl_swap();
    shown++;
  }
  if (sound) {
    audio_stop(ended && !skipped); /* played out, or cut off (skip, exit) */
    mc4_audio_video_owner(0);
  }
  gl_restore(&saved);
  mc4vid_close(v);
  debugPrintf("[video] %d pictures shown, %d late%s\n", shown, late, skipped ? ", skipped" : "");
  return 0;
}

#endif /* MC4_VIDEO */
