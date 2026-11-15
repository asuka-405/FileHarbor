#!/bin/sh
# Push one package's PKGBUILD and .SRCINFO to the AUR.
#   scripts/ci/publish-aur.sh fileharbor     <version> <sha256 of the source tarball>
#   scripts/ci/publish-aur.sh fileharbor-bin <version> <sha256 of the .pkg.tar.zst>
# Needs (environment): AUR_SSH_KEY  private key registered on your AUR account
# Optional: AUR_GIT_NAME, AUR_GIT_EMAIL (commit identity; default: Asuka <asuka@archdev.in>)
#           AUR_BASE_URL (default ssh://aur@aur.archlinux.org; a directory of local bare repos works for a dry run)
# Needs makepkg, git and ssh, so run it in an Arch environment (the release workflow does).
set -e
PKG="$1"; VERSION="$2"; SUM="$3"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[ -n "$AUR_SSH_KEY" ] || { echo "AUR_SSH_KEY is not set; skipping the AUR." ; exit 0; }
T="$(mktemp -d)"
umask 077
printf '%s\n' "$AUR_SSH_KEY" > "$T/key"
ssh-keyscan -t ed25519,rsa aur.archlinux.org > "$T/known_hosts" 2>/dev/null
export GIT_SSH_COMMAND="ssh -i $T/key -o UserKnownHostsFile=$T/known_hosts -o StrictHostKeyChecking=yes"
case "$PKG" in
    fileharbor) GEN=make-pkgbuild.sh ;;
    fileharbor-bin) GEN=make-pkgbuild-bin.sh ;;
    *) echo "unknown package $PKG" >&2; exit 2 ;;
esac
git clone "${AUR_BASE_URL:-ssh://aur@aur.archlinux.org}/$PKG.git" "$T/repo"
cd "$T/repo"
git config user.name "${AUR_GIT_NAME:-Asuka}"
git config user.email "${AUR_GIT_EMAIL:-asuka@archdev.in}"
"$ROOT/scripts/ci/$GEN" "$VERSION" "$SUM" > PKGBUILD
if [ "$(id -u)" = 0 ]; then       # makepkg refuses to run as root (CI containers): use a scratch copy a normal user can read
    id builder >/dev/null 2>&1 || useradd -m builder
    W="$(mktemp -d /tmp/srcinfo.XXXXXX)"
    cp PKGBUILD "$W/PKGBUILD"
    chmod 755 "$W" && chown -R builder "$W"
    su builder -c "cd '$W' && makepkg --printsrcinfo" > .SRCINFO
    rm -rf "$W"
else
    makepkg --printsrcinfo > .SRCINFO
fi
git add PKGBUILD .SRCINFO
if git diff --cached --quiet; then
    echo "The AUR already has $PKG $VERSION."
    exit 0
fi
git commit -m "Update to $VERSION"
git push origin HEAD:master
rm -rf "$T"
echo "Published $PKG $VERSION to the AUR."
