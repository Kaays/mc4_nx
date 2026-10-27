#!/bin/sh
# make_release.sh -- collect a built mc4_nx.nro for a GitHub release.
#
#   tools/make_release.sh            dist/mc4_nx.nro + dist/SHA256SUMS
#   tools/make_release.sh --publish  the same, then `gh release create` for
#                                    the tag v<version> (needs the GitHub CLI,
#                                    signed in, and the tag pushed)
#
# The version is launcher/Makefile's APP_VERSION; the release text is
# docs/releases/v<version>.md. Build first: ./build.sh && ./launcher/build.sh.
# The NRO carries no game files: players add their own APK and OBBs. MIT.
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(sed -n 's/^APP_VERSION *:= *//p' "$HERE/launcher/Makefile")"
NRO="$HERE/launcher/mc4_nx.nro"
NOTES="$HERE/docs/releases/v$VER.md"

[ -f "$NRO" ] || { echo "make_release.sh: no $NRO -- run ./build.sh && ./launcher/build.sh" >&2; exit 1; }
[ -f "$NOTES" ] || { echo "make_release.sh: no release text $NOTES" >&2; exit 1; }
if [ "$NRO" -ot "$HERE/build.sh" ] || find "$HERE/source" -newer "$NRO" -name '*.[ch]' | grep -q .; then
  echo "make_release.sh: source files are newer than the NRO -- rebuild first" >&2
  exit 1
fi

mkdir -p "$HERE/dist"
cp "$NRO" "$HERE/dist/mc4_nx.nro"
(cd "$HERE/dist" && sha256sum mc4_nx.nro > SHA256SUMS)
echo "v$VER: dist/mc4_nx.nro ($(wc -c < "$HERE/dist/mc4_nx.nro") bytes)"
cat "$HERE/dist/SHA256SUMS"

if [ "${1:-}" = "--publish" ]; then
  command -v gh >/dev/null 2>&1 || { echo "make_release.sh: the GitHub CLI (gh) is needed for --publish" >&2; exit 1; }
  git -C "$HERE" rev-parse -q --verify "refs/tags/v$VER" >/dev/null ||
    { echo "make_release.sh: no tag v$VER (git tag -a v$VER && git push --tags)" >&2; exit 1; }
  cd "$HERE" # gh finds the repository from origin
  gh release create "v$VER" dist/mc4_nx.nro dist/SHA256SUMS \
    --title "mc4_nx v$VER" --notes-file "$NOTES"
fi
