"""Colour themes for the Markdown preview, plus the CSS that goes with them."""
import json
import os

try:
    from pygments.formatters import HtmlFormatter
except Exception:          # pygments is optional: code blocks just lose their colours
    HtmlFormatter = None

SANS = '-apple-system, "Segoe UI", "Inter", "Cantarell", "Noto Sans", sans-serif'
SERIF = '"Charter", "Georgia", "Noto Serif", "Times New Roman", serif'
MONO = 'ui-monospace, "JetBrains Mono", "Fira Code", "DejaVu Sans Mono", monospace'

# id: (name, dark, fg, bg, line, accent, code, dim, font, pygments style, editor scheme)
_T = [
    ("github-light", "GitHub Light", False, "#1f2328", "#ffffff", "#d1d9e0", "#0969da", "#f6f8fa", "#59636e", SANS, "default", "classic"),
    ("github-dark", "GitHub Dark", True, "#e6edf3", "#0d1117", "#30363d", "#4493f8", "#161b22", "#9198a1", SANS, "github-dark", "oblivion"),
    ("solarized-light", "Solarized Light", False, "#586e75", "#fdf6e3", "#e0d8c0", "#268bd2", "#eee8d5", "#657b83", SANS, "solarized-light", "solarized-light"),
    ("solarized-dark", "Solarized Dark", True, "#93a1a1", "#002b36", "#0f4452", "#2aa198", "#073642", "#839496", SANS, "solarized-dark", "solarized-dark"),
    ("dracula", "Dracula", True, "#f8f8f2", "#282a36", "#44475a", "#bd93f9", "#21222c", "#a4abd0", SANS, "dracula", "oblivion"),
    ("nord", "Nord", True, "#d8dee9", "#2e3440", "#434c5e", "#88c0d0", "#3b4252", "#a3adc2", SANS, "nord", "cobalt"),
    ("gruvbox-dark", "Gruvbox Dark", True, "#ebdbb2", "#282828", "#504945", "#fabd2f", "#32302f", "#a89984", SANS, "gruvbox-dark", "oblivion"),
    ("gruvbox-light", "Gruvbox Light", False, "#3c3836", "#fbf1c7", "#d5c4a1", "#b57614", "#f2e5bc", "#665c54", SANS, "gruvbox-light", "kate"),
    ("monokai", "Monokai", True, "#f8f8f2", "#272822", "#49483e", "#a6e22e", "#1e1f1c", "#b0b0a0", SANS, "monokai", "oblivion"),
    ("one-dark", "One Dark", True, "#abb2bf", "#282c34", "#3e4451", "#61afef", "#21252b", "#9aa2b1", SANS, "one-dark", "oblivion"),
    ("tokyo-night", "Tokyo Night", True, "#c0caf5", "#1a1b26", "#2f3549", "#7aa2f7", "#16161e", "#9aa5ce", SANS, "material", "cobalt"),
    ("catppuccin-mocha", "Catppuccin Mocha", True, "#cdd6f4", "#1e1e2e", "#45475a", "#cba6f7", "#181825", "#a6adc8", SANS, "one-dark", "oblivion"),
    ("catppuccin-latte", "Catppuccin Latte", False, "#4c4f69", "#eff1f5", "#ccd0da", "#8839ef", "#e6e9ef", "#5c5f77", SANS, "default", "classic"),
    ("rose-pine", "Rosé Pine", True, "#e0def4", "#191724", "#403d52", "#c4a7e7", "#1f1d2e", "#a09cb8", SANS, "material", "oblivion"),
    ("sepia", "Sepia (serif)", False, "#5b4636", "#f4ecd8", "#dccfae", "#a0522d", "#eadfc3", "#7a6652", SERIF, "friendly", "kate"),
    ("paper", "Paper (serif)", False, "#222222", "#fafaf7", "#dcdcd4", "#8b1a1a", "#f0efe9", "#5e5e5e", SERIF, "default", "classic"),
    ("terminal", "Terminal", True, "#4cf07a", "#0a0f0a", "#14401f", "#4cf07a", "#0f1a12", "#28b955", MONO, "rrt", "oblivion"),
    ("contrast-dark", "High Contrast Dark", True, "#ffffff", "#000000", "#ffffff", "#ffd700", "#111111", "#dddddd", SANS, "rrt", "oblivion"),
    ("contrast-light", "High Contrast Light", False, "#000000", "#ffffff", "#000000", "#0000cc", "#eeeeee", "#222222", SANS, "bw", "classic"),
]

KEYS = ("name", "dark", "fg", "bg", "line", "accent", "code", "dim", "font", "pygments", "scheme")
THEMES = {t[0]: dict(zip(KEYS, t[1:])) for t in _T}
ORDER = [t[0] for t in _T]
AUTO = "auto"


def auto_theme(dark):
    """Follows the file manager: same neutral look as the rest of FileHarbor."""
    if dark:
        d = dict(THEMES["github-dark"])
        d.update(name="Follow FileHarbor", fg="#e4e4e4", bg="#1c1c1c", line="#3a3a3a", accent="#ff9933",
                 code="#2a2a2a", dim="#a8a8a8", scheme="oblivion")
    else:
        d = dict(THEMES["github-light"])
        d.update(name="Follow FileHarbor", fg="#1c1c1c", bg="#ffffff", line="#d8d8d8", accent="#b85c00",
                 code="#f2f2f2", dim="#5f5f5f")
    return d


def get(theme_id, dark):
    return THEMES.get(theme_id) or auto_theme(dark)


CSS = """
:root { color-scheme: %(scheme_mode)s; }
html { background: %(bg)s; }
body { font-family: %(font)s; line-height: 1.65; max-width: 860px; margin: 0 auto;
       padding: 28px 36px 80px; font-size: 16px; color: %(fg)s; background: %(bg)s; }
h1, h2 { border-bottom: 1px solid %(line)s; padding-bottom: .3em; }
h1, h2, h3, h4, h5, h6 { line-height: 1.25; margin-top: 1.6em; }
a { color: %(accent)s; }
code, pre { font-family: ui-monospace, "JetBrains Mono", "DejaVu Sans Mono", monospace; }
code { background: %(code)s; padding: .15em .4em; border-radius: 4px; font-size: 90%%; }
pre { background: %(code)s; padding: 14px 16px; overflow: auto; border-radius: 6px; line-height: 1.45; }
pre code { background: none; padding: 0; }
blockquote { margin: 1em 0; padding: 0 1em; color: %(dim)s; border-left: 4px solid %(line)s; }
table { border-collapse: collapse; margin: 1em 0; }
th, td { border: 1px solid %(line)s; padding: 6px 12px; }
th { background: %(code)s; }
img { max-width: 100%%; }
hr { border: 0; border-top: 1px solid %(line)s; margin: 2em 0; }
::selection { background: %(accent)s; color: %(bg)s; }
.admonition { border-left: 4px solid %(accent)s; background: %(code)s; padding: 4px 16px; border-radius: 4px; }
.admonition-title { font-weight: bold; }
.toc { background: %(code)s; padding: 8px 16px; border-radius: 6px; }
.footnote { color: %(dim)s; font-size: 90%%; }
.frontmatter { background: %(code)s; border-left: 4px solid %(accent)s; border-radius: 4px; padding: 6px 14px; margin-bottom: 2em; }
.frontmatter table { margin: 4px 0; } .frontmatter th, .frontmatter td { border: none; background: none; padding: 2px 14px 2px 0; text-align: left; }
.frontmatter th { color: %(dim)s; font-weight: normal; white-space: nowrap; }
/* MDX */
.mdx-component { border: 1px dashed %(line)s; border-radius: 6px; padding: 4px 14px 6px; margin: 1em 0; }
.mdx-component.mdx-callout { border: none; border-left: 4px solid %(accent)s; background: %(code)s; }
.mdx-label { font: 12px ui-monospace, "DejaVu Sans Mono", monospace; color: %(accent)s; margin: 6px 0 2px; }
.mdx-label .props { color: %(dim)s; }
.mdx-tag, .mdx-expr { font-size: 85%%; }
.mdx-tag { color: %(accent)s; } .mdx-expr { color: %(dim)s; }
.mdx-esm { color: %(dim)s; font-size: 13px; margin-bottom: 1.5em; }
.mdx-esm pre { font-size: 12px; }
.mdx-kind-warning, .mdx-kind-caution { border-left-color: #e5a50a !important; }
.mdx-kind-danger, .mdx-kind-error { border-left-color: #e5484d !important; }
.mdx-kind-tip, .mdx-kind-success { border-left-color: #30a46c !important; }
"""


def css_for(theme):
    css = CSS % dict(theme, scheme_mode="dark" if theme["dark"] else "light")
    if HtmlFormatter is not None:
        try:
            css += HtmlFormatter(style=theme.get("pygments", "default")).get_style_defs(".codehilite")
            css += "\n.codehilite, .codehilite pre { background: %s !important; }\n" % theme["code"]
        except Exception:
            pass
    return css


# ---- remembered choice ----------------------------------------------------
def _store():
    return os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"),
                        "fileharbor", "markdown.json")


def load_choice():
    try:
        with open(_store()) as f:
            t = json.load(f).get("theme", AUTO)
        return t if t in THEMES else AUTO
    except Exception:
        return AUTO


def save_choice(theme_id):
    try:
        os.makedirs(os.path.dirname(_store()), exist_ok=True)
        with open(_store(), "w") as f:
            json.dump({"theme": theme_id}, f)
    except OSError:
        pass
