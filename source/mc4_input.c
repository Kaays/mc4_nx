/* mc4_input.c -- controllers and the touch screen, into the engine.
 *
 * The game is told a gamepad is connected (mc4_jni.c, mc4_main.c: MC4.java's
 * "Generic X-Box" mode), its own controller path: buttons arrive as Android
 * key codes through GL2JNILib.keyboardEvent(key, down), the left stick as
 * nativeSetTouchPadDTLeft and the right one as nativeSetTouchPadDT, each
 * -1..1 (Hud::UpdateMovementControls / UpdateCameraControls read them).
 *
 * The layout follows the MC3 port (the same Gameloft key set), with what the
 * hardware tests of MC4 showed:
 *   ZR 103 fire          ZL 102 aim
 *   R  123 grenade       L3 sprint (nativeSetDoubleUp: on with a click, off
 *                           with another, the stick at rest, aiming or a menu)
 *   Y  99 reload         X / L 100 weapon switch
 *   B  20 crouch         A  23 action
 *   D-pad 19-22   + 108 START (pause)   - 109 SELECT
 * In menus (any Flash menu on top but the HUD): A selects (23), B goes back
 * (BACK reaches the game as MC4.nativeHandleKey(4) on release, as on a
 * phone), the D-pad moves. debug.log lists every key sent
 * ("[input] key 23 down").
 *
 * A key held by two buttons at once is pressed once and released when the
 * last one lets go; each button releases the key it pressed even if the game
 * left play meanwhile. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "jni.h"
#include "rt_pad.h"
#include "rt_window.h"
#include "so_util.h"
#include "util.h"

#include "mc4.h"

enum {
  AK_NONE = 0,
  AK_BACK = 4,
  AK_DPAD_UP = 19,
  AK_DPAD_DOWN = 20,
  AK_DPAD_LEFT = 21,
  AK_DPAD_RIGHT = 22,
  AK_DPAD_CENTER = 23,
  AK_BUTTON_X = 99,
  AK_BUTTON_Y = 100,
  AK_BUTTON_L1 = 102,
  AK_BUTTON_R1 = 103,
  AK_BUTTON_L2 = 104,
  AK_BUTTON_R2 = 105,
  AK_BUTTON_THUMBR = 107,
  AK_BUTTON_START = 108,
  AK_BUTTON_SELECT = 109,
  AK_MOVE_END = 123,
  AK_MAX = 128
};

typedef struct {
  u64 button;
  int key_game, key_menu; /* in play / in menus */
} Map;

static const Map k_map[] = {
    {HidNpadButton_ZR, AK_BUTTON_R1, AK_BUTTON_R1},
    {HidNpadButton_ZL, AK_BUTTON_L1, AK_BUTTON_L1},
    {HidNpadButton_R, AK_MOVE_END, AK_NONE},
    {HidNpadButton_L, AK_BUTTON_Y, AK_NONE},
    {HidNpadButton_B, AK_DPAD_DOWN, AK_BACK},
    {HidNpadButton_A, AK_DPAD_CENTER, AK_DPAD_CENTER},
    {HidNpadButton_Y, AK_BUTTON_X, AK_BUTTON_X},
    {HidNpadButton_X, AK_BUTTON_Y, AK_BUTTON_Y},
    {HidNpadButton_Up, AK_DPAD_UP, AK_DPAD_UP},
    {HidNpadButton_Down, AK_DPAD_DOWN, AK_DPAD_DOWN},
    {HidNpadButton_Left, AK_DPAD_LEFT, AK_DPAD_LEFT},
    {HidNpadButton_Right, AK_DPAD_RIGHT, AK_DPAD_RIGHT},
    {HidNpadButton_Plus, AK_BUTTON_START, AK_BUTTON_START},
    {HidNpadButton_Minus, AK_BUTTON_SELECT, AK_BUTTON_SELECT},
};
#define NMAP (sizeof k_map / sizeof k_map[0])

typedef void (*touch_fn)(void *env, void *cls, int action, int x, int y, int id);
typedef void (*key_fn)(void *env, void *cls, int key, int down);
typedef void (*pad_fn)(void *env, void *cls, float x, float y, int pointer);
typedef void (*handle_key_fn)(void *env, void *obj, int key);

static touch_fn f_touch;
static key_fn f_key;
static pad_fn f_pad_left, f_pad_right;
static handle_key_fn f_handle_key;
static void *g_cls, *g_activity;

static PadState g_pad;
static int g_held_key[NMAP];   /* the key each button pressed, 0 = none */
static int g_key_refs[AK_MAX]; /* how many buttons hold each key */

#define DEADZONE 0.2f
#define PAD_POINTER (-1)

void mc4_input_init(void) {
  f_touch = (touch_fn)so_try_find_addr_rx(&g_mc4_mod, "Java_com_gameloft_glf_GL2JNILib_touchEvent");
  f_key = (key_fn)so_try_find_addr_rx(&g_mc4_mod, "Java_com_gameloft_glf_GL2JNILib_keyboardEvent");
  f_pad_left = (pad_fn)so_try_find_addr_rx(&g_mc4_mod, "Java_com_gameloft_glf_GL2JNILib_nativeSetTouchPadDTLeft");
  f_pad_right = (pad_fn)so_try_find_addr_rx(&g_mc4_mod, "Java_com_gameloft_glf_GL2JNILib_nativeSetTouchPadDT");
  f_handle_key = (handle_key_fn)so_try_find_addr_rx(&g_mc4_mod,
                                                    "Java_com_gameloft_android_ANMP_GloftM4HM_MC4_nativeHandleKey");
  g_cls = jni_class("com/gameloft/glf/GL2JNILib")->obj;
  g_activity = jni_singleton("com/gameloft/android/ANMP/GloftM4HM/MC4");
  rt_pad_setup(1, 1);
  padInitializeDefault(&g_pad);
  hidInitializeTouchScreen();
  debugPrintf("[input] touch %s, keys %s, pads %s/%s, back %s\n", f_touch ? "ok" : "MISSING",
              f_key ? "ok" : "MISSING", f_pad_left ? "ok" : "MISSING", f_pad_right ? "ok" : "MISSING",
              f_handle_key ? "ok" : "MISSING");
}

static void key(int k, int down) {
  if (k <= 0 || k >= AK_MAX)
    return;
  if (down) {
    if (g_key_refs[k]++ != 0)
      return;
    debugPrintf("[input] key %d down\n", k);
    /* BACK: MC4.java swallows the press and hands the release to
     * nativeHandleKey (onKeyDown 4 returns at once, onKeyUp 4 calls it) */
    if (k != AK_BACK && f_key)
      f_key(g_jni_env, g_cls, k, 1);
  } else if (g_key_refs[k] > 0) {
    if (--g_key_refs[k] != 0)
      return;
    debugPrintf("[input] key %d up\n", k);
    if (k == AK_BACK) {
      if (f_handle_key)
        f_handle_key(g_jni_env, g_activity, AK_BACK);
    } else if (f_key) {
      f_key(g_jni_env, g_cls, k, 0);
    }
  }
}

/* A radial deadzone: below it 0, above it rescaled to 0..1. */
static void deadzone(float *x, float *y) {
  float mag = sqrtf(*x * *x + *y * *y);
  if (mag < DEADZONE) {
    *x = *y = 0.0f;
    return;
  }
  float k = (mag > 1.0f ? 1.0f : (mag - DEADZONE) / (1.0f - DEADZONE)) / mag;
  *x *= k;
  *y *= k;
}

/* One stick: a value while it is off centre, then (0,0) twice. */
typedef struct {
  float last_x, last_y, last2_x, last2_y;
} Stick;

static void feed(Stick *s, pad_fn fn, float x, float y) {
  if (!fn)
    return;
  int now = x != 0.0f || y != 0.0f;
  int was = s->last_x != 0.0f || s->last_y != 0.0f;
  int was2 = s->last2_x != 0.0f || s->last2_y != 0.0f;
  if (now || was)
    fn(g_jni_env, g_cls, x, y, PAD_POINTER);
  else if (was2)
    fn(g_jni_env, g_cls, 0.0f, 0.0f, PAD_POINTER);
  s->last2_x = s->last_x;
  s->last2_y = s->last_y;
  s->last_x = x;
  s->last_y = y;
}

static Stick g_left, g_right;

static void poll_touch(void) {
  static HidTouchScreenState old;
  HidTouchScreenState st;
  if (!f_touch || !hidGetTouchScreenStates(&st, 1))
    return;
  if (!g_mc4_cfg.touch)
    st.count = 0;
  int w = 1280, h = 720;
  dcr_window_size(&w, &h);
  /* the panel is 1280x720 in either mode; the game's surface is the window */
  for (int i = 0; i < st.count; i++) {
    int was = 0;
    for (int j = 0; j < old.count; j++)
      if (old.touches[j].finger_id == st.touches[i].finger_id)
        was = 1;
    int x = (int)st.touches[i].x * w / 1280, y = (int)st.touches[i].y * h / 720;
    /* GLF: 1 down, 2 move, 0 up */
    f_touch(g_jni_env, g_cls, was ? 2 : 1, x, y, (int)st.touches[i].finger_id);
  }
  for (int j = 0; j < old.count; j++) {
    int still = 0;
    for (int i = 0; i < st.count; i++)
      if (old.touches[j].finger_id == st.touches[i].finger_id)
        still = 1;
    if (!still) {
      int x = (int)old.touches[j].x * w / 1280, y = (int)old.touches[j].y * h / 720;
      f_touch(g_jni_env, g_cls, 0, x, y, (int)old.touches[j].finger_id);
    }
  }
  old = st;
}

/* Sprint: an L3 click turns it on (nativeSetDoubleUp(1), held for the
 * player), another click, the left stick at rest for 8 frames, aiming or a
 * menu turn it off. */
static int g_sprint;
static int g_sprint_idle;

static void sprint(int game_keys, u64 pressed, u64 held, float lx, float ly) {
  int want = g_sprint;
  if (game_keys && (pressed & HidNpadButton_StickL))
    want = !g_sprint;
  g_sprint_idle = (lx == 0.0f && ly == 0.0f) ? g_sprint_idle + 1 : 0;
  if (!game_keys || g_sprint_idle > 8 || (held & HidNpadButton_ZL))
    want = 0;
  if (want != g_sprint) {
    g_sprint = want;
    debugPrintf("[input] sprint %s\n", want ? "on" : "off");
    mc4_set_double_up(want);
  }
}

/* When the menu on top cannot be read (no manager yet, or none found): + in
 * play is taken to open the pause menu and + again, or playing again, to
 * close it; everything else is play. */
static int g_pause_menu;

static void track_pause(u64 pressed, u64 held, float lx, float ly) {
  if (pressed & HidNpadButton_Plus) {
    g_pause_menu = !g_pause_menu;
    debugPrintf("[input] pause menu %s\n", g_pause_menu ? "open" : "closed (+)");
    return;
  }
  if (g_pause_menu && ((held & (HidNpadButton_ZR | HidNpadButton_ZL)) || lx * lx + ly * ly > 0.36f)) {
    g_pause_menu = 0;
    debugPrintf("[input] pause menu closed (playing again)\n");
  }
}

void mc4_input_poll(int have_menus) {
  padUpdate(&g_pad);
  float sticks[4] = {0};
  u64 held = rt_pad_read(&g_pad, sticks);
  static u64 prev_held;
  u64 pressed = held & ~prev_held;
  prev_held = held;

  /* Which mapping: the game's own top menu -- the HUD is play, any other
   * menu (pause, death, debriefing, a skippable cutscene...) takes A/B. */
  const char *menu = mc4_menu_name();
  static char last_menu[48];
  if (menu && strncmp(menu, last_menu, sizeof last_menu - 1)) {
    snprintf(last_menu, sizeof last_menu, "%s", menu);
    debugPrintf("[input] menu %s\n", last_menu);
  }
  int game_keys;
  if (have_menus && menu) {
    g_pause_menu = 0;
    game_keys = strstr(menu, MC4_HUD_MENU) != NULL;
  } else {
    track_pause(pressed, held, sticks[0], sticks[1]);
    game_keys = !g_pause_menu;
  }

  for (unsigned i = 0; i < NMAP; i++) {
    int down = (held & k_map[i].button) != 0;
    if (down && !g_held_key[i]) {
      int k = game_keys ? k_map[i].key_game : k_map[i].key_menu;
      if (k) {
        g_held_key[i] = k;
        key(k, 1);
      }
    } else if (!down && g_held_key[i]) {
      key(g_held_key[i], 0);
      g_held_key[i] = 0;
    }
  }

  /* left: move, up positive (as MC4.java's gamepad path: -AXIS_Y) */
  float lx = sticks[0], ly = sticks[1];
  deadzone(&lx, &ly);
  sprint(game_keys, pressed, held, lx, ly);
  feed(&g_left, f_pad_left, lx, ly);

  /* right: look, down positive (AXIS_RZ as it comes) */
  float rx = sticks[2], ry = sticks[3];
  deadzone(&rx, &ry);
  float sp = g_mc4_cfg.look_speed;
  feed(&g_right, f_pad_right, rx * sp, (g_mc4_cfg.invert_y ? ry : -ry) * sp);

  poll_touch();
}
