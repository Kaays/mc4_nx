/* mc4_jni.c -- the Java side of Modern Combat 4, answered in C.
 *
 * The game's GLF layer (com/gameloft/glf), its activity (MC4), SUtils,
 * Device, the installer, billing, the social libraries, the video activity
 * and a few framework classes (AudioTrack, Intent, Bundle) are reached
 * through JNI. There is no PS Vita port of this game to borrow answers from:
 * these come from the APK's own Java (MC4.java and friends), what each method
 * returns on a phone, through the runtime's port_jni_invoke hook, which sees
 * every call first and matches by NAME (the return kind from the signature).
 *
 * The device presented: an Xperia Play, slid open, like the MC3 port: the
 * engine then takes keys (keyboardEvent) and the two touch pads, and hides
 * its touch buttons. Online is answered "no network" (the servers are gone),
 * so the game does not wait on them.
 *
 * Folders are Android paths; the runtime translates them (dcr_path.c):
 * /sdcard/Android/obb/<pkg>/ -> obb/, /sdcard/gameloft/... -> sdcard/gameloft/,
 * /data/data/<pkg>/ -> data/. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <unistd.h>

#include "dcr_path.h"
#include "dcr_setup.h"
#include "jni.h"
#include "rt_applet.h"
#include "port_config.h"
#include "util.h"

#include "mc4.h"

#define ANDROID_OBB   "/sdcard/Android/obb/" PORT_PACKAGE "/"
#define APP_FILES     "/data/data/" PORT_PACKAGE "/files"

/* ------------------------------------------------------------ helpers
 * Strings handed to the game are immortal, one per value. The engine does
 *   chars = GetStringUTFChars(s); DeleteLocalRef(s); ... use chars ...
 * which is fine on Android (the chars are a copy that lives until
 * ReleaseStringUTFChars) but not on the runtime, whose GetStringUTFChars
 * returns the string's own buffer and whose DeleteLocalRef frees it at once:
 * the OBB paths came back with malloc's free-list pointers over "/sdcard/"
 * (hardware log: fopen("./@?)@?)Android/obb/...")). The answers are a few
 * fixed strings, so keeping each one alive for good costs nothing. */
static jvalue ret_str(const char *s) {
  static struct {
    char *val;
    JObj *o;
  } cache[64];
  static int n;
  static Mutex lock;
  if (!s)
    return jv_l(NULL);
  mutexLock(&lock);
  for (int i = 0; i < n; i++)
    if (!strcmp(cache[i].val, s)) {
      JObj *o = cache[i].o;
      mutexUnlock(&lock);
      return jv_l(o);
    }
  JObj *o = jni_str(s);
  if (o) {
    o->immortal = 1;
    if (n < (int)(sizeof cache / sizeof cache[0])) {
      cache[n].val = strdup(s);
      cache[n].o = o;
      if (cache[n].val)
        n++;
    }
  }
  mutexUnlock(&lock);
  return jv_l(o);
}

/* A placeholder object of the method's own return class, else of its class. */
static jvalue ret_obj_of(const JMethod *m, const char *fallback_cls) {
  const char *r = strchr(m->sig, ')');
  if (r && r[1] == 'L') {
    char cls[112];
    const char *b = r + 2, *e = strchr(b, ';');
    size_t n = e ? (size_t)(e - b) : strlen(b);
    if (n >= sizeof cls)
      n = sizeof cls - 1;
    memcpy(cls, b, n);
    cls[n] = 0;
    if (!strcmp(cls, "java/lang/String"))
      return ret_str("");
    if (!strcmp(cls, "java/lang/Object") && fallback_cls)
      return jv_l(jni_singleton(fallback_cls));
    return jv_l(jni_singleton(cls));
  }
  return jv_l(jni_singleton(fallback_cls ? fallback_cls : m->cls->name));
}

/* ------------------------------------------------------------ strings */
static JNI_H_DECL(h_empty_str) { return ret_str(""); }
static JNI_H_DECL(h_sd_folder) { return ret_str(MC4_SD_FOLDER); }
static JNI_H_DECL(h_save_folder) { return ret_str(APP_FILES); }
static JNI_H_DECL(h_main_obb) { return ret_str(ANDROID_OBB MC4_OBB_MAIN); }
static JNI_H_DECL(h_patch_obb) { return ret_str(ANDROID_OBB MC4_OBB_PATCH); }
static JNI_H_DECL(h_country) { return ret_str("US"); }
static JNI_H_DECL(h_imei) { return ret_str("351066496380730"); }
static JNI_H_DECL(h_package) { return ret_str(PORT_PACKAGE); }
/* SUtils.GetApkPath: ApplicationInfo.sourceDir, which the runtime maps to
 * the player's APK whatever it is called. BITrackingManager::LaunchGame opens
 * it with libzip to read lib/armeabi-v7a/libModernCombat4.so; with "" the
 * archive was NULL and _zip_name_locate read it (hardware). */
static JNI_H_DECL(h_apk_path) { return ret_str("/data/app/" PORT_PACKAGE "-1/base.apk"); }
static JNI_H_DECL(h_firmware) { return ret_str("4.4.2"); }

static JNI_H_DECL(h_language) { return ret_str(g_mc4_cfg.lang); }
static JNI_H_DECL(h_manufacturer) { return ret_str("Nintendo"); }
static JNI_H_DECL(h_model) { return ret_str("Switch"); }

/* ------------------------------------------------------------ assets
 * GL2JNILib.getResource(name): the whole file out of the APK's assets/
 * (getAssets().open), a leading "." dropped; null when there is none. Here
 * from the copy in <game folder>/assets/ (mc4_setup.c). glot's tracking reads
 * eventParameters.json this way, and died on the null the runtime answered
 * (hardware: TrackingManager::PrepareEventParameters, a NULL+0x90 read). */
static JNI_H_DECL(h_get_resource) {
  const char *name = a ? jni_utf(a[0].l) : "";
  while (*name == '.' || *name == '/')
    name++;
  char rel[512], path[DCR_PATH_MAX];
  snprintf(rel, sizeof rel, "assets/%s", name);
  rt_root_path(path, sizeof path, rel);
  size_t len = 0;
  uint8_t *buf = rt_read_whole(path, &len);
  if (!buf) {
    debugPrintf("[mc4] getResource(%s): not in assets/\n", name);
    return jv_l(NULL);
  }
  JObj *arr = jni_array('B', (jsize)len);
  if (arr)
    memcpy(arr->a.data, buf, len);
  free(buf);
  debugPrintf("[mc4] getResource(%s): %u bytes\n", name, (unsigned)len);
  return jv_l(arr);
}

/* A gamepad is connected: MC4.java's "Generic X-Box" mode (5). The engine
 * then reads the right stick (Hud::UpdateCameraControls looks at
 * nativeSetTouchPadDT only with a pad mode set: Application+0xa8c, which
 * MC4.nativeSetIsMogaConnected writes; mc4_main.c sets it). */
static JNI_H_DECL(h_pad_mode) { return jv_i(MC4_PAD_MODE); }

/* GL2JNILib.exitGame (Exit in the main menu). On a phone the activity
 * finishes and the process dies. Here the game went on, in the same frame,
 * into glf::AndroidDestroy and its destructors, and died in one of them
 * (hardware: BITrackingManager's); a key sent after it reached
 * Application::AddKeyUp on a NULL Application. So the process ends here, as
 * Android's would: the sound stopped, the log written, _exit (libnx closes
 * its services). The game saves as it goes, not on exit. */
static JNI_H_DECL(h_exit_game) {
  debugPrintf("[mc4] exitGame: leaving\n");
  rt_request_exit();
  mc4_audio_shutdown();
  log_flush_ring();
  _exit(0);
  return jv_none();
}

/* ------------------------------------------------------------ device id
 * SendInfo.getGLUID: four numbers made at random once and kept in the app's
 * preferences ("GLU"), the id the game ties its save to. The runtime
 * answered null, the engine made a new one at every start
 * ("SAVE GetLocalDeviceGLUID_OLD = ..." changed each time) and the saved
 * progress (campaign money) did not load. Made once here, kept in
 * <game folder>/data/gluid.bin. getGLUIDX86: the same as ints. */
static uint32_t g_gluid[4];

static void gluid_load(void) {
  static int done;
  if (done)
    return;
  done = 1;
  char path[DCR_PATH_MAX];
  rt_root_path(path, sizeof path, "data/gluid.bin");
  size_t len = 0;
  uint8_t *buf = rt_read_whole(path, &len);
  if (buf && len == sizeof g_gluid) {
    memcpy(g_gluid, buf, sizeof g_gluid);
    free(buf);
    return;
  }
  free(buf);
  randomGet(g_gluid, sizeof g_gluid);
  for (int i = 0; i < 4; i++)
    g_gluid[i] &= 0x7fffffffu; /* MC4.java keeps them positive */
  if (!rt_write_atomic(path, g_gluid, sizeof g_gluid))
    debugPrintf("[mc4] could not keep the device id in %s\n", path);
  debugPrintf("[mc4] new device id %u %u %u %u\n", g_gluid[0], g_gluid[1], g_gluid[2], g_gluid[3]);
}

static JNI_H_DECL(h_gluid) {
  gluid_load();
  JObj *arr = jni_array('J', 4);
  if (arr)
    for (int i = 0; i < 4; i++)
      ((jlong *)arr->a.data)[i] = g_gluid[i];
  return jv_l(arr);
}

static JNI_H_DECL(h_gluid_x86) {
  gluid_load();
  JObj *arr = jni_array('I', 4);
  if (arr)
    for (int i = 0; i < 4; i++)
      ((jint *)arr->a.data)[i] = (jint)g_gluid[i];
  return jv_l(arr);
}

/* Android id / serial: stable strings from the same id (the save and the
 * "shared values" key on them too). */
static JNI_H_DECL(h_android_id) {
  gluid_load();
  char b[24];
  snprintf(b, sizeof b, "%08x%08x", g_gluid[0], g_gluid[1]);
  return ret_str(b);
}

/* ------------------------------------------------------------ objects */
static JNI_H_DECL(h_context) { return jv_l(jni_singleton("android/content/Context")); }
static JNI_H_DECL(h_object) { return ret_obj_of(m, NULL); }
/* nativeGetPreference(Bundle) -> the Bundle it was given */
static JNI_H_DECL(h_first_arg) { return jv_l(a && a[0].l ? jni_retain(a[0].l) : ret_obj_of(m, NULL).l); }

/* ------------------------------------------------------------ values
 * MC4.java's answers for a phone, chosen for the Switch where they are the
 * device's own: three cores, an ARM Cortex-A57 (/proc/cpuinfo's "CPU
 * implementer" 0x41, "CPU part" 0xd07), Android 4.4. getGeneration is the
 * engine's quality tier: 9 for every device MC4.java does not list as slow
 * (config.ini graphics_quality picks a lower one). */
static JNI_H_DECL(h_cores) { return jv_i(3); }
static JNI_H_DECL(h_cpu_impl) { return jv_i(0x41); }
static JNI_H_DECL(h_cpu_part) { return jv_i(0xd07); }
static JNI_H_DECL(h_sdk) { return jv_i(19); }
static JNI_H_DECL(h_volume) { return jv_i(100); }
static JNI_H_DECL(h_generation) { return jv_i(g_mc4_cfg.generation); }
static JNI_H_DECL(h_uptime) { return jv_j((jlong)(armTicksToNs(armGetSystemTick()) / 1000000ull)); }

/* ------------------------------------------------------------ video
 * The logo and the briefings are an activity (MyVideoView) started with an
 * Intent whose "video_name" extra is the video's path (the logo:
 * <SD folder>/videos/LOGO_Gameloft_640x960.mp4; on a phone a briefing is read
 * out of the main OBB through a content provider, by its file name). The
 * port plays it after the frame that asked (mc4_main.c, mc4_video.c), from the
 * copy the setup made in <SD folder>/videos/ (mc4_setup.c). */
static char g_video_name[256];

static JNI_H_DECL(h_put_extra) {
  const char *k = a ? jni_utf(a[0].l) : "";
  if (!strcmp(k, "video_name"))
    snprintf(g_video_name, sizeof g_video_name, "%s", jni_utf(a[1].l));
  return jv_l(self ? jni_retain(self) : jni_singleton("android/content/Intent"));
}

static JNI_H_DECL(h_start_activity) {
  debugPrintf("[mc4] startActivity: video \"%s\"\n", g_video_name);
  mc4_video_requested(g_video_name);
  g_video_name[0] = 0;
  return jv_none();
}

/* ------------------------------------------------------------ AudioTrack
 * AudioTrack(streamType, sampleRate, channelConfig, audioFormat, bufferSize,
 * mode): the game's mixer writes 16-bit PCM to it from its own thread; the
 * write blocks on the output (mc4_audio.c), which paces that thread as
 * Android's would. */
static JNI_H_DECL(h_audiotrack_init) {
  int rate = a ? a[1].i : 44100;
  int chcfg = a ? a[2].i : 12;
  int fmt = a ? a[3].i : 2;
  if (self) {
    self->v[0] = rate;
    self->v[1] = chcfg == 4 ? 1 : 2; /* CHANNEL_OUT_MONO 4, STEREO 12 */
    self->v[2] = fmt;
  }
  debugPrintf("[mc4] AudioTrack(%d Hz, channels %d, format %d)\n", rate, chcfg, fmt);
  return jv_none();
}

static JNI_H_DECL(h_audiotrack_write) {
  if (!a)
    return jv_i(0);
  JObj *arr = a[0].l;
  jint off = a[1].i, len = a[2].i;
  if (!arr || arr->kind != JK_ARRAY || off < 0 || len <= 0 || off + len > arr->a.len)
    return jv_i(0);
  int rate = self ? (int)self->v[0] : 44100;
  int chans = self && self->v[1] ? (int)self->v[1] : 2;
  if (arr->a.elem == 'S') {
    const int16_t *s = (const int16_t *)arr->a.data + off;
    mc4_audio_write(s, len / chans, chans, rate);
  } else if (arr->a.elem == 'B') {
    const int16_t *s = (const int16_t *)((const uint8_t *)arr->a.data + off);
    mc4_audio_write(s, len / 2 / chans, chans, rate);
  }
  return jv_i(len);
}

static JNI_H_DECL(h_audio_min_buffer) { return jv_i(4096); }
static JNI_H_DECL(h_audio_rate) { return jv_i(44100); }
static JNI_H_DECL(h_audio_play) { mc4_audio_pause(0); return jv_none(); }
static JNI_H_DECL(h_audio_pause) { mc4_audio_pause(1); return jv_none(); }

/* ------------------------------------------------------------ the table
 * name, the class it must be on (NULL: any), the handler. A method not here
 * falls through to the runtime: its default answer, logged once as
 * unhandled (debug.log "[jni] unhandled ..."), which is how new ones show. */
typedef struct {
  const char *name;
  const char *cls;
  JMethodFn fn;
} Mc4Method;

#define AT "android/media/AudioTrack"

static const Mc4Method k_methods[] = {
    /* folders and the expansion files (MC4.java, SUtils) */
    {"getSDFolder", NULL, h_sd_folder},
    {"getSaveFolder", NULL, h_save_folder},
    {"getMainFileLocation", NULL, h_main_obb},
    {"getPatchFileLocation", NULL, h_patch_obb},
    {"getPackage", NULL, h_package},
    {"GetApkPath", NULL, h_apk_path},
    {"getApkPath", NULL, h_apk_path},
    {"GetDeviceFirmware", NULL, h_firmware},
    {"getDeviceFirmware", NULL, h_firmware},
    {"getPreferenceString", NULL, h_empty_str},
    {"UpdatePreferences", NULL, jni_h_void},
    {"UpdatePreferences2", NULL, jni_h_void},
    {"nativeSetPreference", NULL, jni_h_void},
    {"nativeGetPreference", NULL, h_first_arg},
    {"startGame", NULL, jni_h_void},

    /* the device: an Xperia Play, slid open (keys + touch pads) */
    {"isXperiaPlay", NULL, jni_h_true},
    {"isDeviceOpened", NULL, jni_h_true},
    {"isMogaConnected", NULL, h_pad_mode},
    {"mustSwapButtons", NULL, jni_h_true}, /* MC4.java: always */
    {"isKindleDevice", NULL, jni_h_false},
    {"isKindleOne", NULL, jni_h_false},
    {"isAmazonWhiteList", NULL, jni_h_false},
    {"showKindleBar", NULL, jni_h_void},
    {"isNexus7v2", NULL, jni_h_false},
    {"isHTCDevice", NULL, jni_h_false},
    {"isHoneycombDevice", NULL, jni_h_false},
    {"isSamsung_P7100", NULL, jni_h_false},
    {"hasGyroscope", NULL, jni_h_false},
    {"getGyroMode", NULL, jni_h_zero},
    {"getGyroSensibility", NULL, jni_h_zero},
    {"allowCheats", NULL, jni_h_false},
    {"getGeneration", NULL, h_generation},
    {"getProcessorNum", NULL, h_cores},
    {"getArchitecture", NULL, h_cpu_impl},
    {"getMicroArchitecture", NULL, h_cpu_part},
    {"getBuildVersion", NULL, h_sdk},
    {"getMasterVolume", NULL, h_volume},
    {"setMasterVolume", NULL, jni_h_void},
    {"getCountry", NULL, h_country},
    {"getLocaleCountry", NULL, h_country},
    {"getLocaleLanguage", NULL, h_language},
    {"getDeviceLanguage", NULL, h_language},
    {"GetManufacturer", NULL, h_manufacturer},
    {"getPhoneManufacturer", NULL, h_manufacturer},
    {"getManufacturerModel", NULL, h_model},
    {"getPhoneModel", NULL, h_model},
    {"GetDeviceName", NULL, h_model},
    {"getDeviceName", NULL, h_model},
    {"getResource", NULL, h_get_resource},
    {"getGLUID", NULL, h_gluid},
    {"getGLUIDX86", NULL, h_gluid_x86},
    {"getAndroidId", NULL, h_android_id},
    {"getSerial", NULL, h_android_id},
    {"getSerialNo", NULL, h_android_id},
    {"hasConnectivity", NULL, jni_h_false},
    {"initCheckConnectionType", NULL, jni_h_zero},
    {"setThreadPriority", NULL, jni_h_void},
    {"getIMEI", NULL, h_imei},
    {"getSystemUpTimeMillis", NULL, h_uptime},
    {"setOrientationEnabled", NULL, jni_h_void},
    {"SetPreserveContext", NULL, jni_h_void},
    {"setSkipPurgeBuffers", NULL, jni_h_void},

    /* the view and GL (the context is mc4_gl.c's, made before the game) */
    {"setViewSettings", NULL, jni_h_void},
    {"setCurrentContext", NULL, jni_h_false},
    {"enableAccelerometer", NULL, jni_h_void},
    {"enableGyroscope", NULL, jni_h_void},
    {"getContext", NULL, h_context},
    {"getActivityContext", NULL, h_context},
    {"getSystemService", NULL, h_object},
    {"getContentResolver", NULL, h_object},
    {"showloading", NULL, jni_h_void},
    {"hideloading", NULL, jni_h_void},
    {"showloadingGameplay", NULL, jni_h_void},
    {"hideloadingGameplay", NULL, jni_h_void},
    {"moveToBackground", NULL, jni_h_void},
    {"showKeyboard", NULL, jni_h_void},
    {"ShowKeyboard", NULL, jni_h_void},
    {"invalidBackKey", NULL, jni_h_void},
    {"ShowAlert", NULL, jni_h_void},
    {"Exit", NULL, jni_h_void},
    {"exitGame", NULL, h_exit_game},

    /* online, social, store, tracking: no network (the servers are gone) */
    {"IsWifiEnabled", NULL, jni_h_zero},
    {"IsWifiOr3GEnabled", NULL, jni_h_zero},
    {"getUserAgent", NULL, h_empty_str},
    {"getUsername", NULL, h_empty_str},
    {"getPassword", NULL, h_empty_str},
    {"saveUsername", NULL, jni_h_void},
    {"savePassword", NULL, jni_h_void},
    {"setMPLogout", NULL, jni_h_void},
    {"GetGooglePlusUsernameByID", NULL, h_empty_str},
    {"isHideGooglePlus", NULL, jni_h_true},
    {"showParentButton", NULL, jni_h_void},
    {"hideParentButton", NULL, jni_h_void},
    {"showGPGSButton", NULL, jni_h_void},
    {"hideGPGSButton", NULL, jni_h_void},
    {"InitSocials", NULL, jni_h_void},
    {"InitGameAPI", NULL, jni_h_void},
    {"GameLaunchTracking", NULL, jni_h_void},
    {"generateHit", NULL, jni_h_void},
    {"generateHitCustomerCare", NULL, jni_h_void},
    {"fillUrlParams", NULL, h_empty_str},
    {"setBaseUrl", NULL, jni_h_void},
    {"startWelcomeScreen", NULL, jni_h_void},
    {"downloadWelcomeScreen", NULL, jni_h_void},
    {"startWelcomeBack", NULL, jni_h_void},
    {"CheckOffensiveWords", NULL, jni_h_false},
    {"IGPLaunch", NULL, jni_h_void},
    {"BrowserLaunch", NULL, jni_h_void},
    {"showForum", NULL, jni_h_void},
    {"ShowPlusOneButton", NULL, jni_h_void},

    /* in-level videos (a VideoView over the game): not played -- "could not" */
    {"playVideo", NULL, jni_h_false},
    {"stopVideo", NULL, jni_h_void},
    {"inGameVideoSetSkipEnabled", NULL, jni_h_void},

    /* Intent / Bundle (the logo and briefing activity) */
    {"putExtra", NULL, h_put_extra},
    {"setClassName", NULL, jni_h_self},
    {"setFlags", NULL, jni_h_self},
    {"startActivity", NULL, h_start_activity},
    {"startActivityForResult", NULL, h_start_activity},
    {"putString", NULL, jni_h_void},
    {"getString", NULL, h_empty_str},
    {"getByteArray", NULL, jni_h_null},
    {"putByteArray", NULL, jni_h_void},
    {"getInt", NULL, jni_h_zero},
    {"putInt", NULL, jni_h_void},
    {"getLong", NULL, jni_h_zero},
    {"putLong", NULL, jni_h_void},
    {"getBoolean", NULL, jni_h_false},
    {"putBoolean", NULL, jni_h_void},
    {"containsKey", NULL, jni_h_false},
    {"clear", NULL, jni_h_void},

    /* AudioTrack (vox's Android driver) */
    {"<init>", AT, h_audiotrack_init},
    {"getMinBufferSize", AT, h_audio_min_buffer},
    {"getNativeOutputSampleRate", AT, h_audio_rate},
    {"write", AT, h_audiotrack_write},
    {"play", AT, h_audio_play},
    {"pause", AT, h_audio_pause},
    {"stop", AT, h_audio_pause},
    {"release", AT, jni_h_void},
    {"flush", AT, jni_h_void},
    {"getPlayState", AT, jni_h_zero},
};

#define NMETHODS (sizeof k_methods / sizeof k_methods[0])

static int is_class(const JMethod *m, const JObj *self, const char *cls) {
  if (!strcmp(m->cls->name, cls))
    return 1;
  return self && self->cls && jni_is(self, cls);
}

int port_jni_invoke(JObj *self, JMethod *m, const jvalue *args, jvalue *out) {
  for (unsigned i = 0; i < NMETHODS; i++) {
    const Mc4Method *e = &k_methods[i];
    if (strcmp(e->name, m->name))
      continue;
    if (e->cls && !is_class(m, self, e->cls))
      continue;
    if (g_jni_log)
      debugPrintf("[jni] %s.%s%s (mc4)\n", m->cls->name, m->name, m->sig);
    *out = e->fn(self, args, m);
    return 1;
  }
  /* An unhandled String getter: the runtime would answer a fresh "" that the
   * engine's DeleteLocalRef frees under it (see ret_str); answer the
   * immortal one, and log it once, as the runtime would. */
  const char *r = strchr(m->sig, ')');
  if (r && !strcmp(r + 1, "Ljava/lang/String;")) {
    if (!m->warned) {
      ((JMethod *)m)->warned = 1;
      debugPrintf("[jni] unhandled %s%s.%s%s: \"\"\n", m->is_static ? "static " : "", m->cls->name,
                  m->name, m->sig);
    }
    *out = ret_str("");
    return 1;
  }
  return 0;
}

/* ------------------------------------------------------------ the tables
 * Everything is answered above; these stay as the runtime expects them. */
const JMethodDef jni_method_defs[] = {{NULL, NULL, NULL, NULL}};

/* Build.VERSION.SDK_INT 19 (4.4, as getBuildVersion); Context.WINDOW_SERVICE. */
const JFieldDef jni_field_defs[] = {
    {"android/os/Build$VERSION", "SDK_INT", NULL, 19, NULL},
    {"android/content/Context", "WINDOW_SERVICE", NULL, 0, "window"},
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {{NULL, NULL}};
const char *const jni_missing_classes[] = {NULL};
