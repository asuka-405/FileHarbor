# Run: scripts/test.sh   checks every shipped theme: all 16 colours present, contrast targets met
import os
import subprocess
import sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
r = subprocess.run([sys.executable, "-I", os.path.join(root, "scripts", "gen-themes.py"), "--check"],
                   capture_output=True, text=True)
for line in r.stdout.splitlines():
    print(("FAIL " + line[5:]) if line.startswith("FAIL ") and "problems" not in line else
          ("PASS themes: " + line[5:]) if line.startswith("PASS") else
          ("FAIL themes: " + line[5:]) if line.startswith("FAIL") else line)
if r.returncode != 0 and "FAIL" not in r.stdout:
    print("FAIL themes: the checker could not run: " + r.stderr[-200:])
