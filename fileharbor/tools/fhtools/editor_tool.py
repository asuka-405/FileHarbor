"""Text editor: syntax highlighting for the languages developers use, search and replace, Prettier formatting,
and for Markdown / MDX a live preview next to the text. The editor pane can be collapsed to read the
rendered document on its own."""
import json
import os
import re
import shutil
import subprocess
import threading

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("GtkSource", "4")
gi.require_version("WebKit2", "4.1")
from gi.repository import Gtk, Gdk, Gio, GLib, GtkSource, WebKit2  # noqa: E402

import markdown as md_lib

from . import edschemes, mdthemes
from .base import ToolPage, ToolFailed, keep_recovery_copy
from .common import ask, message, is_dark_theme
from .mdx import convert as mdx_convert

MD_SUFFIXES = (".md", ".markdown", ".mdown", ".mkd", ".mdx")
MD_EXTENSIONS = ["extra", "toc", "sane_lists", "admonition", "footnotes"]
MAX_BYTES = 30 * 1024 * 1024

# file name / extension -> GtkSourceView language id, where guessing from the MIME type is not enough
LANG_BY_EXT = {
    ".mdx": "markdown", ".markdown": "markdown", ".mdown": "markdown", ".mkd": "markdown",
    ".tsx": "typescript-jsx", ".ts": "typescript", ".mts": "typescript", ".cts": "typescript",
    ".jsx": "jsx", ".mjs": "js", ".cjs": "js", ".js": "js",
    ".json": "json", ".jsonc": "json", ".json5": "json", ".webmanifest": "json",
    ".vue": "html", ".svelte": "html", ".astro": "html", ".htm": "html", ".xhtml": "html", ".svg": "xml",
    ".toml": "toml", ".ini": "ini", ".cfg": "ini", ".conf": "ini", ".service": "ini", ".timer": "ini", ".desktop": "desktop",
    ".yml": "yaml", ".yaml": "yaml", ".sh": "sh", ".bash": "sh", ".zsh": "sh", ".ksh": "sh", ".env": "sh", ".fish": "fish",
    ".ps1": "powershell", ".psm1": "powershell", ".bat": "dosbatch", ".cmd": "dosbatch",
    ".py": "python3", ".pyi": "python3", ".pyw": "python3", ".kt": "kotlin", ".kts": "kotlin", ".gradle": "gradle",
    ".tf": "terraform", ".tfvars": "terraform", ".hcl": "terraform", ".mk": "makefile", ".cmake": "cmake",
    ".h": "chdr", ".hpp": "cpphdr", ".hh": "cpphdr", ".hxx": "cpphdr", ".cc": "cpp", ".cxx": "cpp", ".cpp": "cpp", ".c": "c",
    ".cs": "c-sharp", ".fs": "fsharp", ".rs": "rust", ".go": "go", ".rb": "ruby", ".php": "php", ".lua": "lua", ".pl": "perl",
    ".swift": "swift", ".scala": "scala", ".dart": "dart", ".hs": "haskell", ".ml": "ocaml", ".jl": "julia", ".r": "r",
    ".sql": "sql", ".scss": "scss", ".sass": "scss", ".less": "less", ".css": "css", ".html": "html", ".xml": "xml",
    ".tex": "latex", ".rst": "rst", ".diff": "diff", ".patch": "diff", ".proto": "proto", ".sol": "solidity",
    ".vala": "vala", ".zig": None, ".nix": None, ".csv": "csv", ".tsv": "csv", ".ex": None, ".exs": None,
}
LANG_BY_NAME = {"dockerfile": "docker", "containerfile": "docker", "makefile": "makefile", "gnumakefile": "makefile",
                "cmakelists.txt": "cmake", "pkgbuild": "sh", ".bashrc": "sh", ".zshrc": "sh", ".profile": "sh",
                ".gitconfig": "ini", ".editorconfig": "ini", "meson.build": "meson", "cargo.lock": "toml"}
TABS_LANGS = {"go", "makefile", "automake"}
LINE_COMMENT = {"python3": "#", "python": "#", "sh": "#", "yaml": "#", "toml": "#", "ini": ";", "ruby": "#", "perl": "#",
                "makefile": "#", "docker": "#", "cmake": "#", "r": "#", "julia": "#", "powershell": "#", "terraform": "#",
                "sql": "--", "lua": "--", "haskell": "--", "markdown": None, "html": None, "xml": None, "css": None, "json": None}
# Prettier: language id -> file extension it needs to pick a parser
PRETTIER_EXT = {"js": ".js", "jsx": ".jsx", "typescript": ".ts", "typescript-jsx": ".tsx", "json": ".json", "css": ".css",
                "scss": ".scss", "less": ".less", "html": ".html", "markdown": ".md", "yaml": ".yaml"}
PRETTIER_SUFFIXES = (".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx", ".mts", ".cts", ".json", ".jsonc", ".json5", ".css", ".scss",
                     ".less", ".html", ".htm", ".vue", ".svelte", ".md", ".markdown", ".mdx", ".yaml", ".yml", ".graphql", ".gql",
                     ".hbs", ".handlebars")


# ---- preferences ------------------------------------------------------------
def _prefs_path():
    return os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "fileharbor", "editor.json")


def load_prefs():
    try:
        with open(_prefs_path()) as f:
            return json.load(f)
    except Exception:
        return {}


def save_prefs(p):
    try:
        os.makedirs(os.path.dirname(_prefs_path()), exist_ok=True)
        with open(_prefs_path(), "w") as f:
            json.dump(p, f)
    except OSError:
        pass


# ---- rendering ----------------------------------------------------------------
def render_html(text, theme, mdx=False):
    body_md, head = mdx_convert(text, mdx)
    exts = list(MD_EXTENSIONS)
    cfg = {}
    if mdthemes.HtmlFormatter is not None:
        exts.append("codehilite")
        cfg["codehilite"] = {"guess_lang": False, "css_class": "codehilite"}
    body = md_lib.markdown(body_md, extensions=exts, extension_configs=cfg, output_format="html")
    return "<!doctype html><meta charset='utf-8'><style>%s</style><body>%s%s</body>" % (mdthemes.css_for(theme), head, body)


# ---- prettier -----------------------------------------------------------------
def find_prettier(start_dir):
    d = start_dir
    while d and d != os.path.dirname(d):
        p = os.path.join(d, "node_modules", ".bin", "prettier")
        if os.access(p, os.X_OK):
            return p
        d = os.path.dirname(d)
    for cand in (shutil.which("prettier"), os.path.expanduser("~/.local/bin/prettier"),
                 os.path.expanduser("~/.npm-global/bin/prettier"), "/usr/local/bin/prettier", "/usr/bin/prettier"):
        if cand and os.access(cand, os.X_OK):
            return cand
    return None


def run_prettier(binary, text, filename, cwd):
    """Returns (formatted text, error message). The project's own .prettierrc is found through the file name."""
    try:
        r = subprocess.run([binary, "--stdin-filepath", filename], input=text.encode("utf-8"), capture_output=True,
                           timeout=45, cwd=cwd if cwd and os.path.isdir(cwd) else None)
    except subprocess.TimeoutExpired:
        return None, "Prettier took too long."
    except OSError as e:
        return None, str(e)
    if r.returncode != 0:
        return None, r.stderr.decode("utf-8", "replace").strip() or "Prettier failed."
    return r.stdout.decode("utf-8", "replace"), None


# ---- the page --------------------------------------------------------------------
class EditorPage(ToolPage):
    def __init__(self, path=None, preview_only=False):
        super().__init__()
        self.path = None
        self.dirty = False
        self.encoding = "utf-8"
        self.eol = "\n"
        self._pending = None
        self._loading = False
        self._lang_id = None
        self._font_pt = None
        self.prefs = load_prefs()
        self.theme_id = mdthemes.load_choice()
        self.set_default_size(1100, 760)

        self._build_header()
        self._build_body()
        self.show_all()
        self.goto_entry.hide()

        self.set_title("Untitled")
        self.connect("delete-event", self._on_delete)
        Gtk.Box.connect(self, "destroy", self._recover)
        self.connect("key-press-event", self._on_key)
        if path:
            self._load(os.path.abspath(path))
        else:
            self._after_load()
        self.editor_toggle.set_active(not (preview_only and self.is_markdown()))
        self._apply_layout()
        if self.editor.get_visible():
            self.editor.grab_focus()

    # ---------------------------------------------------------------- UI
    def _build_header(self):
        header = Gtk.HeaderBar(show_close_button=True)
        self.set_titlebar(header)
        open_b = Gtk.Button(label="Open")
        open_b.set_tooltip_text("Open a file (Ctrl+O)")
        open_b.connect("clicked", lambda *_: self._on_open())
        new_b = Gtk.Button.new_from_icon_name("document-new-symbolic", Gtk.IconSize.BUTTON)
        new_b.set_tooltip_text("New file in a new tab (Ctrl+N)")
        new_b.connect("clicked", lambda *_: self._new_tab())
        header.pack_start(open_b)
        header.pack_start(new_b)

        self.save_b = Gtk.Button(label="Save")
        self.save_b.get_style_context().add_class("suggested-action")
        self.save_b.set_tooltip_text("Save (Ctrl+S)")
        self.save_b.connect("clicked", lambda *_: self.save())
        header.pack_end(self.save_b)

        menu_b = Gtk.MenuButton()
        menu_b.add(Gtk.Image.new_from_icon_name("open-menu-symbolic", Gtk.IconSize.BUTTON))
        menu_b.set_popover(self._build_menu())
        header.pack_end(menu_b)

        self.format_b = Gtk.Button(label="Format")
        self.format_b.set_tooltip_text("Format with Prettier (Ctrl+Shift+I)")
        self.format_b.set_no_show_all(True)              # only for files Prettier understands
        self.format_b.connect("clicked", lambda *_: self.format_document())
        header.pack_end(self.format_b)

        self.theme_combo = Gtk.ComboBoxText()
        self.theme_combo.set_tooltip_text("Colour theme (editor and preview)")
        self.theme_combo.append(mdthemes.AUTO, "Follow FileHarbor")
        for tid in mdthemes.ORDER:
            self.theme_combo.append(tid, mdthemes.THEMES[tid]["name"])
        self.theme_combo.set_active_id(self.theme_id)
        self.theme_combo.connect("changed", self._on_theme)
        header.pack_end(self.theme_combo)

        self.editor_toggle = Gtk.ToggleButton(label="Editor")
        self.editor_toggle.set_tooltip_text("Show or hide the editor (Ctrl+E)")
        self.editor_toggle.set_active(True)
        self.editor_toggle.set_no_show_all(True)         # only Markdown has a preview to switch to
        self.editor_toggle.connect("toggled", lambda *_: self._apply_layout())
        header.pack_end(self.editor_toggle)
        self.header = header

    def _build_menu(self):
        pop = Gtk.Popover()
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        box.set_border_width(8)

        def item(label, cb, check=None):
            if check is None:
                b = Gtk.ModelButton(text=label)
                b.connect("clicked", lambda *_: (pop.popdown(), cb()))
            else:
                b = Gtk.ModelButton(text=label, role=Gtk.ButtonRole.CHECK, active=check)
                b.connect("clicked", lambda w: cb(not w.get_property("active")))
            b.set_halign(Gtk.Align.FILL)
            box.pack_start(b, False, False, 0)
            return b

        item("Find…            Ctrl+F", lambda: self.show_search(False))
        item("Replace…        Ctrl+H", lambda: self.show_search(True))
        item("Go to line…    Ctrl+G", self.show_goto)
        box.pack_start(Gtk.Separator(), False, False, 4)
        item("Toggle comment   Ctrl+/", self.toggle_comment)
        item("Duplicate line   Ctrl+Shift+D", self.duplicate_line)
        item("Format with Prettier", self.format_document)
        item("Format when saving", self._set_format_on_save, check=bool(self.prefs.get("format_on_save")))
        box.pack_start(Gtk.Separator(), False, False, 4)
        item("Word wrap", self._set_wrap, check=self.prefs.get("wrap", True))
        item("Minimap", self._set_minimap, check=bool(self.prefs.get("minimap")))
        item("Show whitespace", self._set_whitespace, check=bool(self.prefs.get("whitespace")))
        item("Line numbers", self._set_numbers, check=self.prefs.get("numbers", True))
        box.pack_start(Gtk.Separator(), False, False, 4)
        item("Zoom in   Ctrl++", lambda: self.zoom(1))
        item("Zoom out  Ctrl+-", lambda: self.zoom(-1))
        item("Reset zoom  Ctrl+0", lambda: self.zoom(0))
        box.pack_start(Gtk.Separator(), False, False, 4)
        item("Save as…   Ctrl+Shift+S", lambda: self.save(True))
        box.show_all()
        pop.add(box)
        return pop

    def _build_body(self):
        # notice bar (errors, Prettier messages)
        self.notice_bar = Gtk.InfoBar()
        self.notice_bar.set_show_close_button(True)
        self.notice_label = Gtk.Label(xalign=0)
        self.notice_label.set_line_wrap(True)
        self.notice_label.set_selectable(True)
        self.notice_bar.get_content_area().add(self.notice_label)
        self.notice_bar.connect("response", lambda *_: self.notice_bar.hide())
        self.pack_start(self.notice_bar, False, False, 0)
        self.notice_bar.show_all()
        self.notice_bar.set_no_show_all(True)       # shown only when there is something to say, even after a show_all()
        self.notice_bar.hide()

        # search bar
        self.search_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        self.search_box.set_border_width(6)
        row1 = Gtk.Box(spacing=6)
        self.find_entry = Gtk.Entry(placeholder_text="Find", hexpand=True)
        self.find_entry.connect("changed", self._on_find_changed)
        self.find_entry.connect("activate", lambda *_: self.find_next())
        self.count_label = Gtk.Label(label="")
        self.count_label.get_style_context().add_class("dim-label")
        prev_b = Gtk.Button.new_from_icon_name("go-up-symbolic", Gtk.IconSize.BUTTON)
        next_b = Gtk.Button.new_from_icon_name("go-down-symbolic", Gtk.IconSize.BUTTON)
        prev_b.set_tooltip_text("Previous match (Shift+Enter)")
        next_b.set_tooltip_text("Next match (Enter)")
        prev_b.connect("clicked", lambda *_: self.find_next(True))
        next_b.connect("clicked", lambda *_: self.find_next())
        self.case_b = Gtk.ToggleButton(label="Aa")
        self.case_b.set_tooltip_text("Match case")
        self.word_b = Gtk.ToggleButton(label="W")
        self.word_b.set_tooltip_text("Whole words")
        self.regex_b = Gtk.ToggleButton(label=".*")
        self.regex_b.set_tooltip_text("Regular expression")
        for b in (self.case_b, self.word_b, self.regex_b):
            b.connect("toggled", self._on_find_changed)
        close_b = Gtk.Button.new_from_icon_name("window-close-symbolic", Gtk.IconSize.BUTTON)
        close_b.connect("clicked", lambda *_: self.hide_search())
        for w in (self.find_entry, self.count_label, prev_b, next_b, self.case_b, self.word_b, self.regex_b, close_b):
            row1.pack_start(w, w is self.find_entry, w is self.find_entry, 0)
        self.replace_row = Gtk.Box(spacing=6)
        self.replace_entry = Gtk.Entry(placeholder_text="Replace with", hexpand=True)
        self.replace_entry.connect("activate", lambda *_: self.replace_one())
        rep_b = Gtk.Button(label="Replace")
        rep_b.connect("clicked", lambda *_: self.replace_one())
        all_b = Gtk.Button(label="Replace all")
        all_b.connect("clicked", lambda *_: self.replace_all())
        self.replace_row.pack_start(self.replace_entry, True, True, 0)
        self.replace_row.pack_start(rep_b, False, False, 0)
        self.replace_row.pack_start(all_b, False, False, 0)
        self.search_box.pack_start(row1, False, False, 0)
        self.search_box.pack_start(self.replace_row, False, False, 0)
        self.pack_start(self.search_box, False, False, 0)
        self.search_box.show_all()
        self.search_box.set_no_show_all(True)
        self.search_box.hide()

        # buffer / view
        self.buf = GtkSource.Buffer()
        self.buf.set_highlight_syntax(True)
        self.buf.set_highlight_matching_brackets(True)
        self.buf.set_max_undo_levels(-1)
        self.editor = GtkSource.View(buffer=self.buf, monospace=True, show_line_numbers=self.prefs.get("numbers", True),
                                     highlight_current_line=True, auto_indent=True, indent_on_tab=True,
                                     smart_home_end=GtkSource.SmartHomeEndType.BEFORE, show_right_margin=False,
                                     tab_width=4, insert_spaces_instead_of_tabs=True,
                                     top_margin=8, bottom_margin=8, left_margin=8, right_margin=8)
        self.editor.set_wrap_mode(Gtk.WrapMode.WORD_CHAR if self.prefs.get("wrap", True) else Gtk.WrapMode.NONE)
        self.editor.set_indent_width(-1)
        edschemes.install(GtkSource.StyleSchemeManager.get_default())
        self._apply_editor_scheme()
        self._font_css = Gtk.CssProvider()
        self.editor.get_style_context().add_provider(self._font_css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        if self.prefs.get("font_pt"):
            self._font_pt = int(self.prefs["font_pt"])
            self._apply_font()
        self._set_whitespace(bool(self.prefs.get("whitespace")), save=False)

        self.search_settings = GtkSource.SearchSettings(wrap_around=True)
        self.search_ctx = GtkSource.SearchContext.new(self.buf, self.search_settings)
        self.search_ctx.set_highlight(True)
        self.search_ctx.connect("notify::occurrences-count", lambda *_: self._update_count())

        sw = Gtk.ScrolledWindow()
        sw.add(self.editor)
        self.minimap = GtkSource.Map(view=self.editor)
        self.minimap.set_no_show_all(True)
        self.editor_box = Gtk.Box()
        self.editor_box.pack_start(sw, True, True, 0)
        self.editor_box.pack_start(self.minimap, False, False, 0)
        self.editor_box.show_all()
        self.editor_box.set_no_show_all(True)       # layout decides, a later show_all() must not undo it
        self.minimap.set_visible(bool(self.prefs.get("minimap")))

        settings = WebKit2.Settings(enable_javascript=False, enable_plugins=False, enable_developer_extras=False)
        self.web = WebKit2.WebView.new_with_settings(settings)
        self.web.set_no_show_all(True)
        self.web.connect("decide-policy", self._on_policy)

        self.paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        self.paned.pack1(self.editor_box, True, False)
        self.paned.pack2(self.web, True, False)
        self.pack_start(self.paned, True, True, 0)

        # status bar
        bar = Gtk.Box(spacing=12)
        bar.set_border_width(4)
        self.pos_label = Gtk.Label(label="Ln 1, Col 1")
        self.goto_entry = Gtk.Entry(placeholder_text="line", width_chars=6, max_width_chars=8)
        self.goto_entry.set_no_show_all(True)
        self.goto_entry.connect("activate", self._on_goto)
        self.goto_entry.connect("focus-out-event", lambda *_: self.goto_entry.hide() or False)
        self.info_label = Gtk.Label(label="")
        self.info_label.get_style_context().add_class("dim-label")
        self.lang_combo = Gtk.ComboBoxText()
        self.lang_combo.append("", "Plain text")
        langs = []
        lm = GtkSource.LanguageManager.get_default()
        for lid in lm.get_language_ids():
            lang = lm.get_language(lid)
            if lang and not lang.get_hidden():
                langs.append((lang.get_name(), lid))
        for name, lid in sorted(langs, key=lambda x: x[0].lower()):
            self.lang_combo.append(lid, name)
        self.lang_combo.set_tooltip_text("Language")
        self.lang_combo.connect("changed", self._on_lang_changed)
        self.tab_combo = Gtk.ComboBoxText()
        for label in ("Spaces: 2", "Spaces: 4", "Spaces: 8", "Tabs: 4", "Tabs: 8"):
            self.tab_combo.append(label, label)
        self.tab_combo.set_active_id("Spaces: 4")
        self.tab_combo.set_tooltip_text("Indentation")
        self.tab_combo.connect("changed", self._on_indent_changed)
        bar.pack_start(self.pos_label, False, False, 0)
        bar.pack_start(self.goto_entry, False, False, 0)
        bar.pack_start(self.info_label, False, False, 0)
        bar.pack_end(self.lang_combo, False, False, 0)
        bar.pack_end(self.tab_combo, False, False, 0)
        self.pack_start(bar, False, False, 0)

        self.buf.connect("changed", self._on_changed)
        self.buf.connect("notify::cursor-position", self._update_pos)
        self.buf.connect("notify::language", self._on_buffer_language)

    # --------------------------------------------------------- state / helpers
    def _theme(self):
        return mdthemes.get(self.theme_id, is_dark_theme())

    def _apply_editor_scheme(self):
        mgr = GtkSource.StyleSchemeManager.get_default()
        scheme = mgr.get_scheme(edschemes.scheme_id(self.theme_id, is_dark_theme())) or mgr.get_scheme("classic")
        if scheme:
            self.buf.set_style_scheme(scheme)

    def _on_theme(self, combo):
        self.theme_id = combo.get_active_id() or mdthemes.AUTO
        mdthemes.save_choice(self.theme_id)
        self._apply_editor_scheme()
        self._render()

    def is_markdown(self):
        return self._lang_id == "markdown" or (bool(self.path) and self.path.lower().endswith(MD_SUFFIXES))

    def _is_mdx(self):
        return bool(self.path) and self.path.lower().endswith(".mdx")

    def _text(self):
        return self.buf.get_text(self.buf.get_start_iter(), self.buf.get_end_iter(), False)

    def notify(self, text, error=False):
        self.notice_label.set_text(text)
        self.notice_bar.set_message_type(Gtk.MessageType.ERROR if error else Gtk.MessageType.INFO)
        self.notice_bar.show()
        if not error:
            GLib.timeout_add_seconds(6, lambda: (self._alive and self.notice_label.get_text() == text and self.notice_bar.hide()) and False)

    def _apply_layout(self):
        md = self.is_markdown()
        show_editor = self.editor_toggle.get_active() or not md
        self.editor_toggle.set_visible(md)
        self.theme_combo.set_visible(True)
        self.editor_box.set_visible(show_editor)
        self.web.set_visible(md)
        self.format_b.set_visible(self._prettier_supported())
        if md:
            self._render()
            if show_editor:
                GLib.idle_add(self._place_paned)
        if show_editor and self.editor.get_realized():
            self.editor.grab_focus()

    def _place_paned(self):
        w = self.paned.get_allocated_width()
        if w > 100:
            self.paned.set_position(w // 2)
        return False

    def _prettier_supported(self):
        if self.path:
            return self.path.lower().endswith(PRETTIER_SUFFIXES)
        return self._lang_id in PRETTIER_EXT

    def _update_title(self):
        name = os.path.basename(self.path) if self.path else "Untitled"
        self.set_title(("• " if self.dirty else "") + name)

    def _update_pos(self, *_):
        it = self.buf.get_iter_at_mark(self.buf.get_insert())
        self.pos_label.set_text("Ln %d, Col %d" % (it.get_line() + 1, it.get_line_offset() + 1))

    def _update_info(self):
        self.info_label.set_text("%d lines   %s   %s" % (self.buf.get_line_count(), self.encoding.upper(),
                                                          {"\n": "LF", "\r\n": "CRLF", "\r": "CR"}[self.eol]))

    # ------------------------------------------------------- language / indentation
    def guess_language(self, path, text):
        base = os.path.basename(path).lower() if path else ""
        ext = os.path.splitext(base)[1]
        lid = LANG_BY_NAME.get(base)
        if lid is None and ext in LANG_BY_EXT:
            lid = LANG_BY_EXT[ext]
            if lid is None:
                return None
        if lid:
            return lid
        lm = GtkSource.LanguageManager.get_default()
        ctype = Gio.content_type_guess(base or None, text[:2000].encode("utf-8", "ignore"))[0]
        lang = lm.guess_language(base or None, ctype)
        if lang is None and text.startswith("#!"):
            first = text.split("\n", 1)[0]
            for key, l in (("python", "python3"), ("bash", "sh"), ("sh", "sh"), ("node", "js"), ("ruby", "ruby"), ("perl", "perl")):
                if re.search(r"\b" + key + r"\b", first):
                    return l
        return lang.get_id() if lang else None

    def set_language(self, lid):
        lm = GtkSource.LanguageManager.get_default()
        self._lang_id = lid
        self.buf.set_language(lm.get_language(lid) if lid else None)
        self.lang_combo.handler_block_by_func(self._on_lang_changed)
        self.lang_combo.set_active_id(lid or "")
        self.lang_combo.handler_unblock_by_func(self._on_lang_changed)

    def _on_buffer_language(self, *_):
        pass

    def _on_lang_changed(self, combo):
        lid = combo.get_active_id() or None
        self._lang_id = lid
        self.buf.set_language(GtkSource.LanguageManager.get_default().get_language(lid) if lid else None)
        self._apply_layout()

    def detect_indent(self, text, lid):
        tabs = spaces = 0
        steps = {}
        prev = 0
        for ln in text.split("\n")[:800]:
            if not ln.strip():
                continue
            if ln.startswith("\t"):
                tabs += 1
            elif ln.startswith("  "):
                n = len(ln) - len(ln.lstrip(" "))
                spaces += 1
                d = abs(n - prev)
                if d in (2, 3, 4, 8):
                    steps[d] = steps.get(d, 0) + 1
                prev = n
            else:
                prev = 0
        if tabs > spaces or (tabs == spaces == 0 and lid in TABS_LANGS):
            return "Tabs: 8" if lid == "makefile" and False else "Tabs: 4"
        if steps:
            best = max(steps, key=steps.get)
            return {2: "Spaces: 2", 4: "Spaces: 4", 8: "Spaces: 8"}.get(best, "Spaces: 4")
        return "Spaces: 4" if lid not in ("yaml", "json", "js", "jsx", "typescript", "typescript-jsx", "html", "xml", "css", "scss",
                                         "less", "ruby", "markdown") else "Spaces: 2"

    def _on_indent_changed(self, combo):
        label = combo.get_active_id() or "Spaces: 4"
        kind, n = label.split(": ")
        self.editor.set_insert_spaces_instead_of_tabs(kind == "Spaces")
        self.editor.set_tab_width(int(n))

    # --------------------------------------------------------------- loading
    def _load(self, path):
        try:
            if os.path.getsize(path) > MAX_BYTES:
                raise ToolFailed("This file is larger than 30 MB. Open it with a viewer instead.")
            with open(path, "rb") as f:
                raw = f.read()
        except OSError as e:
            raise ToolFailed("Cannot open %s: %s" % (os.path.basename(path), e.strerror or e))
        if b"\x00" in raw[:8192] and not raw.startswith((b"\xff\xfe", b"\xfe\xff")):
            raise ToolFailed("%s looks like a binary file, not text." % os.path.basename(path))
        enc = "utf-8"
        try:
            text = raw.decode("utf-8-sig" if raw.startswith(b"\xef\xbb\xbf") else "utf-8")
            if raw.startswith(b"\xef\xbb\xbf"):
                enc = "utf-8-sig"
        except UnicodeDecodeError:
            if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
                text, enc = raw.decode("utf-16"), "utf-16"
            else:
                text, enc = raw.decode("latin-1"), "latin-1"
        eol = "\r\n" if "\r\n" in text else ("\r" if "\r" in text and "\n" not in text else "\n")
        if eol != "\n":
            text = text.replace("\r\n", "\n").replace("\r", "\n")
        self.path, self.encoding, self.eol = path, enc, eol
        self._loading = True
        self.buf.begin_not_undoable_action()
        self.buf.set_text(text)
        self.buf.end_not_undoable_action()
        self.buf.place_cursor(self.buf.get_start_iter())
        self._loading = False
        self._after_load(text)

    def _after_load(self, text=""):
        self.dirty = False
        lid = self.guess_language(self.path, text) if (self.path or text) else None
        self.set_language(lid)
        self.tab_combo.set_active_id(self.detect_indent(text, lid))
        self._update_title()
        self._update_pos()
        self._update_info()
        self.format_b.set_visible(self._prettier_supported())

    def _on_changed(self, *_):
        if self._loading:
            return
        self.dirty = True
        self._update_title()
        self._update_info()
        if self.web.get_visible():
            if self._pending:
                GLib.source_remove(self._pending)
            self._pending = GLib.timeout_add(250, self._render)

    # --------------------------------------------------------------- preview
    def _render(self):
        self._pending = None
        if not self._alive or not self.is_markdown():
            return False
        base = "file://" + (os.path.dirname(os.path.abspath(self.path)) + "/" if self.path else "/")
        self.web.load_html(render_html(self._text(), self._theme(), self._is_mdx()), base)
        return False

    def _on_policy(self, view, decision, dtype):
        if dtype == WebKit2.PolicyDecisionType.NAVIGATION_ACTION:
            uri = decision.get_navigation_action().get_request().get_uri()
            if uri.startswith(("http://", "https://", "mailto:")):
                Gtk.show_uri_on_window(self.win(), uri, 0)
                decision.ignore()
                return True
            if uri.startswith("file://") and not uri.endswith("/") and "#" not in uri.rsplit("/", 1)[-1]:
                if uri.lower().endswith(MD_SUFFIXES):
                    decision.ignore()
                    self.open_path(GLib.filename_from_uri(uri)[0], preview=True)
                    return True
        return False

    # ----------------------------------------------------------- files
    def _blank(self):
        return self.path is None and not self.dirty and self.buf.get_char_count() == 0

    def open_path(self, path, preview=False):
        if self._blank():
            try:
                self._load(path)
            except ToolFailed as e:
                message(self, "Cannot open this", str(e), Gtk.MessageType.ERROR)
                return
            self._apply_layout()
            return
        tool = "preview" if preview else "editor"
        if not self.open_uri("x-fileharbor-tool:///%s?f=%s" % (tool, GLib.uri_escape_string(path, None, False)), True):
            if self._confirm_discard():
                try:
                    self._load(path)
                except ToolFailed as e:
                    message(self, "Cannot open this", str(e), Gtk.MessageType.ERROR)
                    return
                self._apply_layout()

    def _on_open(self):
        d = Gtk.FileChooserDialog(title="Open file", transient_for=self.win(), action=Gtk.FileChooserAction.OPEN)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Open", Gtk.ResponseType.ACCEPT)
        if self.path:
            d.set_current_folder(os.path.dirname(self.path))
        if d.run() == Gtk.ResponseType.ACCEPT:
            p = d.get_filename()
            d.destroy()
            self.open_path(p)
        else:
            d.destroy()

    def _new_tab(self):
        self.open_uri("x-fileharbor-tool:///editor", True)

    def save(self, force_dialog=False):
        if self.prefs.get("format_on_save") and self._prettier_supported() and not force_dialog:
            self.format_document(then_save=True)
            return True
        return self._write(force_dialog)

    def _write(self, force_dialog=False):
        if self.path is None or force_dialog:
            d = Gtk.FileChooserDialog(title="Save file", transient_for=self.win(), action=Gtk.FileChooserAction.SAVE)
            d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Save", Gtk.ResponseType.ACCEPT)
            d.set_do_overwrite_confirmation(True)
            d.set_current_name(os.path.basename(self.path) if self.path else "untitled.txt")
            if self.path:
                d.set_current_folder(os.path.dirname(self.path))
            if d.run() != Gtk.ResponseType.ACCEPT:
                d.destroy()
                return False
            self.path = d.get_filename()
            d.destroy()
            if self._lang_id is None:
                self.set_language(self.guess_language(self.path, self._text()))
        text = self._text()
        if text and not text.endswith("\n") and self.prefs.get("final_newline", True) and self.path and \
                not self.path.lower().endswith((".txt", ".csv")):
            text += "\n"
        if self.eol != "\n":
            text = text.replace("\n", self.eol)
        try:
            mode = os.stat(self.path).st_mode & 0o7777 if os.path.exists(self.path) else None
            tmp = self.path + ".fh-tmp"
            with open(tmp, "wb") as f:
                f.write(text.encode(self.encoding if self.encoding != "utf-16" else "utf-16", "replace"))
            if mode is not None:
                os.chmod(tmp, mode)
            os.replace(tmp, self.path)           # never leave a half-written file
        except OSError as e:
            message(self, "Could not save", str(e), Gtk.MessageType.ERROR)
            return False
        self.dirty = False
        self._update_title()
        self._apply_layout()
        return True

    def _confirm_discard(self):
        if not self.dirty:
            return True
        if ask(self, "Discard unsaved changes?", "Your edits to this file have not been saved.", "_Discard"):
            self.dirty = False
            return True
        return False

    def _recover(self, *_):
        """Closing a tab cannot ask, so unsaved text is kept in a recovery copy instead."""
        if self.dirty:
            text = self._text()
            keep_recovery_copy(os.path.basename(self.path) if self.path else "untitled.txt",
                               lambda p: open(p, "w", encoding="utf-8").write(text))

    def _on_delete(self, *_):
        return not self._confirm_discard()

    # ---------------------------------------------------------- editing commands
    def _line_range(self):
        b = self.buf
        if b.get_has_selection():
            s, e = b.get_selection_bounds()
            if e.starts_line() and e.get_offset() > s.get_offset():
                e.backward_char()
        else:
            s = e = b.get_iter_at_mark(b.get_insert())
        s.set_line_offset(0)
        if not e.ends_line():
            e.forward_to_line_end()
        return s, e

    def toggle_comment(self):
        prefix = LINE_COMMENT.get(self._lang_id)
        lang = self.buf.get_language()
        if prefix is None and lang is not None:
            prefix = lang.get_metadata("line-comment-start")
        if not prefix:
            self.notify("This language has no line comments.")
            return
        s, e = self._line_range()
        first, last = s.get_line(), e.get_line()
        lines = [self.buf.get_iter_at_line(n) for n in range(first, last + 1)]

        def text_of(it):
            end = it.copy()
            if not end.ends_line():
                end.forward_to_line_end()
            return it.get_text(end)

        body = [text_of(it) for it in lines]
        nonblank = [t for t in body if t.strip()]
        uncomment = bool(nonblank) and all(t.lstrip().startswith(prefix) for t in nonblank)
        self.buf.begin_user_action()
        for n in range(last, first - 1, -1):
            it = self.buf.get_iter_at_line(n)
            t = text_of(it)
            if not t.strip():
                continue
            indent = len(t) - len(t.lstrip())
            at = self.buf.get_iter_at_line_offset(n, indent)
            if uncomment:
                end = self.buf.get_iter_at_line_offset(n, indent + len(prefix) + (1 if t[indent + len(prefix):indent + len(prefix) + 1] == " " else 0))
                self.buf.delete(at, end)
            else:
                self.buf.insert(at, prefix + " ")
        self.buf.end_user_action()

    def duplicate_line(self):
        s, e = self._line_range()
        text = s.get_text(e)
        self.buf.begin_user_action()
        e2 = e.copy()
        self.buf.insert(e2, "\n" + text)
        self.buf.end_user_action()

    def zoom(self, direction):
        cur = self._font_pt or 11
        self._font_pt = 11 if direction == 0 else max(6, min(40, cur + direction))
        self.prefs["font_pt"] = None if direction == 0 else self._font_pt
        save_prefs(self.prefs)
        if direction == 0:
            self._font_pt = None
        self._apply_font()

    def _apply_font(self):
        css = "textview { font-size: %dpt; }" % self._font_pt if self._font_pt else ""
        self._font_css.load_from_data(css.encode())

    def _set_wrap(self, on):
        self.prefs["wrap"] = on
        save_prefs(self.prefs)
        self.editor.set_wrap_mode(Gtk.WrapMode.WORD_CHAR if on else Gtk.WrapMode.NONE)

    def _set_minimap(self, on):
        self.prefs["minimap"] = on
        save_prefs(self.prefs)
        self.minimap.set_visible(on)

    def _set_numbers(self, on):
        self.prefs["numbers"] = on
        save_prefs(self.prefs)
        self.editor.set_show_line_numbers(on)

    def _set_whitespace(self, on, save=True):
        if save:
            self.prefs["whitespace"] = on
            save_prefs(self.prefs)
        sd = self.editor.get_space_drawer()
        sd.set_types_for_locations(GtkSource.SpaceLocationFlags.ALL,
                                   (GtkSource.SpaceTypeFlags.SPACE | GtkSource.SpaceTypeFlags.TAB | GtkSource.SpaceTypeFlags.NBSP))
        sd.set_enable_matrix(on)

    def _set_format_on_save(self, on):
        self.prefs["format_on_save"] = on
        save_prefs(self.prefs)

    # ------------------------------------------------------------ search
    def show_search(self, replace):
        self.search_box.show()
        self.replace_row.set_visible(replace)
        if self.buf.get_has_selection():
            s, e = self.buf.get_selection_bounds()
            t = s.get_text(e)
            if "\n" not in t and t:
                self.find_entry.set_text(t)
        self.find_entry.grab_focus()
        self.find_entry.select_region(0, -1)
        self._on_find_changed()

    def hide_search(self):
        self.search_box.hide()
        self.search_settings.set_search_text(None)
        self.editor.grab_focus()

    def _on_find_changed(self, *_):
        st = self.search_settings
        st.set_case_sensitive(self.case_b.get_active())
        st.set_at_word_boundaries(self.word_b.get_active())
        st.set_regex_enabled(self.regex_b.get_active())
        st.set_search_text(self.find_entry.get_text() or None)
        self._update_count()
        if self.find_entry.get_text():
            self.find_next(from_start=True)

    def _update_count(self):
        n = self.search_ctx.get_occurrences_count()
        if not self.find_entry.get_text():
            self.count_label.set_text("")
        elif n < 0:
            self.count_label.set_text("…")
        else:
            self.count_label.set_text("%d match%s" % (n, "" if n == 1 else "es"))

    def find_next(self, backward=False, from_start=False):
        if not self.find_entry.get_text():
            return
        b = self.buf
        if b.get_has_selection():
            s, e = b.get_selection_bounds()
            it = s if (backward or from_start) else e
        else:
            it = b.get_iter_at_mark(b.get_insert())
        if from_start and b.get_has_selection():
            it = b.get_selection_bounds()[0]
        res = self.search_ctx.backward(it) if backward else self.search_ctx.forward(it)
        if res[0]:
            b.select_range(res[1], res[2])
            self.editor.scroll_to_iter(res[1], 0.15, False, 0, 0)

    def replace_one(self):
        b = self.buf
        if b.get_has_selection():
            s, e = b.get_selection_bounds()
            try:
                self.search_ctx.replace(s, e, self.replace_entry.get_text(), -1)
            except GLib.Error as err:
                self.notify(err.message, True)
                return
        self.find_next()

    def replace_all(self):
        try:
            n = self.search_ctx.replace_all(self.replace_entry.get_text(), -1)
        except GLib.Error as err:
            self.notify(err.message, True)
            return
        self.notify("Replaced %d match%s." % (n, "" if n == 1 else "es"))

    def show_goto(self):
        self.goto_entry.show()
        self.goto_entry.set_text("")
        self.goto_entry.grab_focus()

    def _on_goto(self, entry):
        try:
            n = int(entry.get_text().strip().split(":")[0])
        except ValueError:
            return
        n = max(1, min(n, self.buf.get_line_count()))
        it = self.buf.get_iter_at_line(n - 1)
        self.buf.place_cursor(it)
        self.editor.scroll_to_iter(it, 0.2, True, 0, 0.4)
        entry.hide()
        self.editor.grab_focus()

    # --------------------------------------------------------- prettier
    def format_document(self, then_save=False):
        binary = find_prettier(os.path.dirname(self.path) if self.path else None)
        if binary is None:
            self.notify("Prettier is not installed. Install it with  sudo pacman -S prettier  (or  npm install -g prettier), "
                        "then try again.", True)
            if then_save:
                self._write()
            return
        if not self._prettier_supported():
            self.notify("Prettier does not format this kind of file.")
            if then_save:
                self._write()
            return
        name = os.path.basename(self.path) if self.path else "untitled" + PRETTIER_EXT.get(self._lang_id, ".txt")
        cwd = os.path.dirname(self.path) if self.path else None
        text = self._text()
        self.format_b.set_sensitive(False)

        def work():
            out, err = run_prettier(binary, text, name, cwd)
            GLib.idle_add(done, out, err)

        def done(out, err):
            if not self._alive:
                return False
            self.format_b.set_sensitive(True)
            if err:
                self.notify(err, True)
            elif out is not None and out != self._text():
                it = self.buf.get_iter_at_mark(self.buf.get_insert())
                line = it.get_line()
                self.buf.begin_user_action()
                self.buf.set_text(out)
                self.buf.end_user_action()
                self.buf.place_cursor(self.buf.get_iter_at_line(min(line, self.buf.get_line_count() - 1)))
                self.notify("Formatted with Prettier.")
            else:
                self.notify("Already formatted.")
            if then_save and not err:
                self._write()
            return False

        threading.Thread(target=work, daemon=True).start()

    # ------------------------------------------------------------- keys
    def _on_key(self, w, ev):
        ctrl = bool(ev.state & Gdk.ModifierType.CONTROL_MASK)
        shift = bool(ev.state & Gdk.ModifierType.SHIFT_MASK)
        k = Gdk.keyval_name(ev.keyval) or ""
        if k == "Escape":
            if self.search_box.get_visible():
                self.hide_search()
                return True
            return False
        if not ctrl:
            return False
        kl = k.lower()
        if kl == "s":
            self.save(shift)
        elif kl == "o":
            self._on_open()
        elif kl == "n":
            self._new_tab()
        elif kl == "f":
            self.show_search(False)
        elif kl == "h":
            self.show_search(True)
        elif kl == "g":
            self.show_goto()
        elif kl == "i" and shift:
            self.format_document()
        elif kl == "d" and shift:
            self.duplicate_line()
        elif k in ("slash", "numbersign"):
            self.toggle_comment()
        elif kl == "e" and self.editor_toggle.get_visible():
            self.editor_toggle.set_active(not self.editor_toggle.get_active())
        elif k in ("plus", "equal", "KP_Add"):
            self.zoom(1)
        elif k in ("minus", "KP_Subtract"):
            self.zoom(-1)
        elif k in ("0", "KP_0"):
            self.zoom(0)
        elif kl == "return" and self.search_box.get_visible():
            return False
        else:
            return False
        return True


def run(args, preview=False):
    path = next((a for a in args if not a.startswith("--")), None)
    preview = preview or "--mode=view" in args or "--preview" in args
    if path and not os.path.exists(path):
        raise ToolFailed("%s does not exist." % path)
    if path and os.path.isdir(path):
        raise ToolFailed("%s is a folder." % path)
    return EditorPage(os.path.abspath(path) if path else None, preview)
