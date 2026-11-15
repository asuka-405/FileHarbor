#!/bin/sh
# Gate for production releases. Usage: scripts/ci/check-release.sh <tag>
# Passes only for a plain version tag (v1.2.3). Alpha, beta, rc and any other suffix are refused, as is a tag
# that disagrees with VERSION or has no CHANGELOG section. Exit 0 = production release, go.
# Exit 10 = not a production tag: skip quietly. Exit 1 = production tag with a mistake: fail loudly.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TAG="$1"
[ -n "$TAG" ] || { echo "usage: $0 <tag>" >&2; exit 2; }
if ! printf '%s' "$TAG" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "$TAG is not a production version (vX.Y.Z); pre-releases such as -alpha, -beta or -rc are not built."
    exit 10
fi
VERSION="$(cat "$ROOT/VERSION")"
if [ "v$VERSION" != "$TAG" ]; then
    echo "Tag $TAG does not match VERSION ($VERSION). Fix VERSION, move the tag, publish again." >&2
    exit 1
fi
if ! grep -q "^## $VERSION\$" "$ROOT/CHANGELOG.md"; then
    echo "CHANGELOG.md has no section '## $VERSION'." >&2
    exit 1
fi
echo "$TAG is a production release."
