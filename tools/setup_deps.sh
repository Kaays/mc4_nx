#!/bin/sh
# setup_deps.sh -- fetch and build everything mc4_nx links that is not in this
# repository, once. Afterwards: ./build.sh && ./launcher/build.sh
#
#   tools/setup_deps.sh              libnx32, Mesa, FFmpeg
#   tools/setup_deps.sh --no-video   without FFmpeg (no logo/briefing videos)
#
# What it makes, and where:
#   1. libnx32  github.com/aks796/libnx32 at LIBNX32_COMMIT -> deps/libnx32/prefix
#      (the vita2hos AArch32 libnx with the 32-bit ports' fixes). Skipped when
#      one is already installed: DCR_LIBNX32, deps/, ../libnx32 or ~/libnx32.
#   2. Mesa     github.com/aks796/mesa32 at MESA32_COMMIT (Mesa 20.1 nouveau
#      + libdrm_nouveau), built against that libnx32 -> portlibs32/lib, include.
#      Skipped when portlibs32/lib/libEGL.a exists.
#   3. FFmpeg   n6.1.2, only the MP4 demuxer and the H.264, MPEG-4 and AAC
#      decoders (tools/build_ffmpeg.sh) -> portlibs32/. Skipped when
#      portlibs32/lib/libavcodec.a exists.
#
# Everything is compiled in the AArch32 toolchain container
# (ghcr.io/vita2hos/devcontainer/vita2hos, DCR_TOOLCHAIN_IMAGE for another), so
# the host needs only git and Docker. deps/, portlibs32/ and build-ffmpeg/ are
# ignored by git. To rebuild one step, delete its output and run this again.
# MIT.
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$HERE/deps"
IMAGE="${DCR_TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos:latest}"
LIBNX32_COMMIT=35c0b5aeb84a5456ea1419d98fd833631bda4279
MESA32_COMMIT=b02c9c5f9cf0b08c6e49d4c0ee52a2d8bfd7fd42
VIDEO=1
for a in "$@"; do
  case "$a" in
    --no-video) VIDEO=0 ;;
    -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
    *) echo "setup_deps.sh: unknown option $a" >&2; exit 2 ;;
  esac
done

say() { printf '\n\033[1;32m==> %s\033[0m\n' "$*"; }
for t in git docker; do
  command -v "$t" >/dev/null 2>&1 || { echo "setup_deps.sh: $t is needed" >&2; exit 1; }
done

# clone_at <url> <dir> <commit>: that commit only, checked
clone_at() {
  if [ -d "$2/.git" ]; then
    [ "$(git -C "$2" rev-parse HEAD)" = "$3" ] && return 0
    git -C "$2" fetch -q --depth 1 origin "$3"
  else
    rm -rf "$2.part" && mkdir -p "$2.part"
    git -C "$2.part" init -q
    git -C "$2.part" remote add origin "$1"
    git -C "$2.part" fetch -q --depth 1 origin "$3"
    mv "$2.part" "$2"
  fi
  git -C "$2" -c advice.detachedHead=false checkout -q "$3"
}

have_nx() { [ -f "$1/lib/libnx.a" ] && [ -f "$1/include/switch.h" ]; }

say "toolchain image ($IMAGE)"
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker pull "$IMAGE"

# 1. libnx32 ------------------------------------------------------------------
LIBNX32=""
if [ -n "${DCR_LIBNX32:-}" ]; then
  have_nx "$DCR_LIBNX32" || { echo "setup_deps.sh: DCR_LIBNX32=$DCR_LIBNX32 has no libnx" >&2; exit 1; }
  LIBNX32="$DCR_LIBNX32"
else
  for c in "$DEPS/libnx32/prefix" "$HERE/../libnx32/prefix" "$HOME/libnx32/prefix"; do
    if have_nx "$c"; then LIBNX32="$c"; break; fi
  done
fi
if [ -n "$LIBNX32" ]; then
  LIBNX32="$(cd "$LIBNX32" && pwd)"
  say "libnx32: using $LIBNX32"
else
  say "libnx32: building $(echo $LIBNX32_COMMIT | cut -c1-7) into deps/libnx32"
  clone_at https://github.com/aks796/libnx32.git "$DEPS/libnx32" "$LIBNX32_COMMIT"
  TOOLCHAIN_IMAGE="$IMAGE" "$DEPS/libnx32/build.sh"
  LIBNX32="$DEPS/libnx32/prefix"
  have_nx "$LIBNX32" || { echo "setup_deps.sh: libnx32 did not build" >&2; exit 1; }
fi

# 2. Mesa ---------------------------------------------------------------------
if [ -f "$HERE/portlibs32/lib/libEGL.a" ]; then
  say "Mesa: portlibs32/ already has it"
else
  say "Mesa: building mesa32 $(echo $MESA32_COMMIT | cut -c1-7) (the longest step)"
  clone_at https://github.com/aks796/mesa32.git "$DEPS/mesa32" "$MESA32_COMMIT"
  LIBNX32="$LIBNX32" TOOLCHAIN_IMAGE="$IMAGE" "$DEPS/mesa32/build.sh"
  [ -f "$DEPS/mesa32/prefix/lib/libEGL.a" ] || { echo "setup_deps.sh: Mesa did not build" >&2; exit 1; }
  mkdir -p "$HERE/portlibs32"
  cp -R "$DEPS/mesa32/prefix/lib" "$DEPS/mesa32/prefix/include" "$HERE/portlibs32/"
fi

# 3. FFmpeg -------------------------------------------------------------------
if [ "$VIDEO" = 0 ]; then
  say "FFmpeg: skipped (--no-video): the game is told each video finished at once"
elif [ -f "$HERE/portlibs32/lib/libavcodec.a" ]; then
  say "FFmpeg: portlibs32/ already has it"
else
  say "FFmpeg: building the video decoders"
  DCR_LIBNX32="$LIBNX32" DCR_TOOLCHAIN_IMAGE="$IMAGE" "$HERE/tools/build_ffmpeg.sh"
fi

say "done"
echo "libnx32:   $LIBNX32"
echo "portlibs32: $(cd "$HERE/portlibs32" && ls lib/*.a | tr '\n' ' ')"
echo
echo "Next:  ./build.sh && ./launcher/build.sh   ->  launcher/mc4_nx.nro"
