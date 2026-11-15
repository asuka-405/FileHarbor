"""Lightweight image viewer and editor (Pillow based), with camera RAW support."""
import os
import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gtk, Gdk, GLib, GdkPixbuf  # noqa: E402
import cairo  # noqa: E402

from PIL import Image, ImageEnhance, ImageFilter, ImageOps

from . import rawload, bgremove
from .base import ToolPage, ToolFailed, keep_recovery_copy
from .common import human_size, message, ask


def pil_to_surface(im):
    if im.mode != "RGBA":
        im = im.convert("RGBA")
    r, g, b, a = im.split()
    # cairo wants premultiplied BGRA
    from PIL import ImageChops
    r, g, b = (ImageChops.multiply(c, a) for c in (r, g, b))
    data = bytearray(Image.merge("RGBA", (b, g, r, a)).tobytes())
    surf = cairo.ImageSurface.create_for_data(data, cairo.FORMAT_ARGB32, im.width, im.height, im.width * 4)
    return surf, data            # the caller must keep `data` alive as long as the surface is used


class Canvas(Gtk.DrawingArea):
    def __init__(self, owner):
        super().__init__()
        self.owner = owner
        self.surface = None
        self.zoom = 1.0
        self.fit = True
        self.ox = self.oy = 0.0
        self.drag = None
        self.crop = None            # (x0, y0, x1, y1) in image pixels, while cropping
        self.cropping = False
        self.picking = False        # next click chooses the background colour
        self.add_events(Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK |
                        Gdk.EventMask.POINTER_MOTION_MASK | Gdk.EventMask.SCROLL_MASK |
                        Gdk.EventMask.SMOOTH_SCROLL_MASK)
        self.connect("draw", self._draw)
        self.connect("button-press-event", self._press)
        self.connect("button-release-event", self._release)
        self.connect("motion-notify-event", self._motion)
        self.connect("scroll-event", self._scroll)
        self.connect("size-allocate", lambda *_: self._refit())
        self.set_hexpand(True)
        self.set_vexpand(True)

    def set_image(self, im):
        self.surface, self._pixels = pil_to_surface(im) if im is not None else (None, None)
        self.crop = None
        self._refit()
        self.queue_draw()

    def _refit(self):
        if not self.surface or not self.fit:
            return
        w, h = self.get_allocated_width(), self.get_allocated_height()
        iw, ih = self.surface.get_width(), self.surface.get_height()
        if w > 1 and h > 1:
            self.zoom = min(w / iw, h / ih, 1.0 if self.owner.fit_no_upscale else 1e9)
            self.ox = (w - iw * self.zoom) / 2
            self.oy = (h - ih * self.zoom) / 2

    def set_zoom(self, z, cx=None, cy=None):
        if not self.surface:
            return
        z = max(0.02, min(z, 32))
        w, h = self.get_allocated_width(), self.get_allocated_height()
        cx = w / 2 if cx is None else cx
        cy = h / 2 if cy is None else cy
        ix, iy = (cx - self.ox) / self.zoom, (cy - self.oy) / self.zoom
        self.zoom = z
        self.ox, self.oy = cx - ix * z, cy - iy * z
        self.fit = False
        self._clamp()
        self.owner.update_status()
        self.queue_draw()

    def _clamp(self):
        if not self.surface:
            return
        w, h = self.get_allocated_width(), self.get_allocated_height()
        iw, ih = self.surface.get_width() * self.zoom, self.surface.get_height() * self.zoom
        self.ox = (w - iw) / 2 if iw <= w else min(0, max(w - iw, self.ox))
        self.oy = (h - ih) / 2 if ih <= h else min(0, max(h - ih, self.oy))

    def to_image(self, x, y):
        return (x - self.ox) / self.zoom, (y - self.oy) / self.zoom

    def _draw(self, w, cr):
        alloc = (self.get_allocated_width(), self.get_allocated_height())
        ctx = self.get_style_context()
        Gtk.render_background(ctx, cr, 0, 0, *alloc)
        if not self.surface:
            return False
        cr.save()
        cr.translate(self.ox, self.oy)
        cr.scale(self.zoom, self.zoom)
        # checkerboard behind transparent pixels
        iw, ih = self.surface.get_width(), self.surface.get_height()
        cr.rectangle(0, 0, iw, ih)
        cr.clip_preserve()
        cr.set_source_rgb(0.5, 0.5, 0.5)
        cr.fill_preserve()
        cr.set_source_rgb(0.62, 0.62, 0.62)
        step = 16 / self.zoom
        y = 0
        row = 0
        while y < ih:
            x = step if row % 2 else 0
            while x < iw:
                cr.rectangle(x, y, step, step)
                x += 2 * step
            y += step
            row += 1
        cr.fill()
        cr.set_source_surface(self.surface, 0, 0)
        cr.get_source().set_filter(cairo.FILTER_NEAREST if self.zoom >= 3 else cairo.FILTER_BILINEAR)
        cr.paint()
        cr.restore()
        if self.crop:
            x0, y0, x1, y1 = self.crop
            sx0, sy0 = x0 * self.zoom + self.ox, y0 * self.zoom + self.oy
            sx1, sy1 = x1 * self.zoom + self.ox, y1 * self.zoom + self.oy
            cr.set_source_rgba(0, 0, 0, 0.55)
            cr.set_fill_rule(cairo.FILL_RULE_EVEN_ODD)
            cr.rectangle(0, 0, *alloc)
            cr.rectangle(min(sx0, sx1), min(sy0, sy1), abs(sx1 - sx0), abs(sy1 - sy0))
            cr.fill()
            cr.set_source_rgb(1, 1, 1)
            cr.set_line_width(1.5)
            cr.rectangle(min(sx0, sx1), min(sy0, sy1), abs(sx1 - sx0), abs(sy1 - sy0))
            cr.stroke()
        return False

    def _press(self, w, ev):
        if ev.button == 1 and self.picking and self.surface:
            x, y = self.to_image(ev.x, ev.y)
            self.owner.pick_background(int(x), int(y))
            return True
        if ev.button == 1:
            if self.cropping:
                x, y = self.to_image(ev.x, ev.y)
                self.crop = (x, y, x, y)
            self.drag = (ev.x, ev.y, self.ox, self.oy)
        elif ev.button == 2:
            self.drag = (ev.x, ev.y, self.ox, self.oy)
        if ev.type == Gdk.EventType._2BUTTON_PRESS and not self.cropping:
            self.fit = not self.fit
            if self.fit:
                self._refit()
            else:
                self.set_zoom(1.0, ev.x, ev.y)
            self.owner.update_status()
            self.queue_draw()
        return True

    def _motion(self, w, ev):
        if not self.drag:
            return False
        if self.cropping and self.crop:
            x, y = self.to_image(ev.x, ev.y)
            iw, ih = self.surface.get_width(), self.surface.get_height()
            x, y = max(0, min(iw, x)), max(0, min(ih, y))
            self.crop = (self.crop[0], self.crop[1], x, y)
        else:
            self.fit = False
            self.ox = self.drag[2] + ev.x - self.drag[0]
            self.oy = self.drag[3] + ev.y - self.drag[1]
            self._clamp()
        self.queue_draw()
        return True

    def _release(self, w, ev):
        self.drag = None
        if self.cropping:
            self.owner.crop_changed()
        return True

    def _scroll(self, w, ev):
        if ev.state & Gdk.ModifierType.CONTROL_MASK or True:
            if ev.direction == Gdk.ScrollDirection.SMOOTH:
                ok, dx, dy = ev.get_scroll_deltas()
                f = 1.0 - dy * 0.1
            else:
                f = 1.1 if ev.direction == Gdk.ScrollDirection.UP else 1 / 1.1
            self.set_zoom(self.zoom * f, ev.x, ev.y)
        return True


class ImagePage(ToolPage):
    fit_no_upscale = True

    def __init__(self, path):
        super().__init__()
        self.set_default_size(1100, 760)
        self.path = os.path.abspath(path)
        self.folder = os.path.dirname(self.path)
        self.siblings = []
        self.im = None
        self.original = None
        self.info = {}
        self.history, self.redo = [], []
        self.modified = False

        header = Gtk.HeaderBar(show_close_button=True)
        self.set_titlebar(header)
        for icon, tip, cb in (("go-previous-symbolic", "Previous image (←)", lambda *_: self.step(-1)),
                              ("go-next-symbolic", "Next image (→)", lambda *_: self.step(1))):
            b = Gtk.Button.new_from_icon_name(icon, Gtk.IconSize.BUTTON)
            b.set_tooltip_text(tip)
            b.connect("clicked", cb)
            header.pack_start(b)
        many = Gtk.Button.new_from_icon_name("image-x-generic-symbolic", Gtk.IconSize.BUTTON)
        many.set_tooltip_text("Resize or convert several pictures at once…")
        many.connect("clicked", self._resize_many)
        header.pack_start(many)
        self.save_b = Gtk.MenuButton()
        self.save_b.set_image(Gtk.Image.new_from_icon_name("document-save-symbolic", Gtk.IconSize.BUTTON))
        self.save_b.set_tooltip_text("Save")
        self.save_b.set_popup(self._save_menu())
        header.pack_end(self.save_b)
        for icon, tip, cb in (("view-fullscreen-symbolic", "Full screen (F11)", lambda *_: self.toggle_fullscreen()),
                              ("zoom-fit-best-symbolic", "Fit (F)", lambda *_: self.fit_view()),
                              ("zoom-original-symbolic", "Actual size (1)", lambda *_: self.canvas.set_zoom(1.0)),
                              ("zoom-in-symbolic", "Zoom in (+)", lambda *_: self.canvas.set_zoom(self.canvas.zoom * 1.25)),
                              ("zoom-out-symbolic", "Zoom out (-)", lambda *_: self.canvas.set_zoom(self.canvas.zoom / 1.25))):
            b = Gtk.Button.new_from_icon_name(icon, Gtk.IconSize.BUTTON)
            b.set_tooltip_text(tip)
            b.connect("clicked", cb)
            header.pack_end(b)

        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.add(outer)
        self.toolbar = self._build_toolbar()
        outer.pack_start(self.toolbar, False, False, 0)
        body = Gtk.Box()
        outer.pack_start(body, True, True, 0)
        self.canvas = Canvas(self)
        body.pack_start(self.canvas, True, True, 0)
        self.panel = self._build_adjust_panel()
        body.pack_start(self.panel, False, False, 0)
        self.bg_panel = self._build_bg_panel()
        body.pack_start(self.bg_panel, False, False, 0)
        self._bg_colour = self._bg_seed = None
        self._bg_timer = 0
        self.status = Gtk.Label(xalign=0)
        self.status.set_margin_start(10)
        self.status.set_margin_end(10)
        self.status.set_margin_top(4)
        self.status.set_margin_bottom(4)
        self.status.get_style_context().add_class("dim-label")
        outer.pack_start(self.status, False, False, 0)

        self.connect("key-press-event", self._key)
        self.connect("delete-event", lambda *_: not self._confirm_discard())
        Gtk.Box.connect(self, "destroy", self._recover)
        self.show_all()
        # hidden panels must survive the show_all() that embedding a page in a tab does: show their
        # children now, then keep the panels out of any later show_all()
        for w in (self.panel, self.bg_panel, self.crop_bar):
            w.set_no_show_all(True)
            w.hide()
        self.load(self.path)

    def _resize_many(self, *_):
        """Batch resizing lives in the same tool: pick pictures, a new tab opens the resizer."""
        d = Gtk.FileChooserDialog(title="Choose pictures to resize", transient_for=self.win(), action=Gtk.FileChooserAction.OPEN)
        d.set_select_multiple(True)
        d.set_current_folder(self.folder)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Choose", Gtk.ResponseType.ACCEPT)
        if d.run() == Gtk.ResponseType.ACCEPT:
            files = d.get_filenames()
            d.destroy()
            from gi.repository import GLib
            uri = "x-fileharbor-tool:///resize" + "".join("%sf=%s" % ("?" if i == 0 else "&", GLib.uri_escape_string(f, None, False))
                                                         for i, f in enumerate(files))
            if not self.open_uri(uri, True):
                from . import resize_tool
                from .base import standalone
                standalone(resize_tool.ResizePage(files))
        else:
            d.destroy()

    # ------------------------------------------------------------ chrome
    def _tool_button(self, bar, icon, tip, cb, label=None):
        b = Gtk.ToolButton(icon_name=icon, label=label)
        b.set_tooltip_text(tip)
        b.connect("clicked", cb)
        bar.insert(b, -1)
        return b

    def _build_toolbar(self):
        wrap = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        bar = Gtk.Toolbar()
        bar.set_style(Gtk.ToolbarStyle.BOTH_HORIZ)
        self._tool_button(bar, "object-rotate-left-symbolic", "Rotate left (Ctrl+L)", lambda *_: self.apply(lambda im: im.rotate(90, expand=True)), "Rotate")
        self._tool_button(bar, "object-rotate-right-symbolic", "Rotate right (Ctrl+R)", lambda *_: self.apply(lambda im: im.rotate(-90, expand=True)))
        self._tool_button(bar, "object-flip-horizontal-symbolic", "Flip horizontally", lambda *_: self.apply(ImageOps.mirror), "Flip")
        self._tool_button(bar, "object-flip-vertical-symbolic", "Flip vertically", lambda *_: self.apply(ImageOps.flip))
        bar.insert(Gtk.SeparatorToolItem(), -1)
        self._tool_button(bar, "edit-cut-symbolic", "Crop: drag a rectangle, then press Apply", lambda *_: self.start_crop(), "Crop")
        self._tool_button(bar, "view-fullscreen-symbolic", "Resize the image", lambda *_: self.resize_dialog(), "Resize")
        self._tool_button(bar, "color-select-symbolic", "Brightness, contrast, colour, sharpness", lambda *_: self.toggle_adjust_panel(), "Adjust")
        self._tool_button(bar, "edit-clear-all-symbolic", "Remove a plain-coloured background (make it transparent)", lambda *_: self.toggle_bg_panel(), "Remove BG")
        self._tool_button(bar, "image-x-generic-symbolic", "Black and white", lambda *_: self.apply(lambda im: ImageOps.grayscale(im).convert("RGB")), "B&W")
        self._tool_button(bar, "emblem-photos-symbolic", "Auto contrast", lambda *_: self.apply(lambda im: self._rgb_only(ImageOps.autocontrast, im)), "Auto")
        bar.insert(Gtk.SeparatorToolItem(), -1)
        self._tool_button(bar, "edit-undo-symbolic", "Undo (Ctrl+Z)", lambda *_: self.undo())
        self._tool_button(bar, "edit-redo-symbolic", "Redo (Ctrl+Shift+Z)", lambda *_: self.redo_step())
        self._tool_button(bar, "document-revert-symbolic", "Revert to the original", lambda *_: self.revert(), "Revert")
        wrap.pack_start(bar, False, False, 0)
        self.crop_bar = Gtk.Box(spacing=8)
        self.crop_bar.set_border_width(6)
        self.crop_label = Gtk.Label(label="Drag on the picture to choose the area to keep.")
        apply_b = Gtk.Button(label="Apply crop")
        apply_b.get_style_context().add_class("suggested-action")
        apply_b.connect("clicked", lambda *_: self.apply_crop())
        cancel_b = Gtk.Button(label="Cancel")
        cancel_b.connect("clicked", lambda *_: self.stop_crop())
        self.crop_bar.pack_start(self.crop_label, False, False, 0)
        self.crop_bar.pack_end(cancel_b, False, False, 0)
        self.crop_bar.pack_end(apply_b, False, False, 0)
        wrap.pack_start(self.crop_bar, False, False, 0)
        return wrap

    def _build_adjust_panel(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.set_border_width(10)
        box.set_size_request(230, -1)
        head = Gtk.Label(xalign=0)
        head.set_markup("<b>Adjust</b>")
        box.pack_start(head, False, False, 0)
        self.sliders = {}
        for key, label, lo, hi in (("brightness", "Brightness", 0.2, 2.0), ("contrast", "Contrast", 0.2, 2.0),
                                   ("color", "Colour", 0.0, 2.0), ("sharpness", "Sharpness", 0.0, 3.0)):
            box.pack_start(Gtk.Label(label=label, xalign=0), False, False, 0)
            s = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, lo, hi, 0.05)
            s.set_value(1.0)
            s.add_mark(1.0, Gtk.PositionType.BOTTOM, None)
            s.connect("value-changed", lambda *_: self._adjust_preview())
            box.pack_start(s, False, False, 0)
            self.sliders[key] = s
        row = Gtk.Box(spacing=6)
        ok = Gtk.Button(label="Apply")
        ok.get_style_context().add_class("suggested-action")
        ok.connect("clicked", lambda *_: self._adjust_commit())
        reset = Gtk.Button(label="Reset")
        reset.connect("clicked", lambda *_: self._adjust_reset())
        row.pack_start(ok, True, True, 0)
        row.pack_start(reset, True, True, 0)
        box.pack_start(row, False, False, 6)
        return box

    def _save_menu(self):
        menu = Gtk.Menu()
        for label, cb in (("Save", lambda *_: self.save()), ("Save as…", lambda *_: self.save_as()),
                          ("Save a copy as…", lambda *_: self.save_as(copy=True))):
            it = Gtk.MenuItem(label=label)
            # open the dialog only after the popup menu has gone; a modal dialog started from inside the
            # menu's own activate handler can kill the app on Wayland
            it.connect("activate", lambda *_a, cb=cb: GLib.timeout_add(120, lambda: (cb(), False)[1]))
            menu.append(it)
        menu.show_all()
        return menu

    # ------------------------------------------------------------ loading
    def _scan_siblings(self):
        try:
            names = sorted(os.listdir(self.folder), key=str.lower)
        except OSError:
            names = [os.path.basename(self.path)]
        self.siblings = [os.path.join(self.folder, n) for n in names if rawload.is_image(n)]
        if self.path not in self.siblings:
            self.siblings.append(self.path)

    def load(self, path):
        if not self._confirm_discard():
            return
        self.set_title(os.path.basename(path))
        self.status.set_text("Loading…")
        while Gtk.events_pending():
            Gtk.main_iteration()
        try:
            im, info = rawload.load(path)
        except (OSError, ValueError, Image.UnidentifiedImageError) as e:
            self.status.set_text("")
            message(self, "Cannot open this image", "%s\n\n%s" % (os.path.basename(path), e), Gtk.MessageType.ERROR)
            return
        self.path, self.info = path, info
        self.folder = os.path.dirname(path)
        self._scan_siblings()
        self.original = im
        self.history, self.redo = [], []
        self.modified = False
        self._set_image(im)
        self.fit_view()
        self.set_title(os.path.basename(path))

    def _set_image(self, im):
        self.im = im
        self.canvas.set_image(im)
        self.update_status()

    def fit_view(self):
        self.canvas.fit = True
        self.canvas._refit()
        self.canvas.queue_draw()
        self.update_status()

    def step(self, d):
        if not self.siblings:
            return
        i = self.siblings.index(self.path) if self.path in self.siblings else 0
        self.load(self.siblings[(i + d) % len(self.siblings)])

    def update_status(self):
        if self.im is None:
            return
        try:
            size = human_size(os.path.getsize(self.path))
        except OSError:
            size = "?"
        i = self.siblings.index(self.path) + 1 if self.path in self.siblings else 1
        bits = ["%d × %d" % self.im.size, size, "%d%%" % round(self.canvas.zoom * 100),
                "%d of %d" % (i, len(self.siblings))]
        if self.info.get("raw"):
            bits.insert(1, "RAW")
        elif self.info.get("format"):
            bits.insert(1, self.info["format"])
        if self.modified:
            bits.append("edited")
        self.status.set_text("   ·   ".join(bits))
        self.set_title(("• " if self.modified else "") + os.path.basename(self.path))

    # ------------------------------------------------------------ editing
    def apply(self, fn):
        if self.im is None:
            return
        try:
            new = fn(self.im)
        except Exception as e:
            message(self, "That did not work", str(e), Gtk.MessageType.ERROR)
            return
        self.history.append(self.im)
        if len(self.history) > 20:
            self.history.pop(0)
        self.redo.clear()
        self.modified = True
        self._set_image(new)
        if self.canvas.fit:
            self.fit_view()

    @staticmethod
    def _rgb_only(fn, im):
        if im.mode == "RGBA":
            r, g, b, a = im.split()
            out = fn(Image.merge("RGB", (r, g, b)))
            out.putalpha(a)
            return out
        return fn(im)

    def undo(self):
        if self.history:
            self.redo.append(self.im)
            self._set_image(self.history.pop())
            self.modified = bool(self.history)
            self.update_status()

    def redo_step(self):
        if self.redo:
            self.history.append(self.im)
            self._set_image(self.redo.pop())
            self.modified = True
            self.update_status()

    def revert(self):
        if self.original is not None and self.modified:
            self.history.append(self.im)
            self.modified = False
            self._set_image(self.original)
            self.fit_view()

    def _adjust_preview(self):
        if self.im is None:
            return
        self.canvas.set_image(self._adjusted(self.im))

    def _adjusted(self, im):
        v = {k: s.get_value() for k, s in self.sliders.items()}

        def go(rgb):
            rgb = ImageEnhance.Brightness(rgb).enhance(v["brightness"])
            rgb = ImageEnhance.Contrast(rgb).enhance(v["contrast"])
            rgb = ImageEnhance.Color(rgb).enhance(v["color"])
            return ImageEnhance.Sharpness(rgb).enhance(v["sharpness"])
        return self._rgb_only(go, im)

    def _adjust_commit(self):
        self.apply(self._adjusted)
        self._adjust_reset()

    def _adjust_reset(self):
        for s in self.sliders.values():
            s.set_value(1.0)
        if self.im is not None:
            self.canvas.set_image(self.im)

    # ------------------------------------------------------------ background removal
    def _build_bg_panel(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.set_border_width(10)
        box.set_size_request(250, -1)
        box.set_hexpand(False)
        head = Gtk.Label(xalign=0)
        head.set_markup("<b>Remove background</b>")
        box.pack_start(head, False, False, 0)
        hint = Gtk.Label(xalign=0, wrap=True, max_width_chars=30,
                         label="Works best on plain or softly shaded backgrounds. The colour comes from the picture's "
                               "border; click “Pick colour” to choose another one. Save the result as PNG or WebP "
                               "to keep the transparency.")
        hint.get_style_context().add_class("dim-label")
        box.pack_start(hint, False, False, 0)
        self.bg_sliders = {}
        for key, label, lo, hi, val, step in (("tol", "Tolerance", 0, 100, 30, 1), ("shrink", "Shrink edge (px)", 0, 8, 1, 1),
                                              ("soft", "Soften edge (px)", 0, 8, 1, 0.5)):
            box.pack_start(Gtk.Label(label=label, xalign=0), False, False, 0)
            sc = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, lo, hi, step)
            sc.set_value(val)
            sc.set_digits(1 if step < 1 else 0)
            sc.connect("value-changed", lambda *_: self._bg_schedule())
            box.pack_start(sc, False, False, 0)
            self.bg_sliders[key] = sc
        self.bg_edges = Gtk.CheckButton(label="Edge-connected only", active=True)
        self.bg_edges.set_tooltip_text("On: only background connected to the picture's edges goes. Off: enclosed areas of the same colour (holes in a ring, gaps in a logo) go too")
        self.bg_edges.connect("toggled", lambda *_: self._bg_schedule())
        box.pack_start(self.bg_edges, False, False, 0)
        self.bg_pick = Gtk.ToggleButton(label="Pick colour")
        self.bg_pick.connect("toggled", self._bg_pick_toggled)
        box.pack_start(self.bg_pick, False, False, 0)
        self.bg_info = Gtk.Label(xalign=0, max_width_chars=28, ellipsize=3)
        self.bg_info.get_style_context().add_class("dim-label")
        box.pack_start(self.bg_info, False, False, 0)
        row = Gtk.Box(spacing=6)
        ok = Gtk.Button(label="Apply")
        ok.get_style_context().add_class("suggested-action")
        ok.connect("clicked", lambda *_: self._bg_commit())
        cancel = Gtk.Button(label="Cancel")
        cancel.connect("clicked", lambda *_: self.close_bg_panel())
        row.pack_start(ok, True, True, 0)
        row.pack_start(cancel, True, True, 0)
        box.pack_start(row, False, False, 6)
        return box

    def toggle_adjust_panel(self):
        if self.bg_panel.get_visible():
            self.close_bg_panel()
        self.panel.set_visible(not self.panel.get_visible())

    def toggle_bg_panel(self):
        if self.bg_panel.get_visible():
            self.close_bg_panel()
            return
        if self.im is None:
            return
        self.panel.hide()
        self._bg_colour = self._bg_seed = None
        self.bg_panel.show()
        self._bg_preview()

    def close_bg_panel(self):
        if self._bg_timer:
            GLib.source_remove(self._bg_timer)
            self._bg_timer = 0
        self.bg_pick.set_active(False)
        self.bg_panel.hide()
        if self.im is not None:
            self.canvas.set_image(self.im)

    def _bg_pick_toggled(self, btn):
        self.canvas.picking = btn.get_active()
        if btn.get_active():
            self.bg_info.set_text("Click the colour to remove.")

    def pick_background(self, x, y):
        w, h = self.im.size
        x, y = max(0, min(w - 1, x)), max(0, min(h - 1, y))
        self._bg_colour = self.im.convert("RGB").getpixel((x, y))
        self._bg_seed = (x, y)
        self.bg_pick.set_active(False)
        self._bg_preview()

    def _bg_schedule(self):
        if self._bg_timer:
            GLib.source_remove(self._bg_timer)
        self._bg_timer = GLib.timeout_add(180, self._bg_preview)

    def _bg_params(self):
        return dict(tolerance=self.bg_sliders["tol"].get_value(), edges_only=self.bg_edges.get_active(),
                    soften=self.bg_sliders["soft"].get_value(), shrink=int(self.bg_sliders["shrink"].get_value()),
                    colour=self._bg_colour, seed=self._bg_seed)

    def _bg_compute(self):
        out, colour = bgremove.remove_background(self.im, **self._bg_params())
        self.bg_info.set_text("Background colour: #%02x%02x%02x" % colour)
        return out

    def _bg_preview(self):
        self._bg_timer = 0
        if self.im is None or not self.bg_panel.get_visible():
            return False
        try:
            self.canvas.set_image(self._bg_compute())
        except Exception as e:
            self.bg_info.set_text("Could not remove the background: %s" % e)
        return False

    def _bg_commit(self):
        if self._bg_timer:
            GLib.source_remove(self._bg_timer)
            self._bg_timer = 0
        try:
            out = self._bg_compute()
        except Exception as e:
            message(self, "That did not work", str(e), Gtk.MessageType.ERROR)
            return
        self.bg_pick.set_active(False)
        self.bg_panel.hide()
        self.canvas.set_image(self.im)
        self.apply(lambda im: out)

    def resize_dialog(self):
        if self.im is None:
            return
        d = Gtk.Dialog(title="Resize image", transient_for=self.win(), modal=True)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Resize", Gtk.ResponseType.ACCEPT)
        box = d.get_content_area()
        box.set_border_width(12)
        box.set_spacing(8)
        w0, h0 = self.im.size
        grid = Gtk.Grid(row_spacing=8, column_spacing=10)
        wsp = Gtk.SpinButton.new_with_range(1, 30000, 1)
        hsp = Gtk.SpinButton.new_with_range(1, 30000, 1)
        wsp.set_value(w0)
        hsp.set_value(h0)
        keep = Gtk.CheckButton(label="Keep proportions", active=True)
        busy = [False]

        def on_w(*_):
            if keep.get_active() and not busy[0]:
                busy[0] = True
                hsp.set_value(max(1, round(wsp.get_value() * h0 / w0)))
                busy[0] = False

        def on_h(*_):
            if keep.get_active() and not busy[0]:
                busy[0] = True
                wsp.set_value(max(1, round(hsp.get_value() * w0 / h0)))
                busy[0] = False
        wsp.connect("value-changed", on_w)
        hsp.connect("value-changed", on_h)
        grid.attach(Gtk.Label(label="Width", xalign=0), 0, 0, 1, 1)
        grid.attach(wsp, 1, 0, 1, 1)
        grid.attach(Gtk.Label(label="Height", xalign=0), 0, 1, 1, 1)
        grid.attach(hsp, 1, 1, 1, 1)
        grid.attach(keep, 0, 2, 2, 1)
        box.pack_start(grid, False, False, 0)
        d.show_all()
        if d.run() == Gtk.ResponseType.ACCEPT:
            size = (int(wsp.get_value()), int(hsp.get_value()))
            d.destroy()
            self.apply(lambda im: im.resize(size, Image.LANCZOS))
        else:
            d.destroy()

    def start_crop(self):
        self.canvas.cropping = True
        self.crop_bar.show()
        self.crop_label.set_text("Drag on the picture to choose the area to keep.")

    def stop_crop(self):
        self.canvas.cropping = False
        self.canvas.crop = None
        self.crop_bar.hide()
        self.canvas.queue_draw()

    def crop_changed(self):
        c = self.canvas.crop
        if c:
            self.crop_label.set_text("Keep %d × %d pixels" % (abs(c[2] - c[0]), abs(c[3] - c[1])))

    def apply_crop(self):
        c = self.canvas.crop
        if not c or abs(c[2] - c[0]) < 2 or abs(c[3] - c[1]) < 2:
            self.crop_label.set_text("Drag a rectangle first.")
            return
        box = (int(min(c[0], c[2])), int(min(c[1], c[3])), int(max(c[0], c[2])), int(max(c[1], c[3])))
        self.stop_crop()
        self.apply(lambda im: im.crop(box))

    # ------------------------------------------------------------ saving
    def save(self):
        if rawload.is_raw(self.path):
            return self.save_as()
        if not self.modified:
            return True
        if not ask(self, "Overwrite %s?" % os.path.basename(self.path),
                   "The original file will be replaced. Use “Save a copy as…” to keep it.", "_Overwrite"):
            return False
        return self._write(self.path)

    def save_as(self, copy=False):
        d = Gtk.FileChooserDialog(title="Save a copy" if copy else "Save as", transient_for=self.win(),
                                  action=Gtk.FileChooserAction.SAVE)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Save", Gtk.ResponseType.ACCEPT)
        d.set_do_overwrite_confirmation(True)
        d.set_current_folder(self.folder)
        stem, ext = os.path.splitext(os.path.basename(self.path))
        if rawload.is_raw(self.path) or ext.lower() not in rawload.SAVE_FORMATS.values():
            ext = ".jpg"
        d.set_current_name(stem + ("-edit" if copy or rawload.is_raw(self.path) else "") + ext)
        for name, e in rawload.SAVE_FORMATS.items():
            f = Gtk.FileFilter()
            f.set_name("%s (*%s)" % (name, e))
            f.add_pattern("*" + e)
            d.add_filter(f)
        if d.run() != Gtk.ResponseType.ACCEPT:
            d.destroy()
            return False
        target = d.get_filename()
        d.destroy()
        return self._write(target, switch=not copy)

    def _write(self, target, switch=True):
        try:
            rawload.save(self.im, target)
        except (OSError, ValueError, KeyError) as e:
            message(self, "Could not save", str(e), Gtk.MessageType.ERROR)
            return False
        if switch:
            self.path = target
            self.modified = False
            self.original = self.im
            self.history.clear()
            self._scan_siblings()
        self.update_status()
        return True

    def _confirm_discard(self):
        if not self.modified:
            return True
        if ask(self, "Discard your edits?", "This picture has changes that have not been saved.", "_Discard"):
            self.modified = False
            return True
        return False

    def _recover(self, *_):
        """Closing a tab cannot ask, so an edited picture is kept as a recovery copy."""
        if self.modified and self.im is not None:
            im = self.im
            keep_recovery_copy(os.path.splitext(os.path.basename(self.path))[0] + ".png",
                               lambda p: rawload.save(im, p, "PNG"))

    # ------------------------------------------------------------ keys
    def toggle_fullscreen(self):
        if self.is_fullscreen():
            self.unfullscreen()
        else:
            self.fullscreen()

    def _key(self, w, ev):
        k = Gdk.keyval_name(ev.keyval)
        ctrl = bool(ev.state & Gdk.ModifierType.CONTROL_MASK)
        shift = bool(ev.state & Gdk.ModifierType.SHIFT_MASK)
        if self.canvas.cropping and k == "Escape":
            self.stop_crop()
            return True
        if ctrl:
            if k.lower() == "z":
                (self.redo_step if shift else self.undo)()
            elif k.lower() == "s":
                self.save_as() if shift else self.save()
            elif k.lower() == "r":
                self.apply(lambda im: im.rotate(-90, expand=True))
            elif k.lower() == "l":
                self.apply(lambda im: im.rotate(90, expand=True))
            else:
                return False
            return True
        if k in ("Left", "Page_Up", "BackSpace"):
            self.step(-1)
        elif k in ("Right", "Page_Down", "space"):
            self.step(1)
        elif k in ("plus", "equal", "KP_Add"):
            self.canvas.set_zoom(self.canvas.zoom * 1.25)
        elif k in ("minus", "KP_Subtract"):
            self.canvas.set_zoom(self.canvas.zoom / 1.25)
        elif k in ("1", "KP_1"):
            self.canvas.set_zoom(1.0)
        elif k in ("f", "0"):
            self.fit_view()
        elif k == "F11":
            self.toggle_fullscreen()
        elif k == "Escape" and self.is_fullscreen():
            self.unfullscreen()
        else:
            return False
        return True


def run(args):
    files = [a for a in args if not a.startswith("--")]
    if not files:
        d = Gtk.FileChooserDialog(title="Open image", action=Gtk.FileChooserAction.OPEN)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Open", Gtk.ResponseType.ACCEPT)
        if d.run() != Gtk.ResponseType.ACCEPT:
            raise SystemExit(0)
        files = [d.get_filename()]
        d.destroy()
    return ImagePage(files[0])
