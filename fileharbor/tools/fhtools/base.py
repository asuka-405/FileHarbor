"""ToolPage: the base of every FileHarbor tool.

A tool is a normal GTK widget, so the file manager can show it inside a tab. The same widget also runs
on its own (``fileharbor-tools image photo.jpg``) inside a plain window. ToolPage hides the difference:
it offers the few window-like methods the tools use (set_title, set_titlebar, key handling, dialogs'
parent) and forwards them to whatever toplevel window currently holds the page."""
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk, GLib, GObject  # noqa: E402


class ToolFailed(Exception):
    """The tool cannot start (missing file, missing program...). The message is shown to the user."""


class ToolPage(Gtk.Box):
    tool_title = GObject.Property(type=str, default="")

    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        self._key_cb = None
        self._delete_cb = None
        self._hook_top = None
        self._hook_id = 0
        self.default_size = (900, 640)
        self._alive = True
        Gtk.Box.connect(self, "map", self._hook_keys)
        Gtk.Box.connect(self, "unmap", self._unhook_keys)
        Gtk.Box.connect(self, "destroy", lambda *_: setattr(self, "_alive", False))

    # ---- window-like API used by the tools --------------------------------
    def win(self):
        top = self.get_toplevel()
        return top if isinstance(top, Gtk.Window) else None

    def set_title(self, title):
        self.props.tool_title = title

    def get_title(self):
        return self.props.tool_title

    def set_default_size(self, w, h):
        self.default_size = (w, h)

    def set_titlebar(self, header):
        if isinstance(header, Gtk.HeaderBar):
            header.set_show_close_button(False)
            header.set_decoration_layout("")
        self.pack_start(header, False, False, 0)

    def add(self, child):
        self.pack_start(child, True, True, 0)

    def present(self):
        pass

    def get_focus(self):
        w = self.win()
        return w.get_focus() if w else None

    def is_fullscreen(self):
        w = self.win()
        gw = w.get_window() if w else None
        return bool(gw and gw.get_state() & Gdk.WindowState.FULLSCREEN)

    def fullscreen(self):
        w = self.win()
        if w:
            w.fullscreen()

    def unfullscreen(self):
        w = self.win()
        if w:
            w.unfullscreen()

    def open_uri(self, uri, new_tab=True):
        """Ask the file manager window holding this page to open `uri` (in a new tab by default)."""
        w = self.win()
        if w is not None:
            try:
                w.activate_action("fh-open", GLib.Variant("(sb)", (uri, bool(new_tab))))
                return True
            except Exception:
                pass
        return False

    # ---- signals the tools used on a window: remapped ---------------------
    def connect(self, name, callback, *args):
        if name == "key-press-event":
            self._key_cb = callback
            return 0
        if name == "delete-event":
            self._delete_cb = callback
            return 0
        return super().connect(name, callback, *args)

    def _hook_keys(self, *_):
        top = self.win()
        if top is None or self._hook_id:
            return
        self._hook_top = top
        # Connected normally, so it runs before the file manager's own key handling.
        self._hook_id = top.connect("key-press-event", self._on_top_key)

    def _unhook_keys(self, *_):
        if self._hook_top is not None and self._hook_id:
            try:
                self._hook_top.disconnect(self._hook_id)
            except TypeError:
                pass
        self._hook_top, self._hook_id = None, 0

    def _on_top_key(self, window, event):
        if self._key_cb is None or not self.get_mapped():
            return False
        focus = window.get_focus()
        if focus is not None and not focus.is_ancestor(self) and isinstance(focus, (Gtk.Editable, Gtk.TextView)):
            return False        # the user is typing somewhere else in the file manager
        try:
            return bool(self._key_cb(window, event))
        except Exception:
            return False


def standalone(page, size=None):
    """Show a ToolPage in its own window (used when started from the command line)."""
    win = Gtk.Window()
    win.set_default_size(*(size or page.default_size))
    win.add(page)
    page.bind_property("tool-title", win, "title", GObject.BindingFlags.SYNC_CREATE)

    def on_delete(w, ev):
        cb = page._delete_cb
        return bool(cb(w, ev)) if cb else False
    win.connect("delete-event", on_delete)
    win.connect("destroy", lambda *_: Gtk.main_quit())
    win.show_all()
    page._hook_keys()
    return win


# ---- keep the user's work when a tab is closed --------------------------------
def recovery_dir():
    import os
    d = os.path.join(GLib.get_user_data_dir(), "fileharbor", "recovered")
    os.makedirs(d, exist_ok=True)
    return d


def keep_recovery_copy(name, writer):
    """A tab cannot ask “save changes?” when it is closed, so unsaved work is written here instead.
    `writer(path)` saves it. Returns the path (or None if it could not be written)."""
    import os
    import time
    path = os.path.join(recovery_dir(), "%s-%s" % (time.strftime("%Y%m%d-%H%M%S"), name))
    try:
        writer(path)
    except Exception:
        return None
    try:
        from gi.repository import Gio
        n = Gio.Notification.new("Unsaved changes were kept")
        n.set_body("A copy of %s was saved to %s" % (name, recovery_dir()))
        app = Gio.Application.get_default()
        if app:
            app.send_notification("fh-recovered", n)
    except Exception:
        pass
    return path
