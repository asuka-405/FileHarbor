"""The FileHarbor developer panel (Tools > FileHarbor developer panel, Ctrl+Alt+D, while developer mode is on).

Tabs: Overview, Log, Theme, Settings, Services. The data comes from devmode_logic.py and from the files the file
manager writes while developer mode is on (~/.cache/fileharbor/debug)."""
import json
import os
import subprocess

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, Gio, GLib, Gtk  # noqa: E402

from . import devmode_logic as logic
from .base import ToolPage
from .common import message

SCHEMA_PREFIX = "in.archdev.FileHarbor"
MAX_RECORDS = 6000


def _lookup(widget, name, fallback):
    ok, rgba = widget.get_style_context().lookup_color(name)
    return rgba if ok else fallback


def _label(text, bold=False, dim=False, selectable=True):
    l = Gtk.Label(xalign=0)
    if bold:
        l.set_markup("<b>%s</b>" % GLib.markup_escape_text(text))
    else:
        l.set_text(text)
    l.set_selectable(selectable)
    l.set_line_wrap(True)
    if dim:
        l.get_style_context().add_class("dim-label")
    return l


def _scrolled(child):
    sw = Gtk.ScrolledWindow()
    sw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
    sw.add(child)
    return sw


def _button(text, cb, tip=None, suggested=False):
    b = Gtk.Button(label=text)
    if tip:
        b.set_tooltip_text(tip)
    if suggested:
        b.get_style_context().add_class("suggested-action")
    b.connect("clicked", cb)
    return b


class DevPage(ToolPage):
    def __init__(self):
        super().__init__()
        self.set_title("FileHarbor developer panel")
        self.records = []
        self.offset = 0
        self.settings_rows = []
        head = Gtk.Box(spacing=8)
        head.set_border_width(12)
        t = Gtk.Label(xalign=0)
        t.set_markup("<span size='large' weight='bold'>FileHarbor developer panel</span>")
        head.pack_start(t, True, True, 0)
        head.pack_end(_button("Copy diagnostic report", self.copy_report, "Everything below as Markdown, for a bug report"), False, False, 0)
        head.pack_end(_button("GTK inspector", self.open_inspector, "Inspect widgets and CSS (also Ctrl+Shift+I)"), False, False, 0)
        head.pack_end(_button("Restart FileHarbor", self.restart, "Quit and start it again"), False, False, 0)
        self.pack_start(head, False, False, 0)
        self.nb = Gtk.Notebook()
        self.nb.set_tab_pos(Gtk.PositionType.TOP)
        self.pack_start(self.nb, True, True, 0)
        self.nb.append_page(self._overview_tab(), Gtk.Label(label="Overview"))
        self.nb.append_page(self._log_tab(), Gtk.Label(label="Log"))
        self.nb.append_page(self._theme_tab(), Gtk.Label(label="Theme"))
        self.nb.append_page(self._settings_tab(), Gtk.Label(label="Settings"))
        self.nb.append_page(self._services_tab(), Gtk.Label(label="Services"))
        self.show_all()
        self._poll_id = GLib.timeout_add(1000, self._poll)
        Gtk.Box.connect(self, "destroy", lambda *_: GLib.source_remove(self._poll_id))
        self._poll()

    # ------------------------------------------------------------------ overview
    def _overview_tab(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        box.set_border_width(16)
        sections = logic.overview()
        s = Gtk.Settings.get_default()
        sections.insert(1, ("Appearance", [("GTK theme", s.props.gtk_theme_name), ("Icon theme", s.props.gtk_icon_theme_name),
                                           ("Font", s.props.gtk_font_name),
                                           ("GTK", "%d.%d.%d" % (Gtk.get_major_version(), Gtk.get_minor_version(), Gtk.get_micro_version())),
                                           ("GLib", "%d.%d.%d" % (GLib.MAJOR_VERSION, GLib.MINOR_VERSION, GLib.MICRO_VERSION))]))
        for title, rows in sections:
            box.pack_start(_label(title, bold=True), False, False, 6)
            grid = Gtk.Grid(column_spacing=16, row_spacing=2)
            for i, (k, v) in enumerate(rows):
                grid.attach(_label(k, dim=True), 0, i, 1, 1)
                grid.attach(_label(str(v)), 1, i, 1, 1)
            box.pack_start(grid, False, False, 0)
        actions = Gtk.Box(spacing=8)
        actions.set_margin_top(12)
        for text, path in (("Open config folder", logic.config_dir()), ("Open debug folder", logic.cache_dir())):
            actions.pack_start(_button(text, lambda b, p=path: self._open_folder(p)), False, False, 0)
        self.debug_toggle = Gtk.ToggleButton(label="Debug messages on")
        self.debug_toggle.set_active(True)
        self.debug_toggle.set_tooltip_text("g_debug() output in the log, like G_MESSAGES_DEBUG=all")
        self.debug_toggle.connect("toggled", self.toggle_debug)
        actions.pack_start(self.debug_toggle, False, False, 0)
        box.pack_start(actions, False, False, 0)
        return _scrolled(box)

    def _open_folder(self, path):
        os.makedirs(path, exist_ok=True)
        if not self.open_uri(Gio.File.new_for_path(path).get_uri(), True):
            subprocess.Popen(["xdg-open", path])

    def toggle_debug(self, btn):
        try:
            GLib.log_set_debug_enabled(btn.get_active())
        except Exception as e:
            message(self, "Not available", str(e), Gtk.MessageType.INFO)

    # ------------------------------------------------------------------ log
    def _log_tab(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.set_border_width(8)
        bar = Gtk.Box(spacing=6)
        self.level_btns = {}
        for lvl in logic.LEVELS:
            b = Gtk.ToggleButton(label=lvl.title())
            b.set_active(lvl != "DEBUG")
            b.connect("toggled", lambda *_: self._render_log())
            self.level_btns[lvl] = b
            bar.pack_start(b, False, False, 0)
        self.search = Gtk.SearchEntry()
        self.search.set_placeholder_text("Filter by domain or text")
        self.search.connect("search-changed", lambda *_: self._render_log())
        bar.pack_start(self.search, True, True, 8)
        self.follow = Gtk.CheckButton(label="Follow")
        self.follow.set_active(True)
        bar.pack_start(self.follow, False, False, 0)
        bar.pack_start(_button("Clear", self.clear_log, "Empty the log file"), False, False, 0)
        bar.pack_start(_button("Open file", self.open_log_file), False, False, 0)
        box.pack_start(bar, False, False, 0)
        self.log_view = Gtk.TextView()
        self.log_view.set_editable(False)
        self.log_view.set_monospace(True)
        self.log_view.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.log_view.set_left_margin(8)
        buf = self.log_view.get_buffer()
        dim = _lookup(self.log_view, "ns_text_dim", Gdk.RGBA(0.6, 0.6, 0.6, 1))
        self.tags = {
            "ERROR": buf.create_tag("ERROR", foreground_rgba=_lookup(self.log_view, "ns_error", Gdk.RGBA(1, 0.4, 0.4, 1)), weight=700),
            "CRITICAL": buf.create_tag("CRITICAL", foreground_rgba=_lookup(self.log_view, "ns_error", Gdk.RGBA(1, 0.4, 0.4, 1)), weight=700),
            "WARNING": buf.create_tag("WARNING", foreground_rgba=_lookup(self.log_view, "ns_warning", Gdk.RGBA(1, 0.8, 0.3, 1))),
            "MESSAGE": buf.create_tag("MESSAGE"),
            "INFO": buf.create_tag("INFO", foreground_rgba=_lookup(self.log_view, "ns_info", Gdk.RGBA(0.4, 0.7, 1, 1))),
            "DEBUG": buf.create_tag("DEBUG", foreground_rgba=dim),
            "meta": buf.create_tag("meta", foreground_rgba=dim),
        }
        box.pack_start(_scrolled(self.log_view), True, True, 0)
        self.log_status = _label("", dim=True)
        box.pack_start(self.log_status, False, False, 0)
        return box

    def _render_log(self):
        levels = {l for l, b in self.level_btns.items() if b.get_active()}
        shown = logic.filter_records(self.records, levels, self.search.get_text())
        buf = self.log_view.get_buffer()
        buf.set_text("")
        for time, level, domain, text in shown[-2500:]:
            it = buf.get_end_iter()
            buf.insert_with_tags(it, "%s  " % time, self.tags["meta"])
            buf.insert_with_tags(buf.get_end_iter(), "%-8s " % level, self.tags[level])
            buf.insert_with_tags(buf.get_end_iter(), "[%s] " % domain, self.tags["meta"])
            buf.insert_with_tags(buf.get_end_iter(), text + "\n", self.tags[level] if level in ("ERROR", "CRITICAL", "WARNING") else self.tags["MESSAGE"])
        self.log_status.set_text("%d of %d records   (%s)" % (len(shown), len(self.records), logic.log_path()))
        if self.follow.get_active():
            GLib.idle_add(lambda: self.log_view.scroll_to_iter(buf.get_end_iter(), 0, False, 0, 0) and False)

    def clear_log(self, *_):
        try:
            open(logic.log_path(), "w").close()
        except OSError:
            pass
        self.records, self.offset = [], 0
        self._render_log()

    def open_log_file(self, *_):
        p = logic.log_path()
        if os.path.exists(p):
            self.open_uri("x-fileharbor-tool:///editor?f=" + GLib.uri_escape_string(p, None, False), True)

    # ------------------------------------------------------------------ theme
    def _theme_tab(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        box.set_border_width(16)
        self.theme_info = _label("")
        box.pack_start(self.theme_info, False, False, 0)
        self.swatches = Gtk.FlowBox()
        self.swatches.set_selection_mode(Gtk.SelectionMode.NONE)
        self.swatches.set_max_children_per_line(8)
        self.swatches.set_column_spacing(8)
        self.swatches.set_row_spacing(8)
        box.pack_start(self.swatches, False, False, 0)
        box.pack_start(_label("Contrast (WCAG; text needs 4.5, icons and indicators 3)", bold=True), False, False, 6)
        self.contrast_grid = Gtk.Grid(column_spacing=16, row_spacing=2)
        box.pack_start(self.contrast_grid, False, False, 0)
        row = Gtk.Box(spacing=8)
        row.set_margin_top(10)
        row.pack_start(_button("Reload theme and CSS", self.reload_theme, "Re-applies the theme and dev.css", suggested=True), False, False, 0)
        row.pack_start(_button("Edit dev.css", self.edit_dev_css, "Extra CSS loaded after the theme; saving the file reloads it"), False, False, 0)
        row.pack_start(_button("Open themes folder", lambda *_: self._open_folder(os.path.join(logic.config_dir(), "themes"))), False, False, 0)
        box.pack_start(row, False, False, 0)
        self._theme_stamp = None
        return _scrolled(box)

    def reload_theme(self, *_):
        s = Gio.Settings.new(SCHEMA_PREFIX + ".preferences")
        s.set_uint("developer-reload", (s.get_uint("developer-reload") + 1) % 1000000)

    def edit_dev_css(self, *_):
        p = logic.dev_css_path()
        if not os.path.exists(p):
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w") as f:
                f.write("/* FileHarbor dev.css: loaded after the theme while developer mode is on, reloaded when saved.\n"
                        " * The palette is available as @ns_window, @ns_surface, @ns_surface_alt, @ns_surface_raised,\n"
                        " * @ns_text, @ns_text_dim, @ns_text_faint, @ns_accent, @ns_accent_text, @ns_border,\n"
                        " * @ns_border_strong, @ns_hover, @ns_success, @ns_warning, @ns_error and @ns_info.\n"
                        " * Example:  .nemo-window-pane toolbar { border-bottom: 2px solid @ns_accent; } */\n")
        self.open_uri("x-fileharbor-tool:///editor?f=" + GLib.uri_escape_string(p, None, False), True)

    def _refresh_theme(self):
        try:
            stamp = os.path.getmtime(logic.theme_path())
        except OSError:
            stamp = None
        if stamp == self._theme_stamp:
            return
        self._theme_stamp = stamp
        theme = logic.read_theme()
        for c in self.swatches.get_children():
            self.swatches.remove(c)
        for c in self.contrast_grid.get_children():
            self.contrast_grid.remove(c)
        if not theme:
            self.theme_info.set_text("No theme snapshot yet. It is written when the theme is applied; press “Reload theme and CSS”.")
            return
        self.theme_info.set_text(
            "Theme %s, mode %s, %s variant.\nDesktop detected: %s, prefers %s, accent %s (use desktop accent: %s)." % (
                theme.get("theme"), theme.get("mode"), "dark" if theme.get("dark") else "light", theme.get("desktop"),
                "dark" if theme.get("desktop_prefers_dark") else "light", theme.get("desktop_accent") or "none",
                "yes" if theme.get("use_desktop_accent") else "no"))
        colors = theme.get("colors", {})
        for name, hexv in colors.items():
            cell = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
            area = Gtk.DrawingArea()
            area.set_size_request(92, 36)
            area.connect("draw", self._draw_swatch, hexv)
            cell.pack_start(area, False, False, 0)
            cell.pack_start(_label(name, selectable=False), False, False, 0)
            cell.pack_start(_label(hexv, dim=True), False, False, 0)
            self.swatches.add(cell)
        for i, (fg, bg, ratio, want, ok) in enumerate(logic.contrast_report(colors)):
            self.contrast_grid.attach(_label("%s on %s" % (fg, bg)), 0, i, 1, 1)
            self.contrast_grid.attach(_label("%.2f : 1" % ratio), 1, i, 1, 1)
            self.contrast_grid.attach(_label("target %.1f  %s" % (want, "ok" if ok else "BELOW TARGET"), dim=ok), 2, i, 1, 1)
        self.show_all()

    @staticmethod
    def _draw_swatch(widget, cr, hexv):
        rgba = Gdk.RGBA()
        rgba.parse(hexv)
        cr.set_source_rgba(rgba.red, rgba.green, rgba.blue, 1)
        cr.rectangle(0, 0, widget.get_allocated_width(), widget.get_allocated_height())
        cr.fill()
        cr.set_source_rgba(0.5, 0.5, 0.5, 0.5)
        cr.rectangle(0.5, 0.5, widget.get_allocated_width() - 1, widget.get_allocated_height() - 1)
        cr.set_line_width(1)
        cr.stroke()
        return False

    # ------------------------------------------------------------------ settings
    def _settings_tab(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.set_border_width(8)
        bar = Gtk.Box(spacing=6)
        self.set_filter = Gtk.SearchEntry()
        self.set_filter.set_placeholder_text("Filter settings")
        self.set_filter.connect("search-changed", lambda *_: self._filter_settings())
        self.only_changed = Gtk.CheckButton(label="Only changed")
        self.only_changed.connect("toggled", lambda *_: self._filter_settings())
        bar.pack_start(self.set_filter, True, True, 0)
        bar.pack_start(self.only_changed, False, False, 0)
        bar.pack_start(_button("Refresh", lambda *_: self._load_settings()), False, False, 0)
        bar.pack_start(_button("Reset selected", self.reset_selected, "Put the selected setting back to its default"), False, False, 0)
        box.pack_start(bar, False, False, 0)
        self.set_store = Gtk.ListStore(str, str, str, str, bool)
        self.set_filtered = self.set_store.filter_new()
        self.set_filtered.set_visible_func(self._setting_visible)
        tv = Gtk.TreeView(model=self.set_filtered)
        for i, title in enumerate(("Schema", "Key", "Value", "Default")):
            col = Gtk.TreeViewColumn(title, Gtk.CellRendererText(ellipsize=3), text=i)
            col.set_resizable(True)
            col.set_expand(i == 2)
            tv.append_column(col)
        self.set_tv = tv
        box.pack_start(_scrolled(tv), True, True, 0)
        self._load_settings()
        return box

    def _load_settings(self):
        self.set_store.clear()
        self.settings_rows = []
        src = Gio.SettingsSchemaSource.get_default()
        schemas, _ = src.list_schemas(True)
        for sid in sorted(s for s in schemas if s.startswith(SCHEMA_PREFIX)):
            schema = src.lookup(sid, True)
            gs = Gio.Settings.new(sid)
            for key in sorted(schema.list_keys()):
                k = schema.get_key(key)
                cur = gs.get_value(key).print_(False)
                default = k.get_default_value().print_(False)
                row = [sid.replace(SCHEMA_PREFIX, "…", 1), key, cur, default, cur != default]
                self.set_store.append(row)
                self.settings_rows.append((sid, key, cur, default))
        self._filter_settings()

    def _setting_visible(self, model, it, data=None):
        text = (self.set_filter.get_text() or "").lower()
        row = model[it]
        if self.only_changed.get_active() and not row[4]:
            return False
        return not text or text in (row[0] + " " + row[1] + " " + row[2]).lower()

    def _filter_settings(self):
        self.set_filtered.refilter()

    def reset_selected(self, *_):
        model, it = self.set_tv.get_selection().get_selected()
        if it is None:
            return
        schema_short, key = model[it][0], model[it][1]
        for sid, k, _, _ in self.settings_rows:
            if k == key and sid.replace(SCHEMA_PREFIX, "…", 1) == schema_short:
                Gio.Settings.new(sid).reset(key)
        self._load_settings()

    def changed_settings(self):
        return [("%s %s" % (sid.replace(SCHEMA_PREFIX, "…", 1), k), cur) for sid, k, cur, d in self.settings_rows if cur != d]

    # ------------------------------------------------------------------ services
    def _services_tab(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        box.set_border_width(16)
        self.services_grid = Gtk.Grid(column_spacing=16, row_spacing=4)
        box.pack_start(self.services_grid, False, False, 0)
        row = Gtk.Box(spacing=8)
        row.pack_start(_button("Refresh", lambda *_: self._load_services()), False, False, 0)
        row.pack_start(_button("Start Storage Sense daemon", self.start_daemon), False, False, 0)
        box.pack_start(row, False, False, 0)
        box.pack_start(_label("Storage Sense status", bold=True), False, False, 6)
        self.ss_status = _label("")
        box.pack_start(self.ss_status, False, False, 0)
        self._load_services()
        return _scrolled(box)

    def _owner_info(self, bus, name):
        try:
            owner = bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner",
                                  GLib.Variant("(s)", (name,)), None, Gio.DBusCallFlags.NONE, 800, None).unpack()[0]
            pid = bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                                GLib.Variant("(s)", (owner,)), None, Gio.DBusCallFlags.NONE, 800, None).unpack()[0]
            try:
                cmd = open("/proc/%d/cmdline" % pid).read().replace("\0", " ").strip()[:90]
            except OSError:
                cmd = "?"
            return "running: pid %d  %s" % (pid, cmd)
        except Exception:
            return "not running"

    def _load_services(self):
        for c in self.services_grid.get_children():
            self.services_grid.remove(c)
        rows = []
        try:
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
            for name in ("in.archdev.FileHarbor", "in.archdev.FileHarbor.StorageSense", "org.freedesktop.FileManager1",
                         "org.freedesktop.portal.Desktop"):
                rows.append((name, self._owner_info(bus, name)))
        except Exception as e:
            rows.append(("session bus", "unavailable: %s" % e))
        try:
            r = subprocess.run(["systemctl", "--user", "is-active", "fileharbor-storage-sense.service"],
                               capture_output=True, text=True, timeout=2)
            rows.append(("fileharbor-storage-sense.service", r.stdout.strip() or "unknown"))
        except Exception:
            rows.append(("fileharbor-storage-sense.service", "systemctl unavailable"))
        data = [GLib.get_user_data_dir()] + list(GLib.get_system_data_dirs())
        ext = [os.path.join(d, "fileharbor", "extensions") for d in data]
        found = []
        for d in ext:
            if os.path.isdir(d):
                found += ["%s (%s)" % (n, d) for n in sorted(os.listdir(d))]
        rows.append(("extensions", "; ".join(found) if found else "none found"))
        for i, (k, v) in enumerate(rows):
            self.services_grid.attach(_label(k, dim=True), 0, i, 1, 1)
            self.services_grid.attach(_label(v), 1, i, 1, 1)
        self.services_grid.show_all()
        self._call_storage_status()

    def _call_storage_status(self):
        try:
            proxy = Gio.DBusProxy.new_for_bus_sync(
                Gio.BusType.SESSION, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None, "in.archdev.FileHarbor.StorageSense",
                "/in/archdev/FileHarbor/StorageSense", "in.archdev.FileHarbor.StorageSense", None)
            res = proxy.call_sync("GetStatus", None, Gio.DBusCallFlags.NONE, 1500, None).unpack()[0]
            self.ss_status.set_text(json.dumps(json.loads(res), indent=2) if res else "(empty)")
        except Exception as e:
            self.ss_status.set_text("not reachable: %s" % e)

    def start_daemon(self, *_):
        try:
            subprocess.Popen(["fileharbor-storage-daemon"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        except OSError as e:
            message(self, "Could not start it", str(e), Gtk.MessageType.ERROR)
        GLib.timeout_add(1500, lambda: (self._load_services(), False)[1])

    # ------------------------------------------------------------------ header actions
    def open_inspector(self, *_):
        Gtk.Window.set_interactive_debugging(True)

    def copy_report(self, *_):
        text = logic.report_text(changed_settings=self.changed_settings())
        Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD).set_text(text, -1)
        message(self, "Copied", "The diagnostic report is on the clipboard (%d lines)." % text.count("\n"), Gtk.MessageType.INFO)

    def restart(self, *_):
        subprocess.Popen(["sh", "-c", "fileharbor -q; sleep 1; fileharbor"], start_new_session=True,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # ------------------------------------------------------------------ polling
    def _poll(self):
        new, self.offset = logic.read_new(logic.log_path(), self.offset)
        if new:
            self.records = (self.records + new)[-MAX_RECORDS:]
            self._render_log()
        elif not self.records and not os.path.exists(logic.log_path()):
            self.log_status.set_text("No log yet: it is written while developer mode is on (%s)" % logic.log_path())
        self._refresh_theme()
        return True


def run(args=None):
    return DevPage()
