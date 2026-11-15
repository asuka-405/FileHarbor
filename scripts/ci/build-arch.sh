#!/bin/sh
# Build the Arch package inside an archlinux container (run as root there). Usage:
#   scripts/ci/build-arch.sh <version> <path-to-release-tarball> <output-dir>
# Writes the package to <output-dir>. Used by the release workflow; also fine for a local test with
#   docker run --rm -v "$PWD:/src" -w /src archlinux:base-devel scripts/ci/build-arch.sh 0.1.1 dist/fileharbor-0.1.1.tar.gz out
set -e
VERSION="$1"; TARBALL="$(realpath "$2")"; OUT="$(mkdir -p "$3" && realpath "$3")"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
pacman -Syu --noconfirm --needed base-devel git namcap >/dev/null
id builder >/dev/null 2>&1 || useradd -m builder
echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder
W="$(mktemp -d)"
NAME="$(basename "$TARBALL")"
cp "$TARBALL" "$W/$NAME"
SUM="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
"$ROOT/scripts/ci/make-pkgbuild.sh" "$VERSION" "$SUM" "$NAME" > "$W/PKGBUILD"
chown -R builder "$W"
cd "$W"
su builder -c "PKGDEST='$W' makepkg --syncdeps --noconfirm --clean"
namcap PKGBUILD || true
# the -debug package is not for users: leave it out of the release
for f in "$W"/*.pkg.tar.zst; do case "$f" in *-debug-*) ;; *) cp "$f" "$OUT/" ;; esac; done
ls -l "$OUT"
