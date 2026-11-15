# Packaging

| Target | Files | Status |
| --- | --- | --- |
| Arch Linux / AUR `fileharbor` | `aur/PKGBUILD` | Built from source in a clean Arch container, tests pass |
| Arch Linux / AUR `fileharbor-bin` | `aur-bin/PKGBUILD` | Installs the package CI builds; built locally from it |
| Flatpak `in.archdev.FileHarbor` | `flatpak/in.archdev.FileHarbor.yml`, `.metainfo.xml` | Direct bundle on each GitHub release; built and smoke-tested in a Docker container (GNOME 50) |
| Debian files | `debian/` | Inherited from Nemo, unmaintained |

FileHarbor installs the `fileharbor` binary (with `nemo` and `nemo-desktop` links for old scripts), and keeps Nemo's library and data file names so Nemo extensions keep working, so on Arch it **replaces** the `nemo`
package (`conflicts` and `provides` are set) and it cannot be installed next to it.

## Arch Linux / AUR

Build and install from the repository without the AUR:

    cd packaging/aur
    makepkg -si          # downloads the release tarball named in PKGBUILD

To test a local tarball instead, build one (`scripts/release.sh`), copy `dist/fileharbor-<version>.tar.gz`
next to the PKGBUILD and change `source` to the file name.

Two AUR packages are published: `fileharbor` (builds from the source tarball) and `fileharbor-bin` (installs the
prebuilt `.pkg.tar.zst` of the release; it is built against the libraries of release day, so after a big Arch
library update the source package is the safer choice). The release workflow pushes both
(`scripts/ci/publish-aur.sh <package> <version> <sha256>`, which can be tried against local bare repositories
with `AUR_BASE_URL`). By hand:

1. Make the release (see below) and upload `dist/fileharbor-<version>.tar.gz` to the GitHub release.
2. In `PKGBUILD` replace `sha256sums=('SKIP')` with the checksum `scripts/release.sh` printed, and set
   `pkgver` / `pkgrel`.
3. `makepkg --printsrcinfo > .SRCINFO`, then push both files to the `fileharbor` AUR git repository.

## Flatpak

    flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/in.archdev.FileHarbor.yml
    flatpak run in.archdev.FileHarbor

The release workflow builds the same manifest and attaches `FileHarbor-<version>.flatpak` to the GitHub release; users
install it with `flatpak install --user FileHarbor-<version>.flatpak`. It is **not** published on a Flatpak store, only
as this direct bundle. The Python packages come from `flatpak/python3-modules.yaml`; to refresh them run
`flatpak-pip-generator --runtime org.gnome.Sdk//50 --output python3-modules --yaml markdown pygments pyyaml "pillow==11.3.0"`.

Limits of a sandboxed file manager: the **embedded terminal runs inside the sandbox**, not on the host
(use `flatpak-spawn --host` for a host shell); devices and network mounts depend on the host's gvfs;
Prettier, RAW support (`dcraw_emu`) and catdoc are bundled in the Flatpak. For daily use on
Arch the AUR package is the better fit.

## Releases

A production release is cut in four steps; everything after step 3 is automatic.

1. Update `VERSION` and add a `## <version>` section to `CHANGELOG.md`, commit, push `main`.
2. `scripts/release.sh --tag` runs the tests locally and creates the tag `v<version>` (it also leaves
   `dist/fileharbor-<version>.tar.gz` for a local look). `git push origin v<version>`.
3. On GitHub: Releases, Draft a new release, choose the tag, write the notes, **Publish release**.
4. The `release` workflow (`.github/workflows/release.yml`) starts by itself:

| Job | What it does |
| --- | --- |
| Production release? | Stops quietly unless the tag is exactly `vX.Y.Z` and the release is not marked "pre-release". `-alpha`, `-beta`, `-rc` and the like are never built. Fails loudly if the tag and `VERSION` disagree or the changelog section is missing. |
| Tests and source tarball | Runs `scripts/test.sh`, builds `fileharbor-<version>.tar.gz` and its `.sha256`, attaches both to the release. |
| Arch package | Builds `fileharbor-<version>-1-<arch>.pkg.tar.zst` in an `archlinux` container (`scripts/ci/build-arch.sh`), including the test run. |
| Flatpak bundle | Builds `FileHarbor-<version>.flatpak`. **Allowed to fail**: the manifest has never been built, so this job is the first real test of it. |
| Attach packages | Adds the package and the bundle to the release. |
| Update the AUR package | Pushes `PKGBUILD` and `.SRCINFO` to the `fileharbor` AUR repository (`scripts/ci/publish-aur.sh`). |

To publish a pre-release, tick "Set as a pre-release" when publishing, or use a tag like `v0.2.0-beta.1`:
nothing runs. To rebuild a release (for example after fixing the workflow), run the workflow by hand from the
Actions tab (Run workflow, enter the tag).

### One-time setup for the AUR job

1. Create the AUR account and add your SSH public key to your profile.
2. Make a dedicated key pair for CI: `ssh-keygen -t ed25519 -f aur-ci -N ""`, add `aur-ci.pub` to the AUR profile as well.
3. GitHub repository, Settings, Secrets and variables, Actions: add the secret `AUR_SSH_KEY` with the contents
   of `aur-ci` (the private key). Optionally add the variables `AUR_GIT_NAME` and `AUR_GIT_EMAIL` for the
   commit identity (default: `Asuka <asuka@archdev.in>`).
4. Without the secret the AUR step does nothing and says so; everything else still runs.

The helper scripts in `scripts/ci/` work on their own too: `check-release.sh <tag>`,
`make-pkgbuild.sh <version> <sha256> [tarball]` (prints the PKGBUILD for a release) and `build-arch.sh`
(run inside an Arch container).
