"""Shared helpers for the FileHarbor tool windows."""
import os
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk, GLib  # noqa: E402


def human_size(n):
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if abs(n) < 1000 or unit == "TB":
            return ("%d %s" % (n, unit)) if unit == "B" else ("%.1f %s" % (n, unit))
        n /= 1000.0


def _top(w):
    """Dialogs need a window as their parent: accept a window, a widget inside one, or nothing."""
    if w is None or isinstance(w, Gtk.Window):
        return w
    top = w.get_toplevel() if isinstance(w, Gtk.Widget) else None
    return top if isinstance(top, Gtk.Window) else None


def message(parent, text, secondary=None, kind=Gtk.MessageType.INFO):
    d = Gtk.MessageDialog(transient_for=_top(parent), modal=True, message_type=kind,
                          buttons=Gtk.ButtonsType.OK, text=text)
    if secondary:
        d.format_secondary_text(secondary)
    d.run()
    d.destroy()


def ask(parent, text, secondary=None, accept="_OK"):
    d = Gtk.MessageDialog(transient_for=_top(parent), modal=True, message_type=Gtk.MessageType.QUESTION,
                          buttons=Gtk.ButtonsType.NONE, text=text)
    if secondary:
        d.format_secondary_text(secondary)
    d.add_button("_Cancel", Gtk.ResponseType.CANCEL)
    d.add_button(accept, Gtk.ResponseType.ACCEPT)
    r = d.run()
    d.destroy()
    return r == Gtk.ResponseType.ACCEPT


def copy_to_clipboard(text):
    Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD).set_text(text, -1)


def is_dark_theme():
    s = Gtk.Settings.get_default()
    if s.get_property("gtk-application-prefer-dark-theme"):
        return True
    name = (s.get_property("gtk-theme-name") or "").lower()
    return "dark" in name


def apply_app_theme():
    """Use the file manager's theme (colours come from the same ini files) if it is installed."""
    prov = Gtk.CssProvider()
    css = """
    .fh-title { font-weight: bold; font-size: 110%; }
    .fh-mono  { font-family: monospace; }
    .fh-hint  { opacity: 0.85; }
    """
    prov.load_from_data(css.encode())
    Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), prov,
                                             Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)


def run_main():
    Gtk.main()


def quit_when_last_window_closes(win):
    """Only for real windows; a tool shown in a tab must not quit the file manager."""
    if isinstance(win, Gtk.Window):
        win.connect("destroy", lambda *_: Gtk.main_quit())
