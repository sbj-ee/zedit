#!/usr/bin/env bash
# Version-drift check: the CMake project() version, the top CHANGELOG heading
# and the README "Version bumps" note must all agree.
#   scripts/check-version.sh               # every PR / push (ci.yml)
#   scripts/check-version.sh --tag vX.Y.Z  # tag builds (release.yml): also the
#                                          # tag must be v$VERSION and the top
#                                          # heading must be dated
set -euo pipefail
cd "$(dirname "$0")/.."

tag=""
if [ "${1:-}" = "--tag" ]; then
  tag="${2:?--tag needs a value}"
fi

fail=0
err() { echo "check-version: $*" >&2; fail=1; }

version=$(sed -nE 's/^project\(zedit VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n1)
[ -n "$version" ] || { echo "check-version: no project(zedit VERSION x.y.z) in CMakeLists.txt" >&2; exit 1; }

heading=$(grep -m1 -E '^## [0-9]' CHANGELOG.md || true)
head_ver=$(sed -nE 's/^## ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' <<<"$heading")
[ "$head_ver" = "$version" ] || err "top CHANGELOG heading '$heading' is not $version (CMakeLists.txt)"
grep -qE "^## $version( \(unreleased\)| — [0-9]{4}-[0-9]{2}-[0-9]{2})\$" <<<"$heading" \
  || err "top CHANGELOG heading '$heading' must be '## $version (unreleased)' or '## $version — YYYY-MM-DD'"

readme_ver=$(grep -m1 -oE '\(currently `[0-9]+\.[0-9]+\.[0-9]+`\)' README.md | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || true)
[ "$readme_ver" = "$version" ] || err "README 'Version bumps' says '${readme_ver:-none}', CMakeLists.txt says $version"

# Pinned release asset links go stale every release; the README points at
# releases/latest instead.
if grep -qE 'releases/download/v[0-9]' README.md; then
  err "README links a specific release asset (releases/download/v...); link releases/latest"
fi

if [ -n "$tag" ]; then
  [ "$tag" = "v$version" ] || err "tag $tag does not match CMakeLists.txt version v$version"
  grep -qE "^## $version — [0-9]{4}-[0-9]{2}-[0-9]{2}\$" <<<"$heading" \
    || err "top CHANGELOG heading '$heading' must be dated (## $version — YYYY-MM-DD), not unreleased"
fi

[ "$fail" -eq 0 ] || exit 1
echo "check-version: OK ($version${tag:+, tag $tag})"
