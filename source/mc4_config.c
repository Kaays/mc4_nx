/* mc4_config.c -- config.ini for Modern Combat 4 (the runtime's INI engine,
 * rt_cfg.c, reads and writes it; this is the table). MIT. */
#include "rt_cfg.h"
#include "util.h"

#include "mc4.h"

Mc4Config g_mc4_cfg = {.fps60 = 1, .look_speed = 1.0f, .invert_y = 0, .touch = 1, .sprint_toggle = 1, .generation = 9};

static int g_boost = 1;
static int g_fps_choice = 0; /* index into "60,30" */
static int g_quality = 0;     /* index into "high,medium,low" */

static const CfgOpt k_opts[] = {
    CFG_ROW_RESOLUTION("auto",
                       "Rendering resolution: auto (1080 if docked when the game starts, 720 in\n"
                       "# handheld), 1080 or 720."),
    {"display", "frame_rate", "60",
     "60 or 30 frames a second (30 runs cooler in handheld).",
     CFG_CHOICE, "60,30", &g_fps_choice, 0, 0, 0, 0},
    {"display", "graphics_quality", "high",
     "The game's own detail level, as it picks one per phone: high (what it\n"
     "# gives every recent phone), medium or low (what it gave the Xperia Play).",
     CFG_CHOICE, "high,medium,low", &g_quality, 0, 0, 0, 0},
    {"controls", "look_speed", "1.0",
     "Right stick camera speed (0.3 - 3.0).",
     CFG_FLOAT, NULL, &g_mc4_cfg.look_speed, 0.3f, 3.0f, 0, 0},
    {"controls", "invert_y", "false",
     "Invert the right stick's up/down.",
     CFG_BOOL, NULL, &g_mc4_cfg.invert_y, 0, 0, 0, 0},
    {"controls", "sprint_toggle", "true",
     "L3 starts sprinting with one click; it stops on another click, when the\n"
     "# left stick is let go, or on aim. false: hold L3.",
     CFG_BOOL, NULL, &g_mc4_cfg.sprint_toggle, 0, 0, 0, 0},
    {"controls", "touch_screen", "true",
     "The touch screen works in handheld (the game's on-screen buttons, menus).",
     CFG_BOOL, NULL, &g_mc4_cfg.touch, 0, 0, 0, 0},
    CFG_ROW_BOOST("CPU at 1785 MHz while the game loads (frames over 50 ms), normal otherwise.",
                  &g_boost),
    CFG_ROW_GL_SELFTEST(NULL),
    CFG_ROW_BOOT_LOG("Show the start-up log on screen.", NULL),
    {"debug", "trace_files", "false",
     "Write every file the game opens to debug.log (slow; for bug reports).",
     CFG_BOOL, NULL, &g_mc4_trace_files, 0, 0, 0, 0},
    CFG_ROW_LOG_JNI("Write every Java call the game makes to debug.log (slow; for bug reports).",
                    NULL),
};

static void apply(void) {
  g_mc4_cfg.fps60 = g_fps_choice == 0;
  static const int k_generation[] = {9, 1, 0};
  g_mc4_cfg.generation = k_generation[g_quality < 0 || g_quality > 2 ? 0 : g_quality];
  debugPrintf("[config] resolution %s, %d fps, quality %d, look speed %.2f%s, sprint %s, touch %s, boost %s\n",
              rt_config_get("display", "resolution"), g_mc4_cfg.fps60 ? 60 : 30, g_mc4_cfg.generation,
              (double)g_mc4_cfg.look_speed, g_mc4_cfg.invert_y ? " (inverted)" : "",
              g_mc4_cfg.sprint_toggle ? "toggle" : "hold", g_mc4_cfg.touch ? "on" : "off", g_boost ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .migrate = NULL,
    .nmigrate = 0,
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
