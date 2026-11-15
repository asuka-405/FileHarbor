#!/bin/sh
# Print the PKGBUILD for one release. Usage: scripts/ci/make-pkgbuild.sh <version> <sha256> [local-tarball-name]
# Without the third argument the source is the GitHub release asset (what the AUR needs); with it the source
# is a file next to the PKGBUILD (what the CI test build uses).
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$1"; SUM="$2"; LOCAL="$3"
[ -n "$VERSION" ] && [ -n "$SUM" ] || { echo "usage: $0 <version> <sha256> [local-tarball-name]" >&2; exit 2; }
sed -e "s|^pkgver=.*|pkgver=$VERSION|" \
    -e "s|^pkgrel=.*|pkgrel=1|" \
    -e "s|^sha256sums=.*|sha256sums=('$SUM')|" \
    ${LOCAL:+-e "s|^source=.*|source=(\"$LOCAL\")|"} \
    "$ROOT/packaging/aur/PKGBUILD"
