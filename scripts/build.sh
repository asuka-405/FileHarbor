#!/bin/sh
# Configure (first run), build and install FileHarbor into ./install.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [ ! -d build ]; then
    meson setup build --prefix="$ROOT/install" --libdir=lib -Dtracker=false "$@"
fi
ninja -C build
ninja -C build install
glib-compile-schemas "$ROOT/install/share/glib-2.0/schemas"
