#!/bin/sh
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=mc4_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
