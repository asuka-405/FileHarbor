"""Developer tools window (a small DevToys-style toolbox)."""
import os
import gi

gi.require_version("Gtk", "3.0")
gi.require_version("GtkSource", "4")
from gi.repository import Gtk, GtkSource, GLib, Gio  # noqa: E402

from . import devlogic as L
from .base import ToolPage
from .common import copy_to_clipboard


class Opt:
    def __init__(self, key, label, kind, default, choices=None, lo=0, hi=100):
        self.key, self.label, self.kind, self.default = key, label, kind, default
        self.choices, self.lo, self.hi = choices or [], lo, hi


class Tool:
    def __init__(self, name, group, desc, fn, inputs=("Input",), opts=(), output="Output",
                 placeholder="", generate=False):
        self.name, self.group, self.desc, self.fn = name, group, desc, fn
        self.inputs, self.opts, self.output_label = list(inputs), list(opts), output
        self.placeholder, self.generate = placeholder, generate


def _tools():
    t = []
    add = t.append
    add(Tool("JSON format", "Formatters", "Pretty-print or validate JSON.",
             lambda i, o: L.json_format(i[0], o["indent"], o["sort"]),
             opts=[Opt("indent", "Indent", "choice", "2", ["2", "4", "8"]),
                   Opt("sort", "Sort keys", "bool", False)]))
    add(Tool("JSON minify", "Formatters", "Remove all whitespace from JSON.", lambda i, o: L.json_minify(i[0])))
    add(Tool("JSON ⇄ YAML", "Formatters", "Convert between JSON and YAML.",
             lambda i, o: L.json_to_yaml(i[0]) if o["dir"] == "JSON → YAML" else L.yaml_to_json(i[0]),
             opts=[Opt("dir", "Direction", "choice", "JSON → YAML", ["JSON → YAML", "YAML → JSON"])]))
    add(Tool("Base64", "Encoders", "Encode or decode Base64.",
             lambda i, o: (L.base64_encode(i[0], o["url"]) if o["dir"] == "Encode" else L.base64_decode(i[0], o["url"])),
             opts=[Opt("dir", "Mode", "choice", "Encode", ["Encode", "Decode"]),
                   Opt("url", "URL-safe", "bool", False)]))
    add(Tool("URL encode", "Encoders", "Percent-encode or decode text for URLs.",
             lambda i, o: (L.url_encode(i[0], o["full"]) if o["dir"] == "Encode" else L.url_decode(i[0])),
             opts=[Opt("dir", "Mode", "choice", "Encode", ["Encode", "Decode"]),
                   Opt("full", "Keep URL characters", "bool", False)]))
    add(Tool("HTML entities", "Encoders", "Escape or unescape HTML.",
             lambda i, o: (L.html_encode(i[0]) if o["dir"] == "Escape" else L.html_decode(i[0])),
             opts=[Opt("dir", "Mode", "choice", "Escape", ["Escape", "Unescape"])]))
    add(Tool("Hex", "Encoders", "Text to hex bytes and back.",
             lambda i, o: (L.hex_encode(i[0]) if o["dir"] == "Encode" else L.hex_decode(i[0])),
             opts=[Opt("dir", "Mode", "choice", "Encode", ["Encode", "Decode"])]))
    add(Tool("String escape", "Encoders", "Escape text for JSON, regex or a shell.",
             lambda i, o: L.escape_string(i[0], {"JSON": "json", "Un-JSON": "unjson", "Regex": "regex", "Shell": "shell"}[o["mode"]]),
             opts=[Opt("mode", "Mode", "choice", "JSON", ["JSON", "Un-JSON", "Regex", "Shell"])]))
    add(Tool("Hashes", "Generators", "MD5, SHA-1, SHA-256, SHA-512 and more. Paste a file path to hash a file.",
             lambda i, o: (L.hash_file(i[0].strip()) if "\n" not in i[0].strip() and os.path.isfile(i[0].strip()) else L.hashes(i[0]))))
    add(Tool("UUID", "Generators", "Random UUIDs.",
             lambda i, o: L.uuids(o["count"], o["ver"], o["upper"]), inputs=[], generate=True,
             opts=[Opt("count", "How many", "int", 5, lo=1, hi=500),
                   Opt("ver", "Version", "choice", "4", ["4", "1"]), Opt("upper", "Uppercase", "bool", False)]))
    add(Tool("ULID", "Generators", "Sortable unique ids.", lambda i, o: L.ulids(o["count"]), inputs=[], generate=True,
             opts=[Opt("count", "How many", "int", 5, lo=1, hi=500)]))
    add(Tool("Password", "Generators", "Strong random passwords.",
             lambda i, o: L.password(o["len"], o["lower"], o["upper"], o["digits"], o["symbols"], o["count"]),
             inputs=[], generate=True,
             opts=[Opt("len", "Length", "int", 20, lo=4, hi=128), Opt("count", "How many", "int", 5, lo=1, hi=50),
                   Opt("lower", "a-z", "bool", True), Opt("upper", "A-Z", "bool", True),
                   Opt("digits", "0-9", "bool", True), Opt("symbols", "Symbols", "bool", True)]))
    add(Tool("Lorem ipsum", "Generators", "Placeholder text.",
             lambda i, o: L.lorem(o["paras"], o["sents"]), inputs=[], generate=True,
             opts=[Opt("paras", "Paragraphs", "int", 3, lo=1, hi=50), Opt("sents", "Sentences each", "int", 5, lo=1, hi=30)]))
    add(Tool("Timestamp", "Converters", "Unix time ⇄ readable dates. Type 'now' for the current time.",
             lambda i, o: L.timestamp(i[0]), placeholder="1700000000  or  2024-05-01 12:30:00  or  now"))
    add(Tool("Number base", "Converters", "Decimal, hex, binary and octal.", lambda i, o: L.number_base(i[0]),
             placeholder="255  or  0xFF  or  0b1111"))
    add(Tool("Color", "Converters", "HEX, RGB, HSL and HSV with contrast ratios.", lambda i, o: L.color_convert(i[0]),
             placeholder="#ff9933  or  rgb(255,153,51)  or  hsl(30,100%,60%)"))
    add(Tool("Case", "Text", "snake_case, camelCase, kebab-case and more.",
             lambda i, o: L.case_convert(i[0], o["style"]),
             opts=[Opt("style", "Style", "choice", "snake_case",
                       ["snake_case", "kebab-case", "camelCase", "PascalCase", "CONSTANT_CASE", "Title Case", "UPPER", "lower"])]))
    add(Tool("Text statistics", "Text", "Characters, words, lines and reading time.", lambda i, o: L.text_stats(i[0])))
    add(Tool("Sort lines", "Text", "Sort, reverse and de-duplicate lines.",
             lambda i, o: L.sort_lines(i[0], o["order"], o["uniq"]),
             opts=[Opt("order", "Order", "choice", "a-z", ["a-z", "z-a", "length", "reverse"]),
                   Opt("uniq", "Remove duplicates", "bool", False)]))
    add(Tool("Text diff", "Text", "Compare two texts.", lambda i, o: L.text_diff(i[0], i[1]),
             inputs=["Left", "Right"]))
    add(Tool("Regex tester", "Web", "Test a regular expression.",
             lambda i, o: L.regex_test(i[0].strip("\n"), i[1], o["i"], o["m"], o["s"]),
             inputs=["Pattern", "Text"],
             opts=[Opt("i", "Ignore case", "bool", False), Opt("m", "Multiline", "bool", True), Opt("s", "Dot matches newline", "bool", False)]))
    add(Tool("JWT decoder", "Web", "Read a JSON Web Token (does not verify it).", lambda i, o: L.jwt_decode(i[0])))
    add(Tool("Cron", "Web", "Explain a cron schedule.", lambda i, o: L.cron_explain(i[0]), placeholder="*/5 0 * * 1-5"))
    add(Tool("Compression", "Web", "How small does it deflate?", lambda i, o: L.gzip_size(i[0])))
    return t


TOOLS = _tools()


def make_view(editable, language=None):
    buf = GtkSource.Buffer()
    view = GtkSource.View(buffer=buf, monospace=True, editable=editable, wrap_mode=Gtk.WrapMode.WORD_CHAR,
                          top_margin=6, bottom_margin=6, left_margin=8, right_margin=8)
    sw = Gtk.ScrolledWindow()
    sw.set_shadow_type(Gtk.ShadowType.IN)
    sw.set_hexpand(True)
    sw.set_vexpand(True)
    sw.add(view)
    return view, buf, sw


class DevToolsPage(ToolPage):
    def __init__(self, start=None):
        super().__init__()
        self.set_title("Developer tools")
        self.set_default_size(980, 640)
        self.state = {}               # per tool: widgets and saved text
        self.current = None
        self._pending = None

        paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        self.add(paned)

        # left: searchable list grouped by category
        left = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        left.set_border_width(8)
        search = Gtk.SearchEntry(placeholder_text="Search tools")
        left.pack_start(search, False, False, 0)
        self.store = Gtk.ListStore(str, str, int)    # label, group, tool index
        self.filter = self.store.filter_new()
        self.filter.set_visible_func(self._visible, search)
        self.listview = Gtk.TreeView(model=self.filter, headers_visible=False)
        self.listview.append_column(Gtk.TreeViewColumn("", Gtk.CellRendererText(), text=0))
        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        sw.add(self.listview)
        sw.set_size_request(200, -1)
        left.pack_start(sw, True, True, 0)
        paned.pack1(left, False, False)
        search.connect("search-changed", lambda *_: self.filter.refilter())
        self.listview.get_selection().connect("changed", self._on_select)

        self.stack = Gtk.Stack()
        paned.pack2(self.stack, True, False)
        paned.set_position(220)

        order = {}
        for idx, t in enumerate(TOOLS):
            order.setdefault(t.group, []).append(idx)
        for group, idxs in order.items():
            for idx in idxs:
                self.store.append([TOOLS[idx].name, group, idx])
        self.show_all()
        # select the requested tool, else the first
        target = 0
        if start:
            for i, t in enumerate(TOOLS):
                if t.name.lower() == start.lower():
                    target = i
        for row in self.filter:
            if row[2] == target:
                self.listview.get_selection().select_iter(row.iter)
                break

    @staticmethod
    def _visible(model, it, entry):
        q = entry.get_text().lower().strip()
        if not q:
            return True
        t = TOOLS[model[it][2]]
        return q in t.name.lower() or q in t.desc.lower() or q in t.group.lower()

    def show_tool(self, name):
        for row in self.filter:
            if TOOLS[row[2]].name == name:
                self.listview.get_selection().select_iter(row.iter)
                return

    # ------------------------------------------------------------------
    def _on_select(self, sel):
        model, it = sel.get_selected()
        if it is None:
            return
        idx = model[it][2]
        tool = TOOLS[idx]
        if idx not in self.state:
            self.state[idx] = self._build_page(tool)
            self.stack.add_named(self.state[idx]["box"], str(idx))
            self.state[idx]["box"].show_all()
        self.stack.set_visible_child_name(str(idx))
        self.current = idx
        self._run(idx)

    def _build_page(self, tool):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        box.set_border_width(12)
        title = Gtk.Label(xalign=0)
        title.set_markup("<b>%s</b>  <span alpha='60%%'>%s</span>" % (GLib.markup_escape_text(tool.name),
                                                                  GLib.markup_escape_text(tool.desc)))
        title.set_line_wrap(True)
        box.pack_start(title, False, False, 0)

        st = {"box": box, "inputs": [], "opts": {}, "out": None, "error": None}
        if tool.opts:
            bar = Gtk.Box(spacing=14)
            for o in tool.opts:
                if o.kind == "bool":
                    w = Gtk.CheckButton(label=o.label, active=bool(o.default))
                    w.connect("toggled", lambda *_: self._schedule())
                    st["opts"][o.key] = (lambda w=w: w.get_active())
                    bar.pack_start(w, False, False, 0)
                elif o.kind == "choice":
                    bar.pack_start(Gtk.Label(label=o.label), False, False, 0)
                    w = Gtk.ComboBoxText()
                    for c in o.choices:
                        w.append_text(c)
                    w.set_active(o.choices.index(o.default))
                    w.connect("changed", lambda *_: self._schedule())
                    st["opts"][o.key] = (lambda w=w: w.get_active_text())
                    bar.pack_start(w, False, False, 0)
                elif o.kind == "int":
                    bar.pack_start(Gtk.Label(label=o.label), False, False, 0)
                    w = Gtk.SpinButton.new_with_range(o.lo, o.hi, 1)
                    w.set_value(o.default)
                    w.connect("value-changed", lambda *_: self._schedule())
                    st["opts"][o.key] = (lambda w=w: w.get_value_as_int())
                    bar.pack_start(w, False, False, 0)
            box.pack_start(bar, False, False, 0)

        grid = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for label in tool.inputs:
            lab = Gtk.Label(label=label, xalign=0)
            lab.get_style_context().add_class("dim-label")
            view, buf, sw = make_view(True)
            if tool.placeholder and len(tool.inputs) == 1:
                view.set_tooltip_text(tool.placeholder)
            buf.connect("changed", lambda *_: self._schedule())
            grid.pack_start(lab, False, False, 0)
            grid.pack_start(sw, True, True, 0)
            st["inputs"].append(buf)
        if tool.placeholder:
            hint = Gtk.Label(label="e.g. " + tool.placeholder, xalign=0)
            hint.get_style_context().add_class("dim-label")
            grid.pack_start(hint, False, False, 0)
        if tool.generate:
            gen = Gtk.Button(label="Generate")
            gen.get_style_context().add_class("suggested-action")
            gen.set_halign(Gtk.Align.START)
            gen.connect("clicked", lambda *_: self._run(self.current))
            grid.pack_start(gen, False, False, 0)
        box.pack_start(grid, tool.inputs != [], True, 0)

        head = Gtk.Box(spacing=8)
        lab = Gtk.Label(label=tool.output_label, xalign=0)
        lab.get_style_context().add_class("dim-label")
        head.pack_start(lab, True, True, 0)
        copy = Gtk.Button(label="Copy")
        head.pack_end(copy, False, False, 0)
        box.pack_start(head, False, False, 0)
        view, buf, sw = make_view(False)
        st["out"] = buf
        copy.connect("clicked", lambda *_: copy_to_clipboard(buf.get_text(buf.get_start_iter(), buf.get_end_iter(), False)))
        box.pack_start(sw, True, True, 0)
        return st

    def _schedule(self):
        if self._pending:
            GLib.source_remove(self._pending)
        self._pending = GLib.timeout_add(150, self._tick)

    def _tick(self):
        self._pending = None
        if self.current is not None:
            self._run(self.current)
        return False

    def _run(self, idx):
        tool, st = TOOLS[idx], self.state[idx]
        inputs = [b.get_text(b.get_start_iter(), b.get_end_iter(), False) for b in st["inputs"]]
        opts = {k: f() for k, f in st["opts"].items()}
        if tool.inputs and not any(inputs):
            st["out"].set_text("")
            return
        try:
            result = tool.fn(inputs, opts)
        except L.ToolError as e:
            result = "⚠ " + str(e)
        except Exception as e:   # a tool must never take the window down
            result = "⚠ Unexpected error: %s" % e
        st["out"].set_text(result)


def run(args):
    start = next((a for a in args if not a.startswith("--")), None)
    win = DevToolsPage(start)
    for a in args:
        if a.startswith("--file="):          # "Calculate hashes" from the file manager
            win.show_tool("Hashes")
            idx = [i for i, t in enumerate(TOOLS) if t.name == "Hashes"][0]
            win.state[idx]["inputs"][0].set_text(a.split("=", 1)[1])
    return win
