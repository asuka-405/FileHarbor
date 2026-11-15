#!/usr/bin/env python3
"""Generate the built-in themes in data/themes/ and check their contrast.

    scripts/gen-themes.py            write data/themes/*.ini
    scripts/gen-themes.py --check    only check the files already there (used by scripts/test.sh)

Every theme has a [Dark] and a [Light] variant with the same 16 colours:

  surfaces   window, surface, surface_alt, surface_raised
  text       text, text_dim, text_faint
  accent     accent, accent_text
  lines      border, border_strong, hover
  status     success, warning, error, info

The palette is deliberately quiet: neutrals carry the interface, the accent marks selection, focus and the
one primary action, and the four status colours appear only where something needs attention. Contrast
targets (WCAG): text on surfaces 4.5, dim text 4.5, faint text 3, accent text on accent 4.5, status colours on
surface 3 (they are used for icons, bars and short labels).
"""
import colorsys
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "data", "themes")
KEYS = ["window", "surface", "surface_alt", "surface_raised", "text", "text_dim", "text_faint",
        "accent", "accent_text", "border", "border_strong", "hover", "success", "warning", "error", "info"]
CORAL_DARK, CORAL_LIGHT = "#ff7a68", "#c9372c"      # the default accent: coral
SELECTION_MIX = 0.28                                 # how much accent is mixed into a selected row (gresources/fileharbor-base.css)


# ---------------------------------------------------------------- colour helpers
def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))


def hexs(c):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v * 255))) for v in c)


def lum(h):
    def f(v):
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (f(v) for v in rgb(h))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = lum(a), lum(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def hsl(h, s, l):
    """h in degrees, s and l in 0-100"""
    r, g, b = colorsys.hls_to_rgb(h / 360.0, l / 100.0, s / 100.0)
    return hexs((r, g, b))


def mix(a, b, t):
    ra, rb = rgb(a), rgb(b)
    return hexs(tuple(x * (1 - t) + y * t for x, y in zip(ra, rb)))


def readable_on(bg, dark="#12100f", light="#ffffff"):
    return light if contrast(light, bg) >= contrast(dark, bg) else dark


def fit_text(c, bg, target):
    """Move colour c towards black or white until it reaches the contrast target against bg."""
    toward = "#ffffff" if lum(bg) < 0.4 else "#000000"
    for i in range(0, 101):
        cand = mix(c, toward, i / 100)
        if contrast(cand, bg) >= target:
            return cand
    return toward


# status colours: the same hues everywhere, tuned per variant
STATUS_DARK = {"success": "#5fd18a", "warning": "#f2b84b", "error": "#ff7b72", "info": "#6cb6ff"}
STATUS_LIGHT = {"success": "#15803d", "warning": "#946200", "error": "#b3261e", "info": "#1a63c4"}


def neutral_ramp(hue, sat, dark):
    """The surfaces, lines and text of a tinted-grey interface."""
    if dark:
        p = dict(window=hsl(hue, sat, 8), surface=hsl(hue, sat, 12.5), surface_alt=hsl(hue, sat, 16.5),
                 surface_raised=hsl(hue, sat, 21), text=hsl(hue, min(sat, 12), 92), text_dim=hsl(hue, min(sat, 12), 72),
                 text_faint=hsl(hue, min(sat, 10), 56), border=hsl(hue, sat, 24), border_strong=hsl(hue, sat, 34),
                 hover=hsl(hue, sat, 20))
    else:
        p = dict(window=hsl(hue, sat, 89), surface=hsl(hue, sat, 97), surface_alt=hsl(hue, sat, 93),
                 surface_raised=hsl(hue, sat, 100), text=hsl(hue, min(sat, 20), 9), text_dim=hsl(hue, min(sat, 14), 33),
                 text_faint=hsl(hue, min(sat, 10), 44), border=hsl(hue, sat, 80), border_strong=hsl(hue, sat, 66),
                 hover=hsl(hue, sat, 90))
    return p


def finish(p, dark, status=None):
    """Fill the accent text and the status colours, and nudge anything that misses its contrast target."""
    p = dict(p)
    p["accent_text"] = readable_on(p["accent"])
    for k, v in (status or (STATUS_DARK if dark else STATUS_LIGHT)).items():
        p[k] = v
    # keep text readable on the three reading surfaces
    for surf in ("window", "surface", "surface_alt"):
        p["text"] = fit_text(p["text"], p[surf], 7.0 if surf == "surface" else 4.5) if contrast(p["text"], p[surf]) < 4.5 else p["text"]
        p["text_dim"] = fit_text(p["text_dim"], p[surf], 4.5)
        p["text_faint"] = fit_text(p["text_faint"], p[surf], 3.0)
    for k in ("success", "warning", "error", "info", "accent"):
        for surf in ("window", "surface", "surface_alt"):
            p[k] = fit_text(p[k], p[surf], 3.0) if contrast(p[k], p[surf]) < 3.0 else p[k]
    p["accent_text"] = readable_on(p["accent"])
    return p


def tinted(name, hue, sat, accent_dark, accent_light):
    out = {}
    for dark, accent in ((True, accent_dark), (False, accent_light)):
        p = neutral_ramp(hue, sat, dark)
        p["accent"] = accent
        out["Dark" if dark else "Light"] = finish(p, dark)
    return out


def explicit(dark_vals, light_vals):
    return {"Dark": finish(dark_vals, True), "Light": finish(light_vals, False)}


# ---------------------------------------------------------------- the themes
THEMES = {}

# FileHarbor: neutral graphite and warm paper with the coral accent. The base every other theme falls back to.
THEMES["fileharbor"] = dict(
    name="FileHarbor", metrics=dict(radius=0, gap=8, border=0),
    note="The default look: quiet neutrals with a coral accent.",
    variants=tinted("fileharbor", 20, 6, CORAL_DARK, CORAL_LIGHT))
THEMES["ocean"] = dict(
    name="Ocean", metrics=dict(radius=0, gap=8, border=0), note="Deep blue-grey with a cyan accent.",
    variants=tinted("ocean", 212, 22, "#4cc9f0", "#0b6b8a"))
THEMES["forest"] = dict(
    name="Forest", metrics=dict(radius=0, gap=8, border=0), note="Mossy neutrals with a green accent.",
    variants=tinted("forest", 140, 10, "#6fcf8d", "#1e7a47"))
THEMES["dusk"] = dict(
    name="Dusk", metrics=dict(radius=0, gap=8, border=0), note="Soft violet greys with a lavender accent.",
    variants=tinted("dusk", 262, 16, "#b79cff", "#6a45d8"))
THEMES["sand"] = dict(
    name="Sand", metrics=dict(radius=0, gap=8, border=0), note="Warm stone neutrals with an amber accent.",
    variants=tinted("sand", 35, 14, "#f2b24c", "#9a5a00"))
THEMES["slate"] = dict(
    name="Slate", metrics=dict(radius=0, gap=8, border=0), note="Cool monochrome with a steel-blue accent.",
    variants=tinted("slate", 220, 8, "#9db7de", "#3d5f94"))

# Themes that match the default look of a desktop environment
THEMES["cosmic"] = dict(
    name="COSMIC", metrics=dict(radius=0, gap=10, border=0), note="The COSMIC desktop's default palette.",
    variants=explicit(
        dict(window="#141414", surface="#242424", surface_alt="#2e2e2e", surface_raised="#383838", text="#ececec",
             text_dim="#b4b4b4", text_faint="#8c8c8c", accent="#63d0df", border="#3a3a3a", border_strong="#5a5a5a", hover="#363636"),
        dict(window="#d9d9d9", surface="#f7f7f7", surface_alt="#ebebeb", surface_raised="#ffffff", text="#161616",
             text_dim="#4f4f4f", text_faint="#6e6e6e", accent="#00707d", border="#bdbdbd", border_strong="#9a9a9a", hover="#dcdcdc")))
THEMES["gnome"] = dict(
    name="GNOME (Adwaita)", metrics=dict(radius=6, gap=8, border=1), note="Matches GNOME's Adwaita look.",
    variants=explicit(
        dict(window="#242424", surface="#1e1e1e", surface_alt="#303030", surface_raised="#3a3a3a", text="#ffffff",
             text_dim="#c0bfbc", text_faint="#9a9996", accent="#78aeed", border="#3d3d3d", border_strong="#5e5c64", hover="#383838"),
        dict(window="#fafafa", surface="#ffffff", surface_alt="#ebebeb", surface_raised="#ffffff", text="#2e3436",
             text_dim="#5e5c64", text_faint="#77767b", accent="#1c71d8", border="#d8d8d8", border_strong="#b8b8b8", hover="#e6e6e6")))
THEMES["kde"] = dict(
    name="KDE (Breeze)", metrics=dict(radius=3, gap=6, border=1), note="Matches KDE Plasma's Breeze look.",
    variants=explicit(
        dict(window="#31363b", surface="#232629", surface_alt="#2a2e32", surface_raised="#3b4045", text="#eff0f1",
             text_dim="#bdc3c7", text_faint="#9da3a8", accent="#3daee9", border="#4d4d4d", border_strong="#6c7175", hover="#3a3f44"),
        dict(window="#eff0f1", surface="#fcfcfc", surface_alt="#e6e7e8", surface_raised="#ffffff", text="#232627",
             text_dim="#4d5459", text_faint="#6f777c", accent="#1a74a6", border="#bcc0c4", border_strong="#9fa4a8", hover="#dfe1e3")))
THEMES["cinnamon"] = dict(
    name="Cinnamon (Mint-Y)", metrics=dict(radius=3, gap=6, border=1), note="Matches Cinnamon's Mint-Y look.",
    variants=explicit(
        dict(window="#353535", surface="#2b2b2b", surface_alt="#3c3c3c", surface_raised="#474747", text="#dadada",
             text_dim="#b0b0b0", text_faint="#8c8c8c", accent="#8fa876", border="#4a4a4a", border_strong="#666666", hover="#424242"),
        dict(window="#e8e8e8", surface="#ffffff", surface_alt="#f1f1f1", surface_raised="#ffffff", text="#2e2e2e",
             text_dim="#555555", text_faint="#737373", accent="#4f6b3a", border="#cfcfcf", border_strong="#adadad", hover="#e0e0e0")))
THEMES["hyprland"] = dict(
    name="Hyprland", metrics=dict(radius=0, gap=10, border=1), note="Dark tiles with Hyprland's cyan-to-green glow; for tiling setups.",
    variants=explicit(
        dict(window="#10111a", surface="#171826", surface_alt="#1e2030", surface_raised="#282a3e", text="#e4e6f4",
             text_dim="#a7abc4", text_faint="#7f84a0", accent="#33ccff", border="#2a2d44", border_strong="#434765", hover="#232538"),
        dict(window="#e9ecf4", surface="#f7f8fc", surface_alt="#eceff7", surface_raised="#ffffff", text="#1b1e2e",
             text_dim="#454a66", text_faint="#666b88", accent="#0a78a6", border="#c7cbe0", border_strong="#a4a9c6", hover="#dde1ee")))

ORDER = ["fileharbor", "ocean", "forest", "dusk", "sand", "slate", "cosmic", "gnome", "kde", "cinnamon", "hyprland"]


# ---------------------------------------------------------------- output and checks
def render(tid, t):
    lines = ["# %s: %s" % (t["name"], t["note"]),
             "# Generated by scripts/gen-themes.py; edit that file, not this one.",
             "[Theme]", "Name=%s" % t["name"], "Description=%s" % t["note"], ""]
    for variant in ("Dark", "Light"):
        lines.append("[%s]" % variant)
        for k in KEYS:
            lines.append("%s=%s" % (k, t["variants"][variant][k]))
        lines.append("")
    lines += ["[Metrics]"] + ["%s=%s" % kv for kv in t["metrics"].items()] + [""]
    return "\n".join(lines)


def read_ini(path):
    data, sec = {}, None
    for raw in open(path):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            sec = line[1:-1]
            data[sec] = {}
        elif "=" in line and sec:
            k, v = line.split("=", 1)
            data[sec][k] = v
    return data


def check_palette(label, p):
    problems = []
    need = [("text", "window", 4.5), ("text", "surface", 4.5), ("text", "surface_alt", 4.5), ("text", "surface_raised", 4.5),
            ("text_dim", "window", 4.5), ("text_dim", "surface", 4.5), ("text_dim", "surface_alt", 4.5),
            ("text_faint", "surface", 3.0), ("accent_text", "accent", 4.5),
            ("accent", "surface", 3.0), ("accent", "window", 3.0),
            ("success", "surface", 3.0), ("warning", "surface", 3.0), ("error", "surface", 3.0), ("info", "surface", 3.0),
            ("border_strong", "surface", 1.5)]
    for fg, bg, want in need:
        got = contrast(p[fg], p[bg])
        if got < want:
            problems.append("%s: %s on %s is %.2f, needs %.1f" % (label, fg, bg, got, want))
    # text on a selected row: the accent tint the stylesheet mixes into the surface (SELECTION_MIX)
    for surf in ("surface", "surface_alt"):
        sel = mix(p[surf], p["accent"], SELECTION_MIX)
        got = contrast(p["text"], sel)
        if got < 4.5:
            problems.append("%s: text on a selected %s row is %.2f, needs 4.5" % (label, surf, got))
    for k in KEYS:
        if k not in p:
            problems.append("%s: missing %s" % (label, k))
    return problems


def check_dir(directory):
    problems, count = [], 0
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".ini"):
            continue
        data = read_ini(os.path.join(directory, name))
        for variant in ("Dark", "Light"):
            if variant in data:
                count += 1
                problems += check_palette("%s [%s]" % (name, variant), data[variant])
    return problems, count


def main():
    if "--check" not in sys.argv:
        os.makedirs(OUT, exist_ok=True)
        # drop the previous generation (the themes of older versions) and write the new set
        for stale in ("classic.ini", "cosmic-dark.ini", "cosmic-light.ini"):
            p = os.path.join(OUT, stale)
            if os.path.exists(p):
                os.remove(p)
        for tid in ORDER:
            with open(os.path.join(OUT, tid + ".ini"), "w") as f:
                f.write(render(tid, THEMES[tid]))
        print("wrote %d themes to %s (system.ini is hand-written)" % (len(ORDER), os.path.normpath(OUT)))
    problems, count = check_dir(OUT)
    for p in problems:
        print("FAIL", p)
    print("%s %d theme variants checked, %d problems" % ("FAIL" if problems else "PASS", count, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
