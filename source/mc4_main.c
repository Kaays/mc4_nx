/* mc4_main.c -- Modern Combat 4 on android32: from the player's APK to the
 * game's frame loop.
 *
 * port_load: the runtime has found the APK (by its package) in the game
 *   folder; the setup unpacks lib/armeabi-v7a/libModernCombat4.so next to it
 *   (once; again only when the APK changes), which must be the 1.2.3e build
 *   (its CRC, from the setup's stamp), copies the APK's assets/ out and the
 *   videos out of the main OBB (mc4_setup.c). Then the library is mapped,
 *   relocated, bound to the runtime's bionic shims and sealed. (No SWP in
 *   this one: the MC3 port's rewrite is not needed.)
 *
 * port_run: what the Java side does on a phone, in its order (MC4.java c(),
 *   GL2JNIActivity c(), GL2JNIView's renderer):
 *     JNI_OnLoad; the installer's DRM lock pointers (what
 *     GDRMPolicy.nativeAllow sets after a good license check);
 *     GLUtils.Device.nativeInit; DataSharing.nativeInit; SUtils.nativeInit;
 *     SendInfo.initMethods; GL2JNILib.init; InAppBilling.nativeInit; MyVideoView.Init;
 *     MC4.nativeMC4Init; MyVideoView.playMovie (asks for the logo);
 *     then the surface: GL2JNILib.initGL, InitViewSettings,
 *     setNumExtraContext(0) (no extra loader contexts: the engine loads on
 *     the main thread), stateChanged(true), resize(window); and one
 *     GL2JNILib.step per frame.
 *
 * Videos (an activity on Android) are played between two frames
 * (mc4_video.c, when the build has FFmpeg), then the game is told the video
 * finished (MyVideoView.SetFinished). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_boost.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "jni.h"
#include "port_config.h"
#include "rt_applet.h"
#include "rt_boot.h"
#include "rt_cfg.h"
#include "rt_window.h"
#include "so_util.h"
#include "util.h"

#include "mc4.h"

so_module g_mc4_mod;

typedef int (*jni_onload_fn)(void *vm, void *reserved);
typedef void (*jni_void_fn)(void *env, void *cls);
typedef void (*jni_obj_fn)(void *env, void *cls, void *obj);
typedef void (*jni_int_fn)(void *env, void *cls, int v);
typedef void (*jni_size_fn)(void *env, void *cls, int w, int h);

static jni_void_fn f_step, f_pause, f_resume, f_video_finished;
static jni_int_fn f_double_up, f_double_dn;
static void *g_activity_cls, *g_glf_cls, *g_video_cls;

/* ---------------------------------------------------------------- setup */
static const char *const k_libs[] = {MC4_LIB};

/* With the APK open: its assets/. After: the videos out of the main OBB;
 * both return at once when there is nothing to do. */
static const RtSetupStep k_steps[] = {
    {"assets", RT_STEP_WITH_ZIP, 200, 220, mc4_setup_assets},
    {"videos", RT_STEP_AFTER_ZIP, 220, 1000, mc4_setup_obb},
};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .apk_requirement = "Modern Combat 4 " MC4_VERSION " (" PORT_PACKAGE ") for armeabi-v7a",
    .libs_p0 = 0,
    .libs_p1 = 200,
    .steps = k_steps,
    .nsteps = sizeof k_steps / sizeof k_steps[0],
};

const char *port_apk_help(void) {
  return "Copy the APK of your own Modern Combat 4 " MC4_VERSION " to /switch/" PORT_NAME "/\n"
         "(any name), and its two OBB files to /switch/" PORT_NAME "/obb/:\n"
         "  " MC4_OBB_MAIN "\n"
         "  " MC4_OBB_PATCH;
}

int port_load(const char *apk) {
  dcr_setup_from_apk(apk);

  unsigned long crc = 0, size = 0;
  if (rt_setup_stamp_get(MC4_LIB, &crc, &size) && (crc != MC4_LIB_CRC32 || size != MC4_LIB_SIZE))
    fatal_error("This APK's %s is not the one from Modern Combat 4 %s\n"
                "(CRC %08lx, %lu bytes; this port needs %08lx, %lu bytes).\n\n%s",
                MC4_LIB, MC4_VERSION, crc, size, MC4_LIB_CRC32, MC4_LIB_SIZE, port_apk_help());

  char path[DCR_PATH_MAX];
  rt_root_path(path, sizeof path, MC4_LIB);
  if (so_load(&g_mc4_mod, path, NULL, PORT_SO_REGION_BYTES) != 0)
    fatal_error("Could not load %s.", path);
  so_relocate(&g_mc4_mod);
  so_resolve(&g_mc4_mod, NULL, 0, 1);
  so_fix_kuser_helpers(&g_mc4_mod);
  so_finalize(&g_mc4_mod);
  debugPrintf("[mc4] %s mapped at %p\n", MC4_LIB, g_mc4_mod.load_virtbase);
  return 0;
}

/* ---------------------------------------------------------------- video
 * Asked for from inside a frame (startActivity, mc4_jni.c); played after it
 * (mc4_video.c), then the game is told it finished. The Intent names the
 * video by its path on a phone; the copy is in <SD folder>/videos/ under the
 * same file name (mc4_setup.c). */
static volatile int g_video_pending;
static char g_video_path[512];

void mc4_video_requested(const char *name) {
  const char *base = name ? strrchr(name, '/') : NULL;
  base = base ? base + 1 : (name ? name : "");
  if (*base)
    snprintf(g_video_path, sizeof g_video_path, MC4_SD_FOLDER "/videos/%s", base);
  else
    g_video_path[0] = 0;
  g_video_pending = 1;
}

/* ---------------------------------------------------------------- menus
 * The name of the Flash menu on top, read with the engine's own getter:
 * MenuManager::s_instance, MenuManager::GetMenuStackTop(-1) (the current
 * player), whose name is an STLport string at +8 (short: inline at +9;
 * long: the pointer at +0x14), as MenuManager::GetMenuByName compares it.
 * All found by their symbols (this library exports them). NULL when there is
 * none (yet). */
typedef void *(*menu_top_fn)(void *mgr, int player);
static void **g_menu_mgr;
static menu_top_fn f_menu_top;

const char *mc4_menu_name(void) {
  if (!g_menu_mgr || !f_menu_top || !*g_menu_mgr)
    return NULL;
  uint8_t *m = f_menu_top(*g_menu_mgr, -1);
  if (!m)
    return NULL;
  return (int8_t)m[8] != -1 ? (const char *)(m + 9) : *(const char **)(m + 0x14);
}

/* ---------------------------------------------------------------- sprint */
void mc4_set_double_up(int on) {
  if (f_double_up)
    f_double_up(g_jni_env, g_glf_cls, on ? 1 : 0);
}

void mc4_set_double_dn(int on) {
  if (f_double_dn)
    f_double_dn(g_jni_env, g_glf_cls, on ? 1 : 0);
}

/* ---------------------------------------------------------------- focus */
void port_focus_lost(void) {
  mc4_audio_pause(1);
  if (f_pause)
    f_pause(g_jni_env, g_activity_cls);
}

void port_focus_gained(void) {
  if (f_resume)
    f_resume(g_jni_env, g_activity_cls);
  mc4_audio_pause(0);
}

/* ---------------------------------------------------------------- run */
static void *need(const char *sym) {
  void *p = (void *)so_try_find_addr_rx(&g_mc4_mod, sym);
  if (!p)
    fatal_error("%s has no %s: not the %s build?", MC4_LIB, sym, MC4_VERSION);
  return p;
}

static void *maybe(const char *sym) { return (void *)so_try_find_addr_rx(&g_mc4_mod, sym); }

/* The installer's lock pointers (lockPointer1..4, .bss): GDRMPolicy's
 * nativeAllow points them at ints holding 1 once the license is good (the
 * same scheme the PS Vita port found in MC3). */
static int g_drm_ok[4] = {1, 1, 1, 1};

static void set_drm_flags(void) {
  static const char *const names[] = {"lockPointer1", "lockPointer2", "lockPointer3", "lockPointer4"};
  for (int i = 0; i < 4; i++) {
    int **p = (int **)maybe(names[i]);
    if (p)
      *p = &g_drm_ok[i];
    else
      debugPrintf("[mc4] no %s symbol\n", names[i]);
  }
}

#define ACT "Java_com_gameloft_android_ANMP_GloftM4HM_"
#define GLF "Java_com_gameloft_glf_GL2JNILib_"

static void call_void(const char *sym, void *cls, const char *what) {
  jni_void_fn f = (jni_void_fn)maybe(sym);
  if (!f) {
    debugPrintf("[mc4] no %s\n", sym);
    return;
  }
  f(g_jni_env, cls);
  if (what)
    debugPrintf("[mc4] %s done\n", what);
}

void port_run(void) {
  jni_init();
  so_execute_init_array(&g_mc4_mod);

  g_activity_cls = jni_class("com/gameloft/android/ANMP/GloftM4HM/MC4")->obj;
  g_glf_cls = jni_class("com/gameloft/glf/GL2JNILib")->obj;
  g_video_cls = jni_class("com/gameloft/android/ANMP/GloftM4HM/MyVideoView")->obj;
  void *device_cls = jni_class("com/gameloft/android/ANMP/GloftM4HM/GLUtils/Device")->obj;
  void *sutils_cls = jni_class("com/gameloft/android/ANMP/GloftM4HM/GLUtils/SUtils")->obj;
  void *share_cls = jni_class("com/gameloft/android/ANMP/GloftM4HM/DataSharing")->obj;
  void *iab_cls = jni_class("com/gameloft/android/ANMP/GloftM4HM/iab/InAppBilling")->obj;

  jni_onload_fn onload = (jni_onload_fn)need("JNI_OnLoad");
  debugPrintf("[mc4] JNI_OnLoad -> 0x%x\n", onload(g_jni_vm, NULL));
  set_drm_flags();

  if (mc4_gl_init() != 0)
    fatal_error("The graphics could not start (EGL). See debug.log.");

  call_void(ACT "GLUtils_Device_nativeInit", device_cls, NULL);
  call_void(ACT "DataSharing_nativeInit", share_cls, NULL);
  call_void(ACT "GLUtils_SUtils_nativeInit", sutils_cls, NULL);
  call_void(ACT "SendInfo_initMethods", jni_class("com/gameloft/android/ANMP/GloftM4HM/SendInfo")->obj, NULL);
  ((jni_void_fn)need(GLF "init"))(g_jni_env, g_glf_cls);
  debugPrintf("[mc4] GL2JNILib.init done\n");
  jni_obj_fn iab = (jni_obj_fn)maybe(ACT "iab_InAppBilling_nativeInit");
  if (iab)
    iab(g_jni_env, iab_cls, jni_singleton("android/content/Context"));
  call_void(ACT "MyVideoView_Init", g_video_cls, NULL);
  ((jni_void_fn)need(ACT "MC4_nativeMC4Init"))(g_jni_env, g_activity_cls);
  debugPrintf("[mc4] nativeMC4Init done\n");
  call_void(ACT "MyVideoView_playMovie", g_video_cls, "MyVideoView.playMovie");

  /* the surface: GL2JNIView's config chooser, context factory, renderer */
  ((jni_void_fn)need(GLF "initGL"))(g_jni_env, g_glf_cls);
  call_void(GLF "InitViewSettings", g_glf_cls, NULL);
  jni_int_fn extra = (jni_int_fn)maybe(GLF "setNumExtraContext");
  if (extra)
    extra(g_jni_env, g_glf_cls, 0);
  jni_int_fn state = (jni_int_fn)maybe(GLF "stateChanged");
  if (state)
    state(g_jni_env, g_glf_cls, 1);
  int w = 1280, h = 720;
  dcr_window_size(&w, &h);
  ((jni_size_fn)need(GLF "resize"))(g_jni_env, g_glf_cls, w, h);
  debugPrintf("[mc4] resize %dx%d\n", w, h);

  f_step = (jni_void_fn)need(GLF "step");
  f_pause = (jni_void_fn)maybe(ACT "MC4_pauseGame");
  f_resume = (jni_void_fn)maybe(ACT "MC4_resumeGame");
  f_video_finished = (jni_void_fn)maybe(ACT "MyVideoView_SetFinished");
  f_double_up = (jni_int_fn)maybe(GLF "nativeSetDoubleUp");
  f_double_dn = (jni_int_fn)maybe(GLF "nativeSetDoubleDn");
  g_menu_mgr = (void **)maybe("_ZN11MenuManager10s_instanceE");
  f_menu_top = (menu_top_fn)maybe("_ZN11MenuManager15GetMenuStackTopEi");
  if (!g_menu_mgr || !f_menu_top)
    debugPrintf("[mc4] menu manager not found: menus are told apart by + only\n");
  mc4_input_init();

  debugPrintf("[mc4] frame loop\n");
  uint64_t frame = 0;
  while (!rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll();
    if (!rt_focused()) {
      svcSleepThread(50000000LL);
      continue;
    }
    dcr_boost_frame_begin();
    mc4_input_poll(g_menu_mgr && f_menu_top);
    f_step(g_jni_env, g_glf_cls);
    mc4_gl_swap();
    if (g_video_pending) {
      g_video_pending = 0;
      if (g_video_path[0])
        mc4_video_play(g_video_path);
      if (f_video_finished)
        f_video_finished(g_jni_env, g_video_cls);
    }
    dcr_boost_frame_end(++frame);
  }
  rt_applet_stop();
  mc4_audio_shutdown();
  debugPrintf("[mc4] exit after %llu frames\n", (unsigned long long)frame);
  log_flush_ring();
}
