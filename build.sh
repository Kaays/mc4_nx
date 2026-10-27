#!/bin/sh
# Build mc4_nx.nsp in the AArch32 toolchain container (see runtime/tools/docker_build.sh).
# First, once: tools/setup_deps.sh (libnx32, Mesa, FFmpeg).
#   ./build.sh            mc4_nx.nsp + mc4_nx.build
#   ./build.sh clean
# Then: launcher/build.sh  -> launcher/mc4_nx.nro (carries the NSP)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
# libnx32 built by tools/setup_deps.sh (otherwise found next to this folder)
if [ -z "${DCR_LIBNX32:-}" ] && [ -f "$HERE/deps/libnx32/prefix/lib/libnx.a" ]; then
  DCR_LIBNX32="$HERE/deps/libnx32/prefix"
  export DCR_LIBNX32
fi
exec "$HERE/runtime/tools/docker_build.sh" "$@"
