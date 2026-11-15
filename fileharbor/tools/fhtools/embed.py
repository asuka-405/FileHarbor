"""Entry point used by the file manager to put a tool inside a tab (see src/fileharbor-python.c)."""
import ctypes
import os

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk  # noqa: E402

from .base import ToolPage, ToolFailed
from .common import apply_app_theme

_keep = {}          # pointer -> page: the wrapper must outlive the C side's use of the widget
_themed = False

_get_ptr = ctypes.pythonapi.PyCapsule_GetPointer
_get_ptr.restype = ctypes.c_void_p
_get_ptr.argtypes = [ctypes.py_object, ctypes.c_char_p]


def _pointer(widget):
    return _get_ptr(widget.__gpointer__, None)


class ChoosePage(ToolPage):
    """Shown when a tool is opened with no file: a button to pick one, then the real tool replaces it."""

    def __init__(self, tool, label, multiple=False, noun="a file"):
        super().__init__()
        self.tool, self.multiple = tool, multiple
        self.set_title(label)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        box.set_valign(Gtk.Align.CENTER)
        box.set_halign(Gtk.Align.CENTER)
        img = Gtk.Image.new_from_icon_name("document-open-symbolic", Gtk.IconSize.DIALOG)
        img.set_pixel_size(64)
        lab = Gtk.Label(label="Choose %s to open" % noun)
        btn = Gtk.Button(label="Choose…")
        btn.get_style_context().add_class("suggested-action")
        btn.connect("clicked", self._choose)
        for w in (img, lab, btn):
            box.pack_start(w, False, False, 0)
        self.pack_start(box, True, True, 0)
        self.show_all()
        self.box = box

    def _choose(self, *_):
        d = Gtk.FileChooserDialog(title="Choose", transient_for=self.win(), action=Gtk.FileChooserAction.OPEN)
        d.set_select_multiple(self.multiple)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Open", Gtk.ResponseType.ACCEPT)
        if d.run() != Gtk.ResponseType.ACCEPT:
            d.destroy()
            return
        files = d.get_filenames()
        d.destroy()
        try:
            page = build(self.tool, files)
        except Exception as e:
            from .common import message
            message(self, "Cannot open this", str(e), Gtk.MessageType.ERROR)
            return
        self.remove(self.box)
        # the real page's own widgets move into this tab
        for child in page.get_children():
            page.remove(child)
            self.pack_start(child, True, True, 0)
        self.set_title(page.get_title())
        for attr in ("_key_cb", "_delete_cb"):
            setattr(self, attr, getattr(page, attr))
        self._delegate = page           # keeps the real page (and its state) alive with this tab
        Gtk.Box.connect(self, "destroy", lambda *_: page.destroy())    # so its cleanup (stop playback, recovery copy) runs
        self.show_all()


def build(tool, files):
    """One place decides which page a tool id and a list of files give; used in tabs and standalone."""
    from . import devtools, image_tool, media_tool, resize_tool, editor_tool
    from .hub import HubPage
    if tool in ("hub", ""):
        return HubPage()
    if tool == "fhdev":
        from . import devmode_tool
        return devmode_tool.run()
    if tool == "devtools":
        return devtools.run(["--file=%s" % f for f in files])
    if tool in ("editor", "markdown"):
        return editor_tool.run(files[:1])
    if tool == "preview":
        return editor_tool.run(files[:1], preview=True)
    if tool in ("media", "music", "video"):
        if not files:
            return ChoosePage("media", "Media player", True, "music or video")
        if media_tool.is_video(files[0]):
            return media_tool.VideoPage(files[0])
        return media_tool.MusicPage(files)
    if tool == "image":
        if not files:
            return ChoosePage("image", "Images", True, "pictures")
        return resize_tool.ResizePage(files) if len(files) > 1 else image_tool.ImagePage(files[0])
    if tool == "resize":
        return resize_tool.ResizePage(files) if files else ChoosePage("resize", "Resize pictures", True, "pictures")
    raise ToolFailed("Unknown tool: %s" % tool)


def create(tool, files):
    """Called from C with the tool name and file paths; returns the address of the GtkWidget."""
    global _themed
    if not _themed:
        apply_app_theme()
        _themed = True
    files = [os.path.abspath(f) for f in files]
    try:
        page = build(tool, files)
    except ToolFailed as e:
        raise RuntimeError(str(e))
    ptr = _pointer(page)
    _keep[ptr] = page
    page.connect("destroy", lambda *_: _keep.pop(ptr, None))
    page.show_all()
    return ptr
