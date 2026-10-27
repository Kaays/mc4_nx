#!/bin/sh
# build_ffmpeg.sh -- a small FFmpeg for the briefing videos, built for AArch32
# (libnx32) in the same toolchain image as the port, installed into
# portlibs32/ next to mesa32's files. Only what the videos need: the MP4
# demuxer, the H.264, MPEG-4 and AAC decoders; no programs, no asm, no
# threads, nothing autodetected.
#
#   tools/build_ffmpeg.sh          (from the port folder; once)
#   ./build.sh clean && ./build.sh (the port then links it: MC4_VIDEO=1)
#
# libnx32 is found like ./build.sh finds it (DCR_LIBNX32, else next to the
# port). The source is cloned into build-ffmpeg/ (not part of the repository).
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${DCR_TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos:latest}"
FFMPEG_TAG="${FFMPEG_TAG:-n6.1.2}"

LIBNX32="${DCR_LIBNX32:-}"
if [ -z "$LIBNX32" ]; then
  for c in "$HERE/deps/libnx32/prefix" "$HERE/../libnx32/prefix" "$HERE/../../libnx32/prefix" "$HOME/libnx32/prefix"; do
    if [ -f "$c/lib/libnx.a" ]; then LIBNX32="$c"; break; fi
  done
fi
if [ -z "$LIBNX32" ] || [ ! -f "$LIBNX32/lib/libnx.a" ]; then
  echo "build_ffmpeg.sh: libnx32 not found; set DCR_LIBNX32 to its prefix/ folder" >&2
  exit 1
fi
LIBNX32="$(cd "$LIBNX32" && pwd)"
echo "build_ffmpeg.sh: libnx32 from $LIBNX32, FFmpeg $FFMPEG_TAG"

if [ ! -d "$HERE/build-ffmpeg/src/.git" ]; then
  mkdir -p "$HERE/build-ffmpeg"
  git clone --depth 1 --branch "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg.git "$HERE/build-ffmpeg/src"
fi

NXD=/opt/devkitpro/libnx32
exec docker run --rm --platform linux/amd64 -v "$HERE:/work" -w /work \
  -v "$LIBNX32/include/switch:$NXD/include/switch:ro" \
  -v "$LIBNX32/include/switch.h:$NXD/include/switch.h:ro" \
  -v "$LIBNX32/lib/libnx.a:$NXD/lib/libnx.a:ro" \
  -v "$LIBNX32/lib/libnxd.a:$NXD/lib/libnxd.a:ro" \
  "$IMAGE" bash -lc 'exec sh /work/tools/build_ffmpeg_in.sh'
