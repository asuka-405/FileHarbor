#!/bin/sh
# Print the PKGBUILD of fileharbor-bin for one release. Usage: scripts/ci/make-pkgbuild-bin.sh <version> <sha256-of-the-pkg.tar.zst> [local-package-name]
# The dependency list is taken from packaging/aur/PKGBUILD so both packages stay in step.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$1"; SUM="$2"; LOCAL="$3"
[ -n "$VERSION" ] && [ -n "$SUM" ] || { echo "usage: $0 <version> <sha256> [local-package-name]" >&2; exit 2; }
# the lines between "depends=(" and its closing ")" of the source package, comments removed
DEPS="$(awk '/^depends=\(/{f=1;next} f&&/^\)/{exit} f{sub(/#.*/,""); print}' "$ROOT/packaging/aur/PKGBUILD" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//')"
[ -n "$DEPS" ] || { echo "could not read depends from packaging/aur/PKGBUILD" >&2; exit 1; }
sed -e "s|^pkgver=.*|pkgver=$VERSION|" \
    -e "s|^pkgrel=.*|pkgrel=1|" \
    -e "s|@DEPENDS@|$DEPS|" \
    -e "s|^sha256sums=.*|sha256sums=('$SUM')|" \
    ${LOCAL:+-e "s|^source=.*|source=(\"$LOCAL\")|" -e "s|^noextract=.*|noextract=(\"$LOCAL\")|" -e "s|^_pkg=.*|_pkg=\"$LOCAL\"|"} \
    "$ROOT/packaging/aur-bin/PKGBUILD"
