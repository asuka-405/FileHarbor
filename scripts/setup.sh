#!/bin/sh
# One-time setup for the checkout's build (./install, see scripts/build.sh). Same as running fileharbor-setup.
#   scripts/setup.sh             interactive, for your account
#   scripts/setup.sh --dry-run   show what it would do
#   scripts/setup.sh --undo      take it back
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -x "$ROOT/install/bin/fileharbor-setup" ] || { echo "Run scripts/build.sh first." >&2; exit 1; }
exec "$ROOT/install/bin/fileharbor-setup" "$@"
