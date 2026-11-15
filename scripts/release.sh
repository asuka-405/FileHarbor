#!/bin/sh
# Prepare a release: run the tests, build the source tarball and its checksum.
#   scripts/release.sh            tarball for the version in VERSION, from the current commit
#   scripts/release.sh --tag      also create the annotated git tag v<VERSION>
# Output goes to ./dist. Nothing is pushed or uploaded.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="$(cat VERSION)"
NAME="fileharbor-$VERSION"

if [ -n "$(git status --porcelain)" ]; then
    echo "The working tree has uncommitted changes; commit them first." >&2
    exit 1
fi
grep -q "^## $VERSION\$" CHANGELOG.md || { echo "CHANGELOG.md has no section for $VERSION." >&2; exit 1; }

scripts/test.sh >/dev/null || { echo "Tests failed." >&2; exit 1; }

mkdir -p dist
git archive --format=tar.gz --prefix="$NAME/" -o "dist/$NAME.tar.gz" HEAD
( cd dist && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256" )
echo "Built dist/$NAME.tar.gz"
cat "dist/$NAME.tar.gz.sha256"

if [ "$1" = "--tag" ]; then
    git tag -a "v$VERSION" -m "FileHarbor $VERSION"
    echo "Tagged v$VERSION (push it with: git push origin v$VERSION)"
fi
