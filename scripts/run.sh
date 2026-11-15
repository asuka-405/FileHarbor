#!/bin/sh
# Run FileHarbor from its local install prefix (./install, see scripts/build.sh).
#   scripts/run.sh            isolated test session (own D-Bus, system Nemo untouched)
#   scripts/run.sh --live     real session: quits system Nemo first, uses your real settings
#   scripts/run.sh --session  real session, nothing is quit: a running FileHarbor just opens the new window
#                             (this is what the launcher and the D-Bus services installed by
#                             scripts/install-launcher.sh use)
P="$(cd "$(dirname "$0")/.." && pwd)/install"
export XDG_DATA_DIRS="$P/share:/usr/share"
export GSETTINGS_SCHEMA_DIR="$P/share/glib-2.0/schemas"
export PATH="$P/bin:$PATH"
# the fork ships its own libnemo-extension (it carries the UI resources); without
# this the system copy is loaded and the fork silently loses its resources
export LD_LIBRARY_PATH="$P/lib:$P/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export GI_TYPELIB_PATH="$P/lib/girepository-1.0${GI_TYPELIB_PATH:+:$GI_TYPELIB_PATH}"
if [ "$1" = "--session" ]; then
    shift
    exec "$P/bin/fileharbor" "$@"
fi
if [ "$1" = "--live" ]; then
    shift
    fileharbor -q 2>/dev/null
    exec "$P/bin/fileharbor" "$@"
fi
exec dbus-run-session -- "$P/bin/fileharbor" "$@"
