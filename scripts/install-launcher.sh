#!/bin/sh
# Make the checkout's FileHarbor known to the desktop: name and icon in the app menu and dock, and the
# file-manager roles ("Show in folder" from browsers and other apps, opening folders).
#   scripts/install-launcher.sh             register it (launcher, icons, D-Bus services)
#   scripts/install-launcher.sh --default   same, and make it the default for opening folders
#   scripts/install-launcher.sh --remove    take it all away again
# Everything goes to ~/.local/share, so it is for running from this checkout; the AUR package and the
# Flatpak install system-wide files instead. The launcher and the two D-Bus services written here are named
# for FileHarbor, so they sit beside an installed Nemo's. Build first (scripts/build.sh): the launcher is copied from ./install.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor"
BUS="$DATA/dbus-1/services"
refresh() {
    update-desktop-database "$APPS" 2>/dev/null
    gtk-update-icon-cache -q -t "$ICONS" 2>/dev/null
}
if [ "$1" = "--remove" ]; then
    rm -f "$APPS/in.archdev.FileHarbor.desktop" "$ICONS"/*/apps/in.archdev.FileHarbor.png "$BUS/in.archdev.FileHarbor.service" "$BUS/in.archdev.FileHarbor.FileManager1.service" "$APPS/nemo.desktop" "$BUS/nemo.service" "$BUS/nemo.FileManager1.service"
    refresh
    echo "removed (if FileHarbor was your default for folders, pick another in the system settings)"
    exit 0
fi
[ -f "$ROOT/install/share/applications/in.archdev.FileHarbor.desktop" ] || { echo "Run scripts/build.sh first." >&2; exit 1; }
mkdir -p "$APPS" "$BUS"
for s in 16 24 32 48 64 128 256 512; do
    mkdir -p "$ICONS/${s}x${s}/apps"
    cp "$ROOT/data/icons/hicolor/apps/${s}x${s}/in.archdev.FileHarbor.png" "$ICONS/${s}x${s}/apps/"
done
RUN="$ROOT/scripts/run.sh --session"
# same entry as the installed one, but starting the checkout's copy without quitting a running one
sed "s|^Exec=fileharbor |Exec=$RUN |" "$ROOT/install/share/applications/in.archdev.FileHarbor.desktop" > "$APPS/in.archdev.FileHarbor.desktop"
# D-Bus activation: other programs ask for org.freedesktop.FileManager1 (Show in folder) and in.archdev.FileHarbor
printf '[D-BUS Service]\nName=org.freedesktop.FileManager1\nExec=%s --no-default-window\n' "$RUN" > "$BUS/in.archdev.FileHarbor.FileManager1.service"
printf '[D-BUS Service]\nName=in.archdev.FileHarbor\nExec=%s --no-default-window\n' "$RUN" > "$BUS/in.archdev.FileHarbor.service"
refresh
if [ "$1" = "--default" ]; then
    xdg-mime default in.archdev.FileHarbor.desktop inode/directory application/x-gnome-saved-search
    echo "FileHarbor is now the default for opening folders."
fi
echo "registered in $DATA (log out and in, or restart the panel, if the dock still shows the old entry)"
