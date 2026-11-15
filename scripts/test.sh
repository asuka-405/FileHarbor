#!/bin/sh
# Tests for the tools' logic (no display needed). Prints PASS/FAIL per check; exits 1 on any FAIL.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
out=$(for t in "$ROOT"/fileharbor/tools/tests/test_*.py; do python3 -I "$t"; done)
echo "$out"
echo "$out" | grep -q '^FAIL' && exit 1
exit 0
