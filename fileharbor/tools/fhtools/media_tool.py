"""Music player and a small video player/editor. Audio uses GStreamer (ffmpeg fills in the formats it lacks);
video is decoded by ffmpeg, so every format ffmpeg knows plays, whatever GStreamer codecs are installed."""
import json
import os
import random
import shutil
import signal
import subprocess
import tempfile
import threading
import time
import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gst", "1.0")
gi.require_version("GstPbutils", "1.0")
from gi.repository import Gtk, Gdk, GLib, Gst, GstPbutils, GdkPixbuf  # noqa: E402
import cairo  # noqa: E402

from .base import ToolPage, ToolFailed
from .common import message, human_size

AUDIO_EXTS = {".mp3", ".flac", ".ogg", ".oga", ".opus", ".wav", ".m4a", ".aac", ".wma", ".aiff", ".aif", ".ape",
              ".wv", ".mka", ".mp2", ".amr", ".alac"}
VIDEO_EXTS = {".mp4", ".mkv", ".webm", ".avi", ".mov", ".m4v", ".wmv", ".flv", ".mpg", ".mpeg", ".ts", ".m2ts",
              ".3gp", ".ogv", ".vob", ".mts", ".divx", ".f4v", ".asf"}


def is_audio(p):
    return os.path.splitext(p)[1].lower() in AUDIO_EXTS


def is_video(p):
    return os.path.splitext(p)[1].lower() in VIDEO_EXTS


def fmt_time(sec):
    sec = max(0, int(sec))
    h, rem = divmod(sec, 3600)
    m, s = divmod(rem, 60)
    return "%d:%02d:%02d" % (h, m, s) if h else "%d:%02d" % (m, s)


# --------------------------------------------------------------------------- audio
class MusicPage(ToolPage):
    def __init__(self, files):
        super().__init__()
        self.set_title("Music")
        Gst.init(None)
        self.set_default_size(560, 640)
        self.tracks = []
        self.index = -1
        self.shuffle = False
        self.repeat = "off"           # off | all | one
        self.duration = 0
        self.seeking = False
        self.tmp = None               # ffmpeg-converted temp file for formats GStreamer lacks
        self.tried_ffmpeg = False
        self.player = Gst.ElementFactory.make("playbin", "player")
        self.player.set_property("video-sink", Gst.ElementFactory.make("fakesink", None))
        bus = self.player.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self._on_bus)
        self.discoverer = GstPbutils.Discoverer.new(5 * Gst.SECOND)

        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        outer.set_border_width(12)
        self.add(outer)
        top = Gtk.Box(spacing=14)
        self.cover = Gtk.Image.new_from_icon_name("audio-x-generic-symbolic", Gtk.IconSize.DIALOG)
        self.cover.set_pixel_size(112)
        self.cover.set_size_request(112, 112)
        top.pack_start(self.cover, False, False, 0)
        meta = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        meta.set_valign(Gtk.Align.CENTER)
        self.title = Gtk.Label(xalign=0, ellipsize=3)
        self.title.set_markup("<big><b>Nothing playing</b></big>")
        self.artist = Gtk.Label(xalign=0, ellipsize=3)
        self.album = Gtk.Label(xalign=0, ellipsize=3)
        self.album.get_style_context().add_class("dim-label")
        for w in (self.title, self.artist, self.album):
            meta.pack_start(w, False, False, 0)
        top.pack_start(meta, True, True, 0)
        outer.pack_start(top, False, False, 0)

        sk = Gtk.Box(spacing=8)
        self.pos_label = Gtk.Label(label="0:00")
        self.dur_label = Gtk.Label(label="0:00")
        self.scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 1, 0.1)
        self.scale.set_draw_value(False)
        self.scale.connect("button-press-event", lambda *_: setattr(self, "seeking", True) or False)
        self.scale.connect("button-release-event", self._seek_release)
        sk.pack_start(self.pos_label, False, False, 0)
        sk.pack_start(self.scale, True, True, 0)
        sk.pack_start(self.dur_label, False, False, 0)
        outer.pack_start(sk, False, False, 0)

        ctl = Gtk.Box(spacing=6)
        ctl.set_halign(Gtk.Align.CENTER)

        def btn(icon, tip, cb, toggle=False):
            b = (Gtk.ToggleButton if toggle else Gtk.Button).new()
            b.set_image(Gtk.Image.new_from_icon_name(icon, Gtk.IconSize.LARGE_TOOLBAR))
            b.set_tooltip_text(tip)
            b.set_relief(Gtk.ReliefStyle.NONE)
            b.connect("toggled" if toggle else "clicked", cb)
            return b
        self.shuffle_b = btn("media-playlist-shuffle-symbolic", "Shuffle", self._on_shuffle, True)
        self.prev_b = btn("media-skip-backward-symbolic", "Previous", lambda *_: self.prev())
        self.play_b = btn("media-playback-start-symbolic", "Play / pause (Space)", lambda *_: self.toggle())
        self.next_b = btn("media-skip-forward-symbolic", "Next", lambda *_: self.next())
        self.repeat_b = btn("media-playlist-repeat-symbolic", "Repeat: off", self._on_repeat, True)
        for b in (self.shuffle_b, self.prev_b, self.play_b, self.next_b, self.repeat_b):
            ctl.pack_start(b, False, False, 0)
        vol = Gtk.VolumeButton()
        vol.set_value(0.8)
        vol.connect("value-changed", lambda b, v: self.player.set_property("volume", v))
        self.player.set_property("volume", 0.8)
        ctl.pack_start(vol, False, False, 8)
        outer.pack_start(ctl, False, False, 0)

        self.store = Gtk.ListStore(str, str, str, str)    # path, title, artist, duration
        self.tree = Gtk.TreeView(model=self.store)
        self.tree.set_headers_visible(True)
        for i, (name, w) in enumerate((("Title", 260), ("Artist", 160), ("Time", 60))):
            col = Gtk.TreeViewColumn(name, Gtk.CellRendererText(ellipsize=3), text=i + 1)
            col.set_resizable(True)
            col.set_expand(i < 2)
            self.tree.append_column(col)
        self.tree.connect("row-activated", lambda t, p, c: self.play_index(p.get_indices()[0]))
        sw = Gtk.ScrolledWindow()
        sw.set_shadow_type(Gtk.ShadowType.IN)
        sw.set_vexpand(True)
        sw.add(self.tree)
        outer.pack_start(sw, True, True, 0)
        add = Gtk.Button(label="Add files…")
        add.connect("clicked", self._add_dialog)
        outer.pack_start(add, False, False, 0)

        self.connect("key-press-event", self._key)
        self.connect("destroy", lambda *_: self._cleanup())
        GLib.timeout_add(250, self._tick)
        self.show_all()
        self.add_files(files)
        start = 0
        if files and len(files) == 1:        # opened one file: queue its folder, start on that file
            start = max(0, next((i for i, r in enumerate(self.store) if r[0] == os.path.abspath(files[0])), 0))
        if len(self.store):
            self.play_index(start)

    # -- playlist --------------------------------------------------------
    def add_files(self, files):
        paths = []
        for f in files:
            f = os.path.abspath(f)
            if os.path.isdir(f):
                paths += [os.path.join(f, n) for n in sorted(os.listdir(f), key=str.lower) if is_audio(n)]
            else:
                paths.append(f)
        if len(paths) == 1 and is_audio(paths[0]):     # a single track: play its whole folder
            d = os.path.dirname(paths[0])
            paths = [os.path.join(d, n) for n in sorted(os.listdir(d), key=str.lower) if is_audio(n)] or paths
        for p in paths:
            if is_audio(p) and os.path.isfile(p):
                title, artist, dur = os.path.splitext(os.path.basename(p))[0], "", ""
                self.store.append([p, title, artist, dur])
        threading.Thread(target=self._scan_meta, daemon=True).start()

    def _scan_meta(self):
        for row in list(self.store):
            path = row[0]
            try:
                info = self.discoverer.discover_uri(Gst.filename_to_uri(path)) if hasattr(Gst, "filename_to_uri") else None
            except GLib.Error:
                info = None
            if info is None:
                dur = self._ffprobe_duration(path)
                if dur:
                    GLib.idle_add(self._set_meta, path, None, None, fmt_time(dur))
                continue
            tags = info.get_tags()
            title = artist = None
            if tags:
                title = tags.get_string("title")[1] if tags.get_string("title")[0] else None
                artist = tags.get_string("artist")[1] if tags.get_string("artist")[0] else None
            dur = fmt_time(info.get_duration() / Gst.SECOND)
            GLib.idle_add(self._set_meta, path, title, artist, dur)

    @staticmethod
    def _ffprobe_duration(path):
        try:
            r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "default=nw=1:nk=1", path],
                               capture_output=True, timeout=15)
            return float(r.stdout.strip() or 0)
        except (OSError, ValueError, subprocess.SubprocessError):
            return 0

    def _set_meta(self, path, title, artist, dur):
        for row in self.store:
            if row[0] == path:
                if title:
                    row[1] = title
                if artist:
                    row[2] = artist
                row[3] = dur
                break
        return False

    def _add_dialog(self, *_):
        d = Gtk.FileChooserDialog(title="Add music", transient_for=self.win(), action=Gtk.FileChooserAction.OPEN)
        d.set_select_multiple(True)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Add", Gtk.ResponseType.ACCEPT)
        if d.run() == Gtk.ResponseType.ACCEPT:
            names = d.get_filenames()
            d.destroy()
            first = len(self.store)
            self.add_files(names)
            if self.index < 0 and len(self.store) > first:
                self.play_index(first)
        else:
            d.destroy()

    # -- playback --------------------------------------------------------
    def play_index(self, i, use_original=True):
        if not 0 <= i < len(self.store):
            return
        self.index = i
        path = self.store[i][0]
        self.tried_ffmpeg = False
        self._drop_tmp()
        self.player.set_state(Gst.State.NULL)
        self.player.set_property("uri", Gst.filename_to_uri(path))
        self.player.set_state(Gst.State.PLAYING)
        self.tree.get_selection().select_path(Gtk.TreePath(i))
        self.title.set_markup("<big><b>%s</b></big>" % GLib.markup_escape_text(self.store[i][1]))
        self.artist.set_text(self.store[i][2])
        self.album.set_text(os.path.basename(os.path.dirname(path)))
        self.set_title("%s — Music" % self.store[i][1])
        self._load_cover(path)
        self.play_b.set_image(Gtk.Image.new_from_icon_name("media-playback-pause-symbolic", Gtk.IconSize.LARGE_TOOLBAR))

    def _load_cover(self, path):
        d = os.path.dirname(path)
        for n in ("cover.jpg", "cover.png", "folder.jpg", "folder.png", "front.jpg", "album.jpg", "Cover.jpg", "Folder.jpg"):
            f = os.path.join(d, n)
            if os.path.exists(f):
                try:
                    self.cover.set_from_pixbuf(GdkPixbuf.Pixbuf.new_from_file_at_scale(f, 112, 112, True))
                    return
                except GLib.Error:
                    pass
        self.cover.set_from_icon_name("audio-x-generic-symbolic", Gtk.IconSize.DIALOG)
        self.cover.set_pixel_size(112)

    def toggle(self):
        st = self.player.get_state(0)[1]
        if st == Gst.State.PLAYING:
            self.player.set_state(Gst.State.PAUSED)
            icon = "media-playback-start-symbolic"
        elif self.index < 0 and len(self.store):
            self.play_index(0)
            return
        else:
            self.player.set_state(Gst.State.PLAYING)
            icon = "media-playback-pause-symbolic"
        self.play_b.set_image(Gtk.Image.new_from_icon_name(icon, Gtk.IconSize.LARGE_TOOLBAR))

    def next(self, auto=False):
        n = len(self.store)
        if not n:
            return
        if auto and self.repeat == "one":
            return self.play_index(self.index)
        if self.shuffle and n > 1:
            i = random.choice([k for k in range(n) if k != self.index])
        else:
            i = self.index + 1
            if i >= n:
                if auto and self.repeat == "off":
                    self.player.set_state(Gst.State.PAUSED)
                    self.play_b.set_image(Gtk.Image.new_from_icon_name("media-playback-start-symbolic", Gtk.IconSize.LARGE_TOOLBAR))
                    return
                i = 0
        self.play_index(i)

    def prev(self):
        ok, pos = self.player.query_position(Gst.Format.TIME)
        if ok and pos > 3 * Gst.SECOND:
            self.player.seek_simple(Gst.Format.TIME, Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT, 0)
        else:
            self.play_index((self.index - 1) % max(1, len(self.store)))

    def _on_shuffle(self, b):
        self.shuffle = b.get_active()

    def _on_repeat(self, b):
        self.repeat = {"off": "all", "all": "one", "one": "off"}[self.repeat]
        b.handler_block_by_func(self._on_repeat)
        b.set_active(self.repeat != "off")
        b.handler_unblock_by_func(self._on_repeat)
        b.set_tooltip_text("Repeat: " + self.repeat)

    def _seek_release(self, w, ev):
        self.seeking = False
        if self.duration:
            self.player.seek_simple(Gst.Format.TIME, Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT,
                                    int(self.scale.get_value() / 100.0 * self.duration))
        return False

    def _tick(self):
        if not self._alive:
            return False
        ok_d, d = self.player.query_duration(Gst.Format.TIME)
        ok_p, p = self.player.query_position(Gst.Format.TIME)
        if ok_d and d > 0:
            self.duration = d
            self.dur_label.set_text(fmt_time(d / Gst.SECOND))
        if ok_p and not self.seeking and self.duration:
            self.scale.set_value(100.0 * p / self.duration)
            self.pos_label.set_text(fmt_time(p / Gst.SECOND))
        return True

    def _on_bus(self, bus, msg):
        t = msg.type
        if t == Gst.MessageType.EOS:
            self.next(auto=True)
        elif t == Gst.MessageType.ERROR:
            err, dbg = msg.parse_error()
            if not self.tried_ffmpeg and shutil.which("ffmpeg") and 0 <= self.index < len(self.store):
                self._play_via_ffmpeg(self.store[self.index][0])
            else:
                self.player.set_state(Gst.State.NULL)
                self.album.set_text("Cannot play this file: %s" % err.message)

    def _play_via_ffmpeg(self, path):
        """GStreamer lacks a decoder (for example AAC in .m4a): let ffmpeg convert it to FLAC and play that."""
        self.tried_ffmpeg = True
        self.album.set_text("Converting for playback…")
        self.player.set_state(Gst.State.NULL)

        def work():
            fd, tmp = tempfile.mkstemp(suffix=".flac", prefix="fh-audio-")
            os.close(fd)
            r = subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", path, "-vn", "-c:a", "flac", "-compression_level", "0", tmp],
                               capture_output=True)
            GLib.idle_add(self._after_convert, tmp, r.returncode == 0)
        threading.Thread(target=work, daemon=True).start()

    def _after_convert(self, tmp, ok):
        if not ok:
            self.album.set_text("Cannot play this file")
            os.unlink(tmp)
            return False
        self._drop_tmp()
        self.tmp = tmp
        self.player.set_property("uri", Gst.filename_to_uri(tmp))
        self.player.set_state(Gst.State.PLAYING)
        self.album.set_text(os.path.basename(os.path.dirname(self.store[self.index][0])))
        return False

    def _drop_tmp(self):
        if self.tmp and os.path.exists(self.tmp):
            try:
                os.unlink(self.tmp)
            except OSError:
                pass
        self.tmp = None

    def _cleanup(self):
        self.player.set_state(Gst.State.NULL)
        self._drop_tmp()

    def _key(self, w, ev):
        k = Gdk.keyval_name(ev.keyval)
        if k == "space" and not isinstance(self.get_focus(), Gtk.TreeView):
            self.toggle()
        elif k == "Right" and ev.state & Gdk.ModifierType.CONTROL_MASK:
            self.next()
        elif k == "Left" and ev.state & Gdk.ModifierType.CONTROL_MASK:
            self.prev()
        else:
            return False
        return True


# --------------------------------------------------------------------------- video
def probe(path):
    r = subprocess.run(["ffprobe", "-v", "error", "-print_format", "json", "-show_format", "-show_streams", path],
                       capture_output=True, timeout=30)
    if r.returncode != 0:
        raise OSError(r.stderr.decode(errors="replace").strip() or "ffprobe failed")
    return json.loads(r.stdout)


class VideoPage(ToolPage):
    MAX_W, MAX_H = 1280, 720

    def __init__(self, path):
        super().__init__()
        self.set_default_size(960, 600)
        self.path = os.path.abspath(path)
        self.pos = 0.0
        self.playing = False
        self.vproc = self.aproc = None
        self.generation = 0
        self.surface = None
        self.start_wall = 0.0
        self.start_pos = 0.0
        self.volume = 80
        self.seeking = False
        try:
            self.meta = probe(self.path)
        except (OSError, subprocess.SubprocessError, json.JSONDecodeError) as e:
            raise ToolFailed("Cannot open this video: %s" % e)
        vs = next((s for s in self.meta["streams"] if s["codec_type"] == "video"), None)
        if vs is None:
            raise ToolFailed("There is no video in %s" % os.path.basename(self.path))
        self.has_audio = any(s["codec_type"] == "audio" for s in self.meta["streams"])
        self.duration = float(self.meta["format"].get("duration") or vs.get("duration") or 0)
        w, h = int(vs["width"]), int(vs["height"])
        rot = abs(int((vs.get("tags") or {}).get("rotate", 0) or 0))
        if rot in (90, 270):
            w, h = h, w
        sc = min(self.MAX_W / w, self.MAX_H / h, 1.0)
        self.vw, self.vh = max(2, int(w * sc) & ~1), max(2, int(h * sc) & ~1)
        num, _, den = (vs.get("avg_frame_rate") or "30/1").partition("/")
        try:
            self.fps = max(1.0, min(120.0, float(num) / float(den or 1)))
        except (ValueError, ZeroDivisionError):
            self.fps = 30.0

        self.set_title(os.path.basename(self.path))
        header = Gtk.HeaderBar(show_close_button=True)
        self.set_titlebar(header)
        edit = Gtk.MenuButton(label="Edit")
        menu = Gtk.Menu()
        for label, cb in (("Trim…", self.trim_dialog), ("Convert…", self.convert_dialog),
                          ("Extract audio…", self.extract_audio), ("Make a GIF…", self.make_gif),
                          ("Save this frame as a picture", self.snapshot)):
            it = Gtk.MenuItem(label=label)
            # dialogs start after the popup menu has gone (a modal dialog inside the menu's own handler can kill the app on Wayland)
            it.connect("activate", lambda *_a, cb=cb: GLib.timeout_add(120, lambda: (cb(), False)[1]))
            menu.append(it)
        menu.show_all()
        edit.set_popup(menu)
        header.pack_end(edit)

        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.add(outer)
        self.area = Gtk.DrawingArea()
        self.area.set_hexpand(True)
        self.area.set_vexpand(True)
        self.area.connect("draw", self._draw)
        self.area.add_events(Gdk.EventMask.BUTTON_PRESS_MASK)
        self.area.connect("button-press-event", self._on_click)
        outer.pack_start(self.area, True, True, 0)
        bar = Gtk.Box(spacing=8)
        bar.set_border_width(8)
        self.play_b = Gtk.Button.new_from_icon_name("media-playback-start-symbolic", Gtk.IconSize.BUTTON)
        self.play_b.connect("clicked", lambda *_: self.toggle())
        self.pos_label = Gtk.Label(label="0:00")
        self.scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, max(self.duration, 1), 0.5)
        self.scale.set_draw_value(False)
        self.scale.connect("button-press-event", lambda *_: setattr(self, "seeking", True) or False)
        self.scale.connect("button-release-event", self._seek_release)
        self.dur_label = Gtk.Label(label=fmt_time(self.duration))
        vol = Gtk.VolumeButton()
        vol.set_value(self.volume / 100.0)
        vol.connect("value-changed", self._on_volume)
        full = Gtk.Button.new_from_icon_name("view-fullscreen-symbolic", Gtk.IconSize.BUTTON)
        full.connect("clicked", lambda *_: self._fullscreen())
        for w_, expand in ((self.play_b, False), (self.pos_label, False), (self.scale, True), (self.dur_label, False),
                           (vol, False), (full, False)):
            bar.pack_start(w_, expand, expand, 0)
        outer.pack_start(bar, False, False, 0)
        self.connect("key-press-event", self._key)
        self.connect("destroy", lambda *_: self._stop())
        GLib.timeout_add(200, self._tick)
        self.show_all()
        self.play(0.0)

    # -- decoding pipeline -------------------------------------------------
    def play(self, start):
        self._stop()
        self.generation += 1
        gen = self.generation
        self.pos = self.start_pos = max(0.0, min(start, max(self.duration - 0.1, 0)))
        self.start_wall = time.monotonic()
        self.playing = True
        self.play_b.set_image(Gtk.Image.new_from_icon_name("media-playback-pause-symbolic", Gtk.IconSize.BUTTON))
        self.vproc = subprocess.Popen(
            ["ffmpeg", "-v", "error", "-ss", "%.3f" % self.start_pos, "-i", self.path, "-an", "-sn",
             "-vf", "scale=%d:%d:flags=fast_bilinear" % (self.vw, self.vh), "-r", "%.3f" % self.fps,
             "-pix_fmt", "bgra", "-f", "rawvideo", "-"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=self.vw * self.vh * 4 * 2)
        if self.has_audio and shutil.which("ffplay"):
            self.aproc = subprocess.Popen(
                ["ffplay", "-v", "quiet", "-nodisp", "-autoexit", "-vn", "-ss", "%.3f" % self.start_pos,
                 "-volume", str(self.volume), self.path],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        threading.Thread(target=self._reader, args=(gen, self.vproc), daemon=True).start()

    def _reader(self, gen, proc):
        size = self.vw * self.vh * 4
        n = 0
        t0 = time.monotonic()
        while gen == self.generation:
            buf = proc.stdout.read(size)
            if len(buf) < size:
                break
            due = t0 + n / self.fps                      # pace frames against the wall clock
            delay = due - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            elif delay < -0.25:                          # far behind: skip ahead instead of slowing down
                n += int(-delay * self.fps)
                continue
            n += 1
            GLib.idle_add(self._show, gen, bytearray(buf), n / self.fps + self.start_pos)
        GLib.idle_add(self._ended, gen)

    def _show(self, gen, data, pos):
        if gen == self.generation:
            self.surface = cairo.ImageSurface.create_for_data(data, cairo.FORMAT_ARGB32, self.vw, self.vh, self.vw * 4)
            self._frame_data = data
            self.pos = pos
            self.area.queue_draw()
        return False

    def _ended(self, gen):
        if gen == self.generation and self.playing:
            self.playing = False
            self.play_b.set_image(Gtk.Image.new_from_icon_name("media-playback-start-symbolic", Gtk.IconSize.BUTTON))
            self.pos = self.duration
        return False

    def _stop(self):
        self.generation += 1
        for p in (self.vproc, self.aproc):
            if p and p.poll() is None:
                try:
                    p.send_signal(signal.SIGTERM)
                except OSError:
                    pass
        self.vproc = self.aproc = None
        self.playing = False

    def toggle(self):
        if self.playing:
            pos = self.pos
            self._stop()
            self.pos = pos
            self.play_b.set_image(Gtk.Image.new_from_icon_name("media-playback-start-symbolic", Gtk.IconSize.BUTTON))
        else:
            self.play(self.pos if self.pos < self.duration - 0.2 else 0.0)

    def _seek_release(self, w, ev):
        self.seeking = False
        target = self.scale.get_value()
        was = self.playing
        if was:
            self.play(target)
        else:
            self.pos = target
            self.play(target)
            GLib.timeout_add(300, lambda: (self.toggle(), False)[1])
        return False

    def _on_volume(self, b, v):
        self.volume = int(v * 100)       # applied from the next start; ffplay cannot change it live

    def _tick(self):
        if not self._alive:
            return False
        if not self.seeking:
            self.scale.set_value(self.pos)
            self.pos_label.set_text(fmt_time(self.pos))
        return True

    def _draw(self, w, cr):
        W, H = w.get_allocated_width(), w.get_allocated_height()
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        if self.surface:
            s = min(W / self.vw, H / self.vh)
            cr.translate((W - self.vw * s) / 2, (H - self.vh * s) / 2)
            cr.scale(s, s)
            cr.set_source_surface(self.surface, 0, 0)
            cr.get_source().set_filter(cairo.FILTER_BILINEAR)
            cr.paint()
        return False

    def _on_click(self, w, ev):
        if ev.type == Gdk.EventType._2BUTTON_PRESS:
            self._fullscreen()
        elif ev.type == Gdk.EventType.BUTTON_PRESS:
            self.toggle()
        return True

    def _fullscreen(self):
        if self.is_fullscreen():
            self.unfullscreen()
        else:
            self.fullscreen()

    def _key(self, w, ev):
        k = Gdk.keyval_name(ev.keyval)
        if k == "space":
            self.toggle()
        elif k == "Left":
            self.play(self.pos - 5)
        elif k == "Right":
            self.play(self.pos + 5)
        elif k == "f" or k == "F11":
            self._fullscreen()
        elif k == "Escape":
            self.unfullscreen()
        else:
            return False
        return True

    # -- editing with ffmpeg -----------------------------------------------
    def _out_name(self, suffix, ext):
        stem = os.path.splitext(self.path)[0]
        out = "%s-%s%s" % (stem, suffix, ext)
        n = 2
        while os.path.exists(out):
            out = "%s-%s-%d%s" % (stem, suffix, n, ext)
            n += 1
        return out

    def _run_ffmpeg(self, args, out, title):
        d = Gtk.MessageDialog(transient_for=self.win(), modal=True, message_type=Gtk.MessageType.INFO,
                              buttons=Gtk.ButtonsType.NONE, text=title)
        d.format_secondary_text("This can take a while for long videos.")
        result = {}

        def work():
            r = subprocess.run(["ffmpeg", "-y", "-v", "error", *args, out], capture_output=True)
            result["rc"], result["err"] = r.returncode, r.stderr.decode(errors="replace")
            GLib.idle_add(d.response, Gtk.ResponseType.OK)
        threading.Thread(target=work, daemon=True).start()
        d.run()
        d.destroy()
        if result.get("rc") != 0:
            message(self, "ffmpeg failed", result.get("err", "")[-600:], Gtk.MessageType.ERROR)
            if os.path.exists(out):
                os.unlink(out)
        else:
            message(self, "Saved", "%s\n(%s)" % (os.path.basename(out), human_size(os.path.getsize(out))))

    def _time_dialog(self, title, with_end=True):
        d = Gtk.Dialog(title=title, transient_for=self.win(), modal=True)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_OK", Gtk.ResponseType.ACCEPT)
        box = d.get_content_area()
        box.set_border_width(12)
        box.set_spacing(8)
        a = Gtk.SpinButton.new_with_range(0, max(self.duration, 1), 0.1)
        a.set_value(self.pos)
        b = Gtk.SpinButton.new_with_range(0, max(self.duration, 1), 0.1)
        b.set_value(self.duration)
        g = Gtk.Grid(row_spacing=8, column_spacing=10)
        g.attach(Gtk.Label(label="Start (seconds)", xalign=0), 0, 0, 1, 1)
        g.attach(a, 1, 0, 1, 1)
        if with_end:
            g.attach(Gtk.Label(label="End (seconds)", xalign=0), 0, 1, 1, 1)
            g.attach(b, 1, 1, 1, 1)
        box.pack_start(g, False, False, 0)
        d.show_all()
        ok = d.run() == Gtk.ResponseType.ACCEPT
        vals = (a.get_value(), b.get_value())
        d.destroy()
        return ok, vals

    def trim_dialog(self, *_):
        ok, (a, b) = self._time_dialog("Trim")
        if not ok or b <= a:
            return
        d = Gtk.MessageDialog(transient_for=self.win(), modal=True, message_type=Gtk.MessageType.QUESTION,
                              buttons=Gtk.ButtonsType.NONE, text="How should the video be cut?")
        d.format_secondary_text("Exact cut re-encodes the video (slower, frame-accurate).\n"
                                "Fast cut copies it without re-encoding, but can start a few seconds early.")
        d.add_button("_Cancel", Gtk.ResponseType.CANCEL)
        d.add_button("_Fast cut", Gtk.ResponseType.NO)
        d.add_button("_Exact cut", Gtk.ResponseType.YES)
        r = d.run()
        d.destroy()
        if r not in (Gtk.ResponseType.YES, Gtk.ResponseType.NO):
            return
        if self.playing:
            self.toggle()
        ext = os.path.splitext(self.path)[1] or ".mp4"
        out = self._out_name("trim", ext)
        if r == Gtk.ResponseType.NO:
            args = ["-ss", "%.3f" % a, "-to", "%.3f" % b, "-i", self.path, "-c", "copy", "-avoid_negative_ts", "make_zero"]
        else:
            args = ["-ss", "%.3f" % a, "-to", "%.3f" % b, "-i", self.path, "-c:v", "libx264", "-crf", "18", "-preset", "fast", "-c:a", "aac"]
        self._run_ffmpeg(args, out, "Trimming…")

    def convert_dialog(self, *_):
        d = Gtk.Dialog(title="Convert", transient_for=self.win(), modal=True)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Convert", Gtk.ResponseType.ACCEPT)
        box = d.get_content_area()
        box.set_border_width(12)
        box.set_spacing(8)
        combo = Gtk.ComboBoxText()
        presets = [("MP4 (H.264, plays everywhere)", ".mp4", ["-c:v", "libx264", "-crf", "23", "-preset", "medium", "-c:a", "aac", "-movflags", "+faststart"]),
                   ("MP4 (H.265, smaller)", ".mp4", ["-c:v", "libx265", "-crf", "28", "-c:a", "aac", "-tag:v", "hvc1"]),
                   ("WebM (VP9)", ".webm", ["-c:v", "libvpx-vp9", "-crf", "32", "-b:v", "0", "-c:a", "libopus"]),
                   ("MKV (keep streams)", ".mkv", ["-c", "copy"]),
                   ("MP4 720p (small)", ".mp4", ["-vf", "scale=-2:720", "-c:v", "libx264", "-crf", "26", "-c:a", "aac"])]
        for p in presets:
            combo.append_text(p[0])
        combo.set_active(0)
        box.pack_start(combo, False, False, 0)
        d.show_all()
        if d.run() == Gtk.ResponseType.ACCEPT:
            _, ext, args = presets[combo.get_active()]
            d.destroy()
            out = self._out_name("converted", ext)
            self._run_ffmpeg(["-i", self.path, *args], out, "Converting…")
        else:
            d.destroy()

    def extract_audio(self, *_):
        out = self._out_name("audio", ".mp3")
        self._run_ffmpeg(["-i", self.path, "-vn", "-c:a", "libmp3lame", "-q:a", "2"], out, "Extracting audio…")

    def make_gif(self, *_):
        ok, (a, b) = self._time_dialog("Make a GIF")
        if ok and b > a:
            out = self._out_name("clip", ".gif")
            self._run_ffmpeg(["-ss", "%.3f" % a, "-to", "%.3f" % min(b, a + 30), "-i", self.path,
                              "-vf", "fps=12,scale=480:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse", "-loop", "0"],
                             out, "Making the GIF…")

    def snapshot(self, *_):
        out = self._out_name("frame", ".png")
        self._run_ffmpeg(["-ss", "%.3f" % self.pos, "-i", self.path, "-frames:v", "1"], out, "Saving the frame…")


def run(args, kind):
    files = [a for a in args if not a.startswith("--")]
    if kind == "music":
        return MusicPage(files)
    if not files:
        d = Gtk.FileChooserDialog(title="Open video", action=Gtk.FileChooserAction.OPEN)
        d.add_buttons("_Cancel", Gtk.ResponseType.CANCEL, "_Open", Gtk.ResponseType.ACCEPT)
        if d.run() != Gtk.ResponseType.ACCEPT:
            raise SystemExit(0)
        files = [d.get_filename()]
        d.destroy()
    return VideoPage(files[0])
