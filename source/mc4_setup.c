/* mc4_setup.c -- the port's own part of the first start, and file tracing.
 *
 * The videos: on a phone the logo is a file in the game's SD folder
 * (<SD folder>/videos/LOGO_Gameloft_640x960.mp4) and the briefings are played
 * out of the main OBB through a content provider. Here both are copied once
 * from obb/<main OBB> into <SD folder>/videos/ (the runtime keeps
 * /sdcard/gameloft/... in <game folder>/sdcard/gameloft/...), with their
 * subtitles: the mp4 and srt entries only, about 67 MB. Again only when the
 * OBB changes.
 *
 * The APK's assets/: what the game opens with the NDK asset manager
 * (mc4_asset.c), copied to <game folder>/assets/ while the APK is open.
 *
 * The expansion files themselves stay zips: the game reads them in place.
 * MIT.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "port_config.h"
#include "rt_boot.h"
#include "util.h"

#include "mc4.h"

#define STAMP_VIDEOS "videos"
#define FIRST_VIDEO  "LOGO_Gameloft_640x960.mp4"

static int exists(const char *p) {
  struct stat st;
  return stat(p, &st) == 0;
}

static int ends_with(const char *s, const char *suffix) {
  size_t n = strlen(s), k = strlen(suffix);
  return n >= k && !strcasecmp(s + n - k, suffix);
}

/* <SD folder>/videos[/name] as a path on the SD card */
static void videos_path(char *out, size_t cap, const char *name) {
  char android[DCR_PATH_MAX];
  snprintf(android, sizeof android, MC4_SD_FOLDER "/videos%s%s", name ? "/" : "", name ? name : "");
  const char *t = dcr_translate_path(android, out, cap);
  if (t != out)
    snprintf(out, cap, "%s", t);
}

void mc4_setup_obb(RtSetupCtx *ctx) {
  char main_obb[DCR_PATH_MAX], patch_obb[DCR_PATH_MAX];
  snprintf(main_obb, sizeof main_obb, "%s/obb/" MC4_OBB_MAIN, dcr_game_root());
  snprintf(patch_obb, sizeof patch_obb, "%s/obb/" MC4_OBB_PATCH, dcr_game_root());
  if (rt_file_size(main_obb) <= 0 || rt_file_size(patch_obb) <= 0)
    fatal_error("The game's OBB files are missing.\n\n%s", port_apk_help());

  unsigned long obb_size = (unsigned long)rt_file_size(main_obb), crc = 0, count = 0;
  char first[DCR_PATH_MAX];
  videos_path(first, sizeof first, FIRST_VIDEO);
  if (rt_setup_stamp_get(STAMP_VIDEOS, &crc, &count) && crc == obb_size && count > 0 && exists(first))
    return; /* done, and the OBB has not changed */

  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!mz_zip_reader_init_file(&zip, main_obb, 0))
    fatal_error("%s is not a readable zip.\n\n%s", main_obb, port_apk_help());
  unsigned n = mz_zip_reader_get_num_files(&zip);
  uint64_t total = 0, done = 0;
  for (unsigned i = 0; i < n; i++) {
    mz_zip_archive_file_stat st;
    if (mz_zip_reader_file_stat(&zip, i, &st) && !st.m_is_directory &&
        (ends_with(st.m_filename, ".mp4") || ends_with(st.m_filename, ".srt")))
      total += st.m_uncomp_size;
  }
  debugPrintf("[mc4] copying the videos: %llu MB from %s\n", (unsigned long long)(total >> 20), MC4_OBB_MAIN);
  unsigned made = 0;
  for (unsigned i = 0; i < n; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
      continue;
    const char *name = st.m_filename;
    if (!ends_with(name, ".mp4") && !ends_with(name, ".srt"))
      continue;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (!*base || strstr(base, ".."))
      continue;
    char dst[DCR_PATH_MAX];
    videos_path(dst, sizeof dst, base);
    int p0 = ctx->p0 + (int)((uint64_t)(ctx->p1 - ctx->p0) * done / (total ? total : 1));
    done += st.m_uncomp_size;
    int p1 = ctx->p0 + (int)((uint64_t)(ctx->p1 - ctx->p0) * done / (total ? total : 1));
    if (rt_setup_extract_entry(&zip, (int)i, dst, "Copying the videos", p0, p1) != 0)
      fatal_error("Could not copy %s from %s (SD card full?).", base, MC4_OBB_MAIN);
    made++;
  }
  mz_zip_reader_end(&zip);
  if (!exists(first))
    debugPrintf("[mc4] note: no " FIRST_VIDEO " in %s (unexpected layout)\n", MC4_OBB_MAIN);
  rt_setup_stamp_set(STAMP_VIDEOS, obb_size, made);
  char dir[DCR_PATH_MAX];
  videos_path(dir, sizeof dir, NULL);
  debugPrintf("[mc4] %u video files in %s/\n", made, dir);
}

/* The APK's assets/ -> <game folder>/assets/ (small: one json in 1.2.3e).
 * With the APK open; files already there with the same size stay. */
void mc4_setup_assets(RtSetupCtx *ctx) {
  if (!ctx->apk)
    return;
  unsigned n = mz_zip_reader_get_num_files(ctx->apk), made = 0;
  for (unsigned i = 0; i < n; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(ctx->apk, i, &st) || st.m_is_directory)
      continue;
    if (strncmp(st.m_filename, "assets/", 7) || strstr(st.m_filename, ".."))
      continue;
    char dst[DCR_PATH_MAX];
    rt_root_path(dst, sizeof dst, st.m_filename);
    if (rt_file_size(dst) == (long)st.m_uncomp_size)
      continue;
    if (rt_setup_extract_entry(ctx->apk, (int)i, dst, "Unpacking the game's assets", ctx->p0, ctx->p1) != 0)
      fatal_error("Could not unpack %s from the APK (SD card full?).", st.m_filename);
    made++;
  }
  if (made)
    debugPrintf("[mc4] %u asset file(s) unpacked to assets/\n", made);
}

/* ---------------------------------------------------------------- tracing
 * [debug] trace_files: every path the game opens or stats goes to debug.log
 * ([path], [io] fopen/open/stat lines of the runtime), so a missing file
 * names itself. Off by default (it costs frame time). */
int g_mc4_trace_files = 0;

int dcr_path_traced(const char *p) {
  if (!p)
    return 0;
  if (g_mc4_trace_files)
    return 1;
  return strstr(p, ".apk") != NULL;
}
