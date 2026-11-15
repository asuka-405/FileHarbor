"""Data behind the FileHarbor developer panel (no GTK here, so it can be tested): log parsing, theme
contrast, the facts about the install and the desktop, and the text of the diagnostic report."""
import json
import os
import platform
import shutil
import subprocess
import sys

LEVELS = ["DEBUG", "INFO", "MESSAGE", "WARNING", "CRITICAL", "ERROR"]


def cache_dir():
    base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    return os.path.join(base, "fileharbor", "debug")


def config_dir():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(base, "fileharbor")


def log_path():
    return os.path.join(cache_dir(), "debug.log")


def theme_path():
    return os.path.join(cache_dir(), "theme.json")


def dev_css_path():
    return os.path.join(config_dir(), "dev.css")


# ------------------------------------------------------------------ log
def parse_log_line(line):
    """'12:00:01.123\\tWARNING\\tNemo\\ttext' -> (time, level, domain, text); anything else is kept as a plain MESSAGE."""
    line = line.rstrip("\n")
    parts = line.split("\t", 3)
    if len(parts) == 4 and parts[1] in LEVELS:
        return parts[0], parts[1], parts[2], parts[3].replace("\x1f", "\n")
    return "", "MESSAGE", "-", line


def filter_records(records, levels, needle=""):
    needle = needle.lower().strip()
    out = []
    for r in records:
        if r[1] not in levels:
            continue
        if needle and needle not in (r[2] + " " + r[3]).lower():
            continue
        out.append(r)
    return out


def read_new(path, offset):
    """Reads what was appended to the log since `offset`; returns (records, new_offset). A shorter file means the
    log was cleared or rotated, so reading starts again from the top."""
    try:
        size = os.path.getsize(path)
    except OSError:
        return [], 0
    if size < offset:
        offset = 0
    if size == offset:
        return [], offset
    with open(path, "r", errors="replace") as f:
        f.seek(offset)
        data = f.read()
        new_offset = f.tell()
    if not data.endswith("\n"):                       # a half-written last line waits for the next read
        cut = data.rfind("\n") + 1
        new_offset -= len(data) - cut
        data = data[:cut]
    return [parse_log_line(l) for l in data.splitlines() if l.strip()], new_offset


# ------------------------------------------------------------------ theme
def _lum(hexcolor):
    h = hexcolor.lstrip("#")
    r, g, b = (int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))

    def f(v):
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)


def contrast(a, b):
    la, lb = _lum(a), _lum(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


PAIRS = [("text", "surface", 4.5), ("text", "window", 4.5), ("text_dim", "surface", 4.5), ("text_faint", "surface", 3.0),
         ("accent_text", "accent", 4.5), ("accent", "surface", 3.0), ("success", "surface", 3.0),
         ("warning", "surface", 3.0), ("error", "surface", 3.0), ("info", "surface", 3.0)]


def contrast_report(colors):
    """[(foreground, background, ratio, target, ok)] for the pairs that matter."""
    out = []
    for fg, bg, want in PAIRS:
        if fg in colors and bg in colors:
            ratio = contrast(colors[fg], colors[bg])
            out.append((fg, bg, ratio, want, ratio >= want))
    return out


def read_theme():
    try:
        with open(theme_path()) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


# ------------------------------------------------------------------ facts
def _first_line(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=3)
        return (r.stdout or r.stderr).strip().splitlines()[0]
    except Exception:
        return "unavailable"


def python_modules():
    out = []
    for name in ("PIL", "markdown", "pygments", "yaml"):
        try:
            m = __import__(name)
            out.append((name, getattr(m, "__version__", getattr(m, "VERSION", "installed"))))
        except Exception:
            out.append((name, "MISSING"))
    return out


def tools_on_path():
    return [(t, shutil.which(t) or "missing") for t in ("prettier", "ffmpeg", "ffprobe", "dcraw_emu", "catdoc", "dconf", "gdbus")]


def overview():
    """[(section, [(key, value)])]"""
    exe = os.path.realpath("/proc/self/exe") if os.path.exists("/proc/self/exe") else sys.executable
    env_keys = ["XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE", "WAYLAND_DISPLAY", "DISPLAY", "GDK_BACKEND", "GTK_THEME",
                "FILEHARBOR_DEV", "NEMO_CLASSIC", "NEMO_THEME_DIR", "G_MESSAGES_DEBUG", "GTK_DEBUG", "GDK_DEBUG", "LANG"]
    sections = [
        ("Program", [("Executable", exe), ("Nemo base", _first_line(["fileharbor", "--version"])),
                     ("Python", platform.python_version()), ("System", platform.platform())]),
        ("Python modules", python_modules()),
        ("Helper programs", tools_on_path()),
        ("Environment", [(k, os.environ.get(k, "")) for k in env_keys if os.environ.get(k)]),
        ("Folders", [("Config", config_dir()), ("Debug files", cache_dir()), ("dev.css", dev_css_path())]),
    ]
    return sections


def read_tail(path, lines=100):
    try:
        with open(path, "r", errors="replace") as f:
            return [parse_log_line(l) for l in f.read().splitlines()[-lines:] if l.strip()]
    except OSError:
        return []


def report_text(extra_sections=(), changed_settings=(), tail=100):
    """The diagnostic report to paste into a bug report: Markdown."""
    out = ["# FileHarbor diagnostic report", ""]
    for title, rows in list(overview()) + list(extra_sections):
        out.append("## " + title)
        out += ["- %s: %s" % kv for kv in rows] or ["- (nothing)"]
        out.append("")
    theme = read_theme()
    if theme:
        out += ["## Theme", "- theme: %s, mode: %s, dark: %s" % (theme.get("theme"), theme.get("mode"), theme.get("dark")),
                "- desktop: %s (prefers dark: %s, accent: %s, use desktop accent: %s)" % (
                    theme.get("desktop"), theme.get("desktop_prefers_dark"), theme.get("desktop_accent"), theme.get("use_desktop_accent")),
                "- colours: " + ", ".join("%s=%s" % kv for kv in theme.get("colors", {}).items()), ""]
        bad = [r for r in contrast_report(theme.get("colors", {})) if not r[4]]
        out += ["- contrast below target: " + (", ".join("%s on %s %.2f<%.1f" % (a, b, r, w) for a, b, r, w, _ in bad) or "none"), ""]
    if changed_settings:
        out += ["## Settings that differ from the defaults"] + ["- %s = %s" % kv for kv in changed_settings] + [""]
    records = read_tail(log_path(), tail)
    out += ["## Last %d log lines" % len(records), "```"]
    out += ["%s %s [%s] %s" % r for r in records]
    out += ["```", ""]
    return "\n".join(out)
