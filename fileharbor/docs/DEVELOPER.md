# Developer mode

Developer mode gathers what you need to work on FileHarbor or to debug a problem. It is off by default.

# Turning it on

- **Edit > Appearance > Developer mode**, or
- start with `fileharbor --developer`, or
- set `FILEHARBOR_DEV=1` for one run.

From a shell: `gsettings set in.archdev.FileHarbor developer-mode true`.

# What it does

- Writes every GLib log message, including `g_debug`, to `~/.cache/fileharbor/debug/debug.log` (deleted at start-up if over 8 MB).
- Enables the GTK inspector (`Ctrl+Shift+I` or `Ctrl+Shift+D`).
- Writes the resolved theme to `~/.cache/fileharbor/debug/theme.json` each time it changes.
- Reloads `~/.config/fileharbor/dev.css` whenever you save it. It is loaded after the theme, so it overrides anything. Syntax errors are logged and the previous look stays.
- Adds **Tools > FileHarbor developer panel** (`Ctrl+Alt+D`).

# The developer panel

- **Overview**: version, install paths, desktop and session type, Python modules, and which helper tools are on `PATH`.
- **Log**: live log with level filters and a text filter; buttons to open the file or clear it.
- **Theme**: the resolved palette, the mode and variant in use, contrast ratios for the important pairs, and buttons to reload the theme and `dev.css`, edit `dev.css` and open the themes folder.
- **Settings**: every `in.archdev.FileHarbor` key with its current and default value, changed ones highlighted, and a button to reset the selected one.
- **Services**: whether the Storage Sense daemon and the file manager D-Bus services are running, with a button to start the daemon.

The header has **GTK inspector**, **Restart FileHarbor** and **Copy diagnostic report**.

The report is Markdown (overview, changed settings, the end of the log), ready to paste into an issue.

# Handy commands

```
scripts/build.sh                 # build into the local prefix
scripts/run.sh --live            # quit the running instance, start this build
for t in fileharbor/tools/tests/test_*.py; do python3 "$t"; done
python3 scripts/gen-themes.py    # regenerate data/themes/*.ini and check contrast
```
