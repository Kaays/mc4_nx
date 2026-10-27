/* mc4_gl.c -- the GL context the game's Java side would have made.
 *
 * On Android GL2JNIActivity's GLSurfaceView makes an EGL window surface and
 * a GLES 2 context, then calls into the engine from its render thread; the
 * engine itself never calls EGL (libModernCombat4.so does not link libEGL).
 * So the port makes them once, on the thread that then runs the engine,
 * before the engine's first GL call (GL2JNILib.init loads shaders).
 *
 * Through the runtime's b_egl* entry points, not <EGL/egl.h>: they exist with
 * either renderer -- gl_mesa.c (Mesa/nouveau: they also hand the screen over
 * from the boot console and size the window) or gl_null.c (draws nothing,
 * paces at 60 Hz) -- so this file builds with or without portlibs32/. MIT.
 */
#include <stdint.h>
#include <switch.h>

#include "dcr_boost.h"
#include "rt_window.h"
#include "util.h"

#include "mc4.h"

/* The EGL 1.4 values used here (Khronos egl.h). */
typedef int32_t EGLint;
typedef uint32_t EGLBoolean;
#define EGL_SUCCESS             0x3000
#define EGL_ALPHA_SIZE          0x3021
#define EGL_BLUE_SIZE           0x3022
#define EGL_GREEN_SIZE          0x3023
#define EGL_RED_SIZE            0x3024
#define EGL_DEPTH_SIZE          0x3025
#define EGL_STENCIL_SIZE        0x3026
#define EGL_SURFACE_TYPE        0x3033
#define EGL_NONE                0x3038
#define EGL_RENDERABLE_TYPE     0x3040
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WINDOW_BIT          0x0004
#define EGL_OPENGL_ES2_BIT      0x0004

/* The runtime's (gl_mesa.c / gl_null.c). Handles are pointers in both. */
void *b_eglGetDisplay(void *native);
EGLBoolean b_eglInitialize(void *dpy, EGLint *major, EGLint *minor);
EGLint b_eglGetError(void);
EGLBoolean b_eglChooseConfig(void *dpy, const EGLint *attrs, void **configs, EGLint cap, EGLint *num);
void *b_eglCreateWindowSurface(void *dpy, void *cfg, void *win, const EGLint *attrs);
void *b_eglCreateContext(void *dpy, void *cfg, void *share, const EGLint *attrs);
EGLBoolean b_eglMakeCurrent(void *dpy, void *draw, void *read, void *ctx);
EGLBoolean b_eglSwapInterval(void *dpy, EGLint interval);
EGLBoolean b_eglSwapBuffers(void *dpy, void *surface);

static void *s_dpy, *s_surf, *s_ctx;

int mc4_gl_init(void) {
  s_dpy = b_eglGetDisplay(NULL);
  if (!s_dpy || !b_eglInitialize(s_dpy, NULL, NULL)) {
    debugPrintf("[egl] no display (0x%x)\n", (unsigned)b_eglGetError());
    return -1;
  }

  /* GLSurfaceView's default for GLES 2 is RGB565 + depth 16; the engine was
   * tuned on devices that gave it RGBA8888 + depth 24 (the Xperia Play). */
  static const EGLint cfg_attrs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
  void *cfg = NULL;
  EGLint n = 0;
  if (!b_eglChooseConfig(s_dpy, cfg_attrs, &cfg, 1, &n) || n < 1) {
    debugPrintf("[egl] no RGBA8 D24S8 config (0x%x)\n", (unsigned)b_eglGetError());
    return -1;
  }
  s_surf = b_eglCreateWindowSurface(s_dpy, cfg, nwindowGetDefault(), NULL);
  if (!s_surf) {
    debugPrintf("[egl] no window surface (0x%x)\n", (unsigned)b_eglGetError());
    return -1;
  }
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  s_ctx = b_eglCreateContext(s_dpy, cfg, NULL, ctx_attrs);
  if (!s_ctx || !b_eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx)) {
    debugPrintf("[egl] no GLES 2 context (0x%x)\n", (unsigned)b_eglGetError());
    return -1;
  }
  mc4_gl_set_interval(g_mc4_cfg.fps60 ? 1 : 2);

  int w = 0, h = 0;
  dcr_window_size(&w, &h);
  debugPrintf("[egl] GLES 2 context, %dx%d, %d fps%s\n", w, h, g_mc4_cfg.fps60 ? 60 : 30,
              DCR_GL_MESA ? "" : " (null renderer: nothing is drawn)");
  return 0;
}

void mc4_gl_set_interval(int interval) {
  if (s_dpy)
    b_eglSwapInterval(s_dpy, interval);
}

void mc4_gl_swap(void) {
  static int first = 1;
  if (first) {
    first = 0;
    dcr_boost_first_picture();
  }
  b_eglSwapBuffers(s_dpy, s_surf);
  dcr_boost_launch_tick();
}
