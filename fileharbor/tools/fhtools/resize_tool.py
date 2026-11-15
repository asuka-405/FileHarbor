"""Batch image resizing / converting: pick files in the file manager, choose a size, done."""
import os
import threading
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib  # noqa: E402

from PIL import Image

from . import rawload
from .base import ToolPage
from .common import human_size, message

PRESETS = [("Custom", None), ("Email / chat (1280 px)", 1280), ("Web (1920 px)", 1920), ("HD 1080p (1920 px)", 1920),
           ("4K (3840 px)", 3840), ("Thumbnail (320 px)", 320), ("Avatar (256 px)", 256)]


def target_size(w, h, mode, value):
    """mode: percent | width | height | longest | exact(value=(w, h)); never returns 0."""
    if mode == "percent":
        f = value / 100.0
        return max(1, round(w * f)), max(1, round(h * f))
    if mode == "width":
        return value, max(1, round(h * value / w))
    if mode == "height":
        return max(1, round(w * value / h)), value
    if mode == "longest":
        f = value / max(w, h)
        return max(1, round(w * f)), max(1, round(h * f))
    return value


def resize_one(src, out_dir, mode, value, fmt, quality, suffix, no_upscale, strip_exif):
    im, info = rawload.load(src)
    w, h = im.size
    nw, nh = target_size(w, h, mode, value)
    if no_upscale and (nw > w or nh > h):
        nw, nh = w, h
    if (nw, nh) != (w, h):
        im = im.resize((nw, nh), Image.LANCZOS)
    stem = os.path.splitext(os.path.basename(src))[0]
    ext = rawload.SAVE_FORMATS[fmt] if fmt != "Same as original" else (
        os.path.splitext(src)[1].lower() if not rawload.is_raw(src) and os.path.splitext(src)[1].lower() in rawload.SAVE_FORMATS.values() else ".jpg")
    real_fmt = {v: k for k, v in rawload.SAVE_FORMATS.items()}[ext] if ext in rawload.SAVE_FORMATS.values() else "JPEG"
    out = os.path.join(out_dir, stem + suffix + ext)
    n = 1
    while os.path.exists(out) and os.path.abspath(out) == os.path.abspath(src):   # never overwrite the source
        out = os.path.join(out_dir, "%s%s-%d%s" % (stem, suffix, n, ext))
        n += 1
    rawload.save(im, out, real_fmt, quality)
    return out, (w, h), (nw, nh)


class ResizePage(ToolPage):
    def __init__(self, files):
        super().__init__()
        self.set_title("Resize pictures")
        self.files = [f for f in files if rawload.is_image(f)]
        self.set_default_size(560, 640)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        box.set_border_width(14)
        self.add(box)

        self.count = Gtk.Label(xalign=0)
        box.pack_start(self.count, False, False, 0)

        grid = Gtk.Grid(row_spacing=8, column_spacing=12)
        box.pack_start(grid, False, False, 0)
        r = 0

        def row(label, widget):
            nonlocal r
            l = Gtk.Label(label=label, xalign=0)
            grid.attach(l, 0, r, 1, 1)
            grid.attach(widget, 1, r, 1, 1)
            widget.set_hexpand(True)
            r += 1

        self.preset = Gtk.ComboBoxText()
        for n, _ in PRESETS:
            self.preset.append_text(n)
        self.preset.set_active(0)
        self.preset.connect("changed", self._on_preset)
        row("Preset", self.preset)

        self.mode = Gtk.ComboBoxText()
        for m in ("Percentage of original", "Width (pixels)", "Height (pixels)", "Longest side (pixels)"):
            self.mode.append_text(m)
        self.mode.set_active(3)
        self.mode.connect("changed", self._on_mode)
        row("Resize by", self.mode)

        self.value = Gtk.SpinButton.new_with_range(1, 30000, 1)
        self.value.set_value(1920)
        row("Size", self.value)

        self.fmt = Gtk.ComboBoxText()
        for f in ["Same as original"] + list(rawload.SAVE_FORMATS):
            self.fmt.append_text(f)
        self.fmt.set_active(0)
        row("Format", self.fmt)

        self.quality = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 30, 100, 1)
        self.quality.set_value(88)
        self.quality.set_value_pos(Gtk.PositionType.RIGHT)
        row("Quality (JPEG, WebP)", self.quality)

        self.suffix = Gtk.Entry(text="-resized")
        row("Add to file name", self.suffix)

        self.same_folder = Gtk.RadioButton.new_with_label(None, "Same folder as the originals")
        self.sub_folder = Gtk.RadioButton.new_with_label_from_widget(self.same_folder, "A new “resized” folder next to them")
        self.sub_folder.set_active(True)
        self.other = Gtk.FileChooserButton(title="Choose a folder", action=Gtk.FileChooserAction.SELECT_FOLDER)
        row("Save in", self.sub_folder)
        row("", self.same_folder)
        row("or another folder", self.other)

        self.noup = Gtk.CheckButton(label="Never enlarge small pictures", active=True)
        box.pack_start(self.noup, False, False, 0)

        self.progress = Gtk.ProgressBar(show_text=True)
        box.pack_start(self.progress, False, False, 0)
        self.log = Gtk.TextView(editable=False, monospace=True)
        sw = Gtk.ScrolledWindow()
        sw.set_vexpand(True)
        sw.set_shadow_type(Gtk.ShadowType.IN)
        sw.add(self.log)
        box.pack_start(sw, True, True, 0)

        self.go = Gtk.Button(label="Resize")
        self.go.get_style_context().add_class("suggested-action")
        self.go.connect("clicked", self._go)
        box.pack_start(self.go, False, False, 0)
        self._update_count()
        self._on_mode()
        self.show_all()

    def _update_count(self):
        total = 0
        for f in self.files:
            try:
                total += os.path.getsize(f)
            except OSError:
                pass
        self.count.set_markup("<b>%d picture%s</b> selected (%s)" % (len(self.files), "" if len(self.files) == 1 else "s", human_size(total)))

    def _on_preset(self, *_):
        v = PRESETS[self.preset.get_active()][1]
        if v:
            self.mode.set_active(3)
            self.value.set_value(v)

    def _on_mode(self, *_):
        if self.mode.get_active() == 0:
            self.value.set_range(1, 1000)
            self.value.set_value(50)
        else:
            self.value.set_range(1, 30000)
            if self.value.get_value() <= 1000 and self.value.get_value() == 50:
                self.value.set_value(1920)

    def _go(self, *_):
        if not self.files:
            message(self, "No pictures to resize", "Select pictures in the file manager first.")
            return
        self.go.set_sensitive(False)
        mode = ("percent", "width", "height", "longest")[self.mode.get_active()]
        args = dict(mode=mode, value=int(self.value.get_value()), fmt=self.fmt.get_active_text(),
                    quality=int(self.quality.get_value()), suffix=self.suffix.get_text(), no_upscale=self.noup.get_active(),
                    strip_exif=True)
        buf = self.log.get_buffer()
        buf.set_text("")

        def log(line):
            buf.insert(buf.get_end_iter(), line + "\n")
            return False

        def work():
            ok = bad = 0
            for i, f in enumerate(self.files, 1):
                try:
                    out_dir = self._outdir_threadsafe(f)
                    out, a, b = resize_one(f, out_dir, **args)
                    GLib.idle_add(log, "✓ %s  %d×%d → %d×%d  (%s)" % (os.path.basename(f), a[0], a[1], b[0], b[1], human_size(os.path.getsize(out))))
                    ok += 1
                except Exception as e:
                    GLib.idle_add(log, "✗ %s: %s" % (os.path.basename(f), e))
                    bad += 1
                GLib.idle_add(self.progress.set_fraction, i / len(self.files))
                GLib.idle_add(self.progress.set_text, "%d of %d" % (i, len(self.files)))
            GLib.idle_add(self._done, ok, bad)

        # choose output folders on the main thread (the file chooser is a widget)
        self._mode_same = self.same_folder.get_active()
        self._mode_sub = self.sub_folder.get_active()
        self._other = self.other.get_filename()
        threading.Thread(target=work, daemon=True).start()

    def _outdir_threadsafe(self, src):
        if self._mode_sub:
            d = os.path.join(os.path.dirname(src), "resized")
            os.makedirs(d, exist_ok=True)
            return d
        if self._mode_same:
            return os.path.dirname(src)
        return self._other or os.path.dirname(src)

    def _done(self, ok, bad):
        self.go.set_sensitive(True)
        self.progress.set_text("Done: %d resized%s" % (ok, (", %d failed" % bad) if bad else ""))
        return False


def run(args):
    files = [os.path.abspath(a) for a in args if not a.startswith("--")]
    if not files:
        d = Gtk.FileChooserDialog(title="Choose pictures to resize", action=Gtk.FileChooserAction.OPEN)
        d.set_select_multiple(True)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Choose", Gtk.ResponseType.ACCEPT)
        if d.run() != Gtk.ResponseType.ACCEPT:
            raise SystemExit(0)
        files = d.get_filenames()
        d.destroy()
    return ResizePage(files)
