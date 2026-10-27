/* mc4_asset.c -- the NDK asset manager (AAssetManager_open and friends),
 * which libModernCombat4.so imports and the runtime does not implement.
 *
 * On a phone these read the APK's assets/ in place. Here they read the copy
 * the setup makes in <game folder>/assets/ (mc4_setup.c): an AAsset is an open
 * FILE, an AAssetDir a directory listing. The manager pointer the game passes
 * is ignored (any non-NULL one works). MIT.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "dcr_setup.h"
#include "util.h"

typedef struct {
  FILE *f;
  long size;
} Asset;

typedef struct {
  DIR *d;
} AssetDir;

static void asset_path(char *out, size_t cap, const char *name) {
  while (name && *name == '/')
    name++;
  char rel[512];
  snprintf(rel, sizeof rel, "assets/%s", name ? name : "");
  rt_root_path(out, cap, rel);
}

void *b_AAssetManager_open(void *mgr, const char *filename, int mode) {
  (void)mgr, (void)mode;
  char path[600];
  asset_path(path, sizeof path, filename);
  FILE *f = fopen(path, "rb");
  if (!f) {
    debugPrintf("[asset] %s: not found\n", filename ? filename : "(null)");
    return NULL;
  }
  Asset *a = calloc(1, sizeof *a);
  if (!a) {
    fclose(f);
    return NULL;
  }
  a->f = f;
  fseek(f, 0, SEEK_END);
  a->size = ftell(f);
  fseek(f, 0, SEEK_SET);
  return a;
}

int b_AAsset_read(void *asset, void *buf, size_t count) {
  Asset *a = asset;
  if (!a || !a->f)
    return -1;
  return (int)fread(buf, 1, count, a->f);
}

/* off_t on bionic armeabi-v7a is 32 bits (AAsset_seek, not AAsset_seek64) */
int b_AAsset_seek(void *asset, int offset, int whence) {
  Asset *a = asset;
  if (!a || !a->f || fseek(a->f, offset, whence) != 0)
    return -1;
  return (int)ftell(a->f);
}

int b_AAsset_getLength(void *asset) {
  Asset *a = asset;
  return a ? (int)a->size : 0;
}

void b_AAsset_close(void *asset) {
  Asset *a = asset;
  if (!a)
    return;
  if (a->f)
    fclose(a->f);
  free(a);
}

void *b_AAssetManager_openDir(void *mgr, const char *dirname) {
  (void)mgr;
  char path[600];
  asset_path(path, sizeof path, dirname);
  AssetDir *ad = calloc(1, sizeof *ad);
  if (ad)
    ad->d = opendir(path); /* a missing folder lists nothing, as on Android */
  return ad;
}

/* Files only, as Android's: the name is valid until the next call. */
const char *b_AAssetDir_getNextFileName(void *dir) {
  AssetDir *ad = dir;
  if (!ad || !ad->d)
    return NULL;
  struct dirent *e;
  while ((e = readdir(ad->d)) != NULL) {
    if (e->d_name[0] == '.')
      continue;
    if (e->d_type == DT_DIR)
      continue;
    return e->d_name;
  }
  return NULL;
}

void b_AAssetDir_close(void *dir) {
  AssetDir *ad = dir;
  if (!ad)
    return;
  if (ad->d)
    closedir(ad->d);
  free(ad);
}
