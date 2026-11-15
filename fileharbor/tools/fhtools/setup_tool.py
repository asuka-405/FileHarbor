"""The FileHarbor setup page: opens once on the first start (and from Tools > FileHarbor setup).

It collects the choices in one form and hands them to `fileharbor-setup`, the same program that runs in a
terminal, so both do exactly the same thing and `fileharbor-setup --undo` takes either back."""
import configparser
import os
import shutil

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_version("Gio", "2.0")
from gi.repository import Gdk, Gio, GLib, Gtk  # noqa: E402

from .base import ToolPage

PREFS = "in.archdev.FileHarbor.preferences"

APPEARANCE = [
    ("fileharbor-theme-mode", "Light or dark", [("system", "Follow the system"), ("light", "Always light"), ("dark", "Always dark")]),
    ("fileharbor-roundness", "Corner roundness", [("theme", "As the theme (default)"), ("square", "Square"), ("soft", "Soft"),
                                                  ("rounded", "Rounded"), ("round", "Very round")]),
    ("fileharbor-spaciousness", "Spaciousness", [("compact", "Compact"), ("default", "Default"), ("comfortable", "Comfortable"),
                                                 ("spacious", "Spacious")]),
]
BROWSING = [
    ("default-folder-viewer", "Default folder view", [("icon-view", "Icons"), ("list-view", "List"), ("compact-view", "Compact")]),
    ("click-policy", "Open items with", [("double", "Double click"), ("single", "Single click")]),
]
SWITCHES = [
    ("Browsing", [
        ("show-hidden-files", "Show hidden files"),
        ("sort-directories-first", "List folders before files"),
        ("restore-tabs-on-startup", "Reopen the tabs of the last session"),
    ]),
    ("Built-in tools", [
        ("text-open-in-editor", "Open text files in the built-in editor"),
        ("markdown-open-in-preview", "Open Markdown files in the preview"),
        ("media-open-in-builtin", "Play audio and video in the built-in player"),
        ("terminal-panel-visible", "Show the terminal panel at the top"),
        ("terminal-ssh-integration", "Follow ssh sessions of the terminal over sftp"),
    ]),
]
STORAGE = [
    ("on-demand", "When needed", "Starts when the Storage page asks for it. Nothing runs in the background otherwise."),
    ("always", "Always", "Starts at login and keeps the disk usage index fresh, so the Storage page opens instantly."),
    ("off", "Never", "No background service."),
]


def theme_choices():
    """[(id, name)]: built-in and your own themes."""
    dirs = []
    if os.environ.get("NEMO_THEME_DIR"):
        dirs.append(os.environ["NEMO_THEME_DIR"])
    dirs += [os.path.join(d, "nemo", "themes") for d in GLib.get_system_data_dirs()]
    dirs.append(os.path.join(GLib.get_user_config_dir(), "fileharbor", "themes"))
    found = {}
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.endswith(".ini"):
                cp = configparser.RawConfigParser()
                try:
                    cp.read(os.path.join(d, f))
                    found[f[:-4]] = cp.get("Theme", "Name", fallback=f[:-4])
                except configparser.Error:
                    found[f[:-4]] = f[:-4]
    ids = sorted(found, key=lambda i: (i != "system", i != "fileharbor", found[i].lower()))
    out = [(i, found[i] + (" (default)" if i == "system" else "")) for i in ids]
    out.append(("gtk", "Plain GTK theme"))
    return out


def setup_program():
    here = os.path.dirname(os.path.realpath(__file__))
    prefix = os.path.abspath(os.path.join(here, "..", "..", "..", ".."))      # <prefix>/share/nemo/tools/fhtools
    for cand in (os.path.join(prefix, "bin", "fileharbor-setup"), shutil.which("fileharbor-setup")):
        if cand and os.path.isfile(cand):
            return cand
    return None


def _label(text, bold=False, dim=False):
    l = Gtk.Label(xalign=0)
    if bold:
        l.set_markup("<b>%s</b>" % GLib.markup_escape_text(text))
    else:
        l.set_text(text)
    l.set_line_wrap(True)
    l.set_max_width_chars(64)
    if dim:
        l.get_style_context().add_class("dim-label")
    return l


class SetupPage(ToolPage):
    def __init__(self):
        super().__init__()
        self.set_title("FileHarbor setup")
        self.s = Gio.Settings.new(PREFS)
        self.combos = {}
        self.switches = {}
        self.storage = "on-demand"
        self.running = False
        self.sizes = Gtk.SizeGroup(mode=Gtk.SizeGroupMode.HORIZONTAL)

        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.pack_start(sw, True, True, 0)
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        outer.set_halign(Gtk.Align.CENTER)
        outer.set_border_width(24)
        sw.add(outer)
        self.body = body = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        outer.pack_start(body, True, True, 0)

        t = Gtk.Label(xalign=0)
        t.set_markup("<span size='x-large' weight='bold'>Welcome to FileHarbor</span>")
        body.pack_start(t, False, False, 0)
        body.pack_start(_label("A one-time setup. Everything can be changed later in Edit > Appearance and Preferences, "
                               "and Tools > FileHarbor setup opens this page again.", dim=True), False, False, 0)

        # appearance
        body.pack_start(_label("Appearance", bold=True), False, False, 6)
        grid = self._grid()
        row = 0
        self._combo(grid, row, "fileharbor-theme", "Theme", theme_choices()); row += 1
        for key, label, choices in APPEARANCE:
            self._combo(grid, row, key, label, choices); row += 1
        self.accent = Gtk.Switch(halign=Gtk.Align.START, valign=Gtk.Align.CENTER)
        self.accent.set_active(self.s.get_boolean("fileharbor-theme-desktop-accent"))
        acc = _label("Use my desktop's accent colour")
        self.sizes.add_widget(acc)
        grid.attach(acc, 0, row, 1, 1)
        grid.attach(self.accent, 1, row, 1, 1)
        body.pack_start(grid, False, False, 0)

        # browsing and tools
        body.pack_start(_label("Browsing", bold=True), False, False, 6)
        grid = self._grid()
        row = 0
        for key, label, choices in BROWSING:
            self._combo(grid, row, key, label, choices); row += 1
        body.pack_start(grid, False, False, 0)
        for title, items in SWITCHES:
            if title != "Browsing":
                body.pack_start(_label(title, bold=True), False, False, 6)
            for key, label in items:
                self._switch(body, key, label)

        # Storage Sense
        body.pack_start(_label("Storage Sense service", bold=True), False, False, 6)
        group = None
        for mode, name, hint in STORAGE:
            rb = Gtk.RadioButton.new_with_label_from_widget(group, name)
            group = group or rb
            rb.set_active(mode == "on-demand")
            rb.connect("toggled", lambda b, m=mode: b.get_active() and setattr(self, "storage", m))
            body.pack_start(rb, False, False, 0)
            h = _label(hint, dim=True)
            h.set_margin_start(28)
            body.pack_start(h, False, False, 0)

        # desktop
        body.pack_start(_label("Desktop", bold=True), False, False, 6)
        self.default_mgr = Gtk.CheckButton.new_with_label("Open folders with FileHarbor by default")
        self.default_mgr.set_active(True)
        body.pack_start(self.default_mgr, False, False, 0)
        self.system_scope = Gtk.CheckButton.new_with_label("Apply these as defaults for everyone on this computer (asks for your password)")
        body.pack_start(self.system_scope, False, False, 0)

        # buttons and result
        self.result = _label("", dim=True)
        self.result.set_selectable(True)
        footer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)       # stays in view while the form scrolls
        footer.set_border_width(12)
        footer.set_halign(Gtk.Align.CENTER)
        footer.pack_start(self.result, False, False, 0)
        bar = Gtk.Box(spacing=8)
        self.apply_btn = Gtk.Button(label="Apply setup")
        self.apply_btn.get_style_context().add_class("suggested-action")
        self.apply_btn.connect("clicked", self.apply)
        later = Gtk.Button(label="Not now")
        later.set_tooltip_text("Close this page; it will open again at the next start")
        later.connect("clicked", lambda *_: self.close_tab())
        never = Gtk.Button(label="Don't show again")
        never.set_tooltip_text("Keep the defaults and never open this page by itself. Tools > FileHarbor setup still opens it.")
        never.connect("clicked", self.dismiss)
        bar.pack_start(self.apply_btn, False, False, 0)
        bar.pack_start(later, False, False, 0)
        bar.pack_start(never, False, False, 0)
        footer.pack_start(bar, False, False, 0)
        self.pack_end(footer, False, False, 0)
        if setup_program() is None:
            self.apply_btn.set_sensitive(False)
            self.result.set_text("fileharbor-setup was not found, so nothing can be applied from here.")

    # ---- widgets ----------------------------------------------------------
    def _grid(self):
        g = Gtk.Grid(row_spacing=8, column_spacing=16)
        return g

    def _combo(self, grid, row, key, label, choices):
        c = Gtk.ComboBoxText()
        for cid, name in choices:
            c.append(cid, name)
        cur = self.s.get_string(key)
        if not c.set_active_id(cur):
            c.set_active(0)
        c.set_size_request(260, -1)
        lab = _label(label)
        self.sizes.add_widget(lab)
        grid.attach(lab, 0, row, 1, 1)
        grid.attach(c, 1, row, 1, 1)
        self.combos[key] = c

    def _switch(self, box, key, label):
        b = Gtk.CheckButton.new_with_label(label)
        b.set_active(self.s.get_boolean(key))
        box.pack_start(b, False, False, 0)
        self.switches[key] = b

    # ---- actions ----------------------------------------------------------
    def close_tab(self):
        """Closes this tab the way Ctrl+W does (the file manager owns the tab, a page cannot remove itself)."""
        top = self.win()
        if top is not None:
            Gtk.accel_groups_activate(top, Gdk.KEY_w, Gdk.ModifierType.CONTROL_MASK)

    def dismiss(self, *_):
        self.s.set_boolean("setup-done", True)
        self.close_tab()

    def command(self):
        prog = setup_program()
        cmd = [prog, "--yes", "--force", "--storage", self.storage,
               "--default-manager" if self.default_mgr.get_active() else "--no-default-manager"]
        values = {k: c.get_active_id() for k, c in self.combos.items()}
        values["fileharbor-theme-desktop-accent"] = "true" if self.accent.get_active() else "false"
        for k, b in self.switches.items():
            values[k] = "true" if b.get_active() else "false"
        for k, v in values.items():
            if v is not None:
                cmd += ["--set", "%s=%s" % (k, v)]
        if self.system_scope.get_active():
            pk = shutil.which("pkexec")
            if pk:
                cmd = [pk] + cmd[:1] + ["--system"] + cmd[1:]
        return cmd

    def apply(self, *_):
        if self.running:
            return
        self.running = True
        self.apply_btn.set_sensitive(False)
        self.result.set_text("Applying…")
        try:
            proc = Gio.Subprocess.new(self.command(), Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_MERGE)
        except GLib.Error as e:
            self.finished(False, str(e))
            return
        proc.communicate_utf8_async(None, None, self._done)

    def _done(self, proc, res):
        try:
            ok, out, _ = proc.communicate_utf8_finish(res)
        except GLib.Error as e:
            self.finished(False, str(e))
            return
        self.finished(proc.get_exit_status() == 0, out or "")

    def finished(self, ok, out):
        if not self._alive:
            return
        self.running = False
        self.apply_btn.set_sensitive(True)
        if ok:
            self.s.set_boolean("setup-done", True)
            self.result.set_text("Done. Your choices are applied. Log out and in again to pick up the launcher and the folder default.")
            self.apply_btn.set_label("Apply again")
        else:
            tail = [l for l in out.strip().splitlines() if l.strip()][-3:]
            self.result.set_text("Setup did not finish.\n" + "\n".join(tail))


def run(args=None):
    return SetupPage()
