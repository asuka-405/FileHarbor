"""The Tools page: every built-in tool as a card, opened in a new tab."""
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk  # noqa: E402

from .base import ToolPage

TOOLS = [
    ("editor", "Text editor", "accessories-text-editor-symbolic",
     "Code and text with syntax highlighting for the languages you use. Search, replace, format with Prettier. "
     "Markdown and MDX show a live preview you can collapse the editor from."),
    ("image", "Images", "image-x-generic-symbolic",
     "View, edit and batch-resize pictures: JPEG, PNG, WebP, camera RAW and more. Select several to resize them at once."),
    ("media", "Media player", "applications-multimedia-symbolic",
     "Music and video in one player. Trim, convert, extract audio, make a GIF."),
    ("devtools", "Developer tools", "applications-development-symbolic",
     "JSON, YAML, Base64, URL and HTML encoding, hashes, UUIDs, passwords, timestamps, colours, regex, JWT, diff."),
]


class HubPage(ToolPage):
    def __init__(self):
        super().__init__()
        self.set_title("Tools")
        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        box.set_border_width(20)
        head = Gtk.Label(xalign=0)
        head.set_markup("<span size='x-large' weight='bold'>Tools</span>")
        sub = Gtk.Label(label="Choose a tool. It opens in a new tab. Select files first, or right-click a file, "
                              "to open them directly.", xalign=0)
        sub.set_line_wrap(True)
        sub.get_style_context().add_class("dim-label")
        box.pack_start(head, False, False, 0)
        box.pack_start(sub, False, False, 0)
        flow = Gtk.FlowBox()
        flow.set_selection_mode(Gtk.SelectionMode.NONE)
        flow.set_homogeneous(True)
        flow.set_min_children_per_line(1)
        flow.set_max_children_per_line(4)
        flow.set_column_spacing(12)
        flow.set_row_spacing(12)
        for tool, title, icon, desc in TOOLS:
            btn = Gtk.Button()
            btn.set_relief(Gtk.ReliefStyle.NONE)
            card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
            card.set_border_width(14)
            img = Gtk.Image.new_from_icon_name(icon, Gtk.IconSize.DIALOG)
            img.set_pixel_size(48)
            img.set_halign(Gtk.Align.START)
            t = Gtk.Label(xalign=0)
            t.set_markup("<b>%s</b>" % title)
            d = Gtk.Label(label=desc, xalign=0)
            d.set_line_wrap(True)
            d.set_max_width_chars(34)
            d.get_style_context().add_class("dim-label")
            card.pack_start(img, False, False, 0)
            card.pack_start(t, False, False, 0)
            card.pack_start(d, False, False, 0)
            btn.add(card)
            btn.connect("clicked", self._open, tool)
            flow.add(btn)
        box.pack_start(flow, False, False, 0)
        sw.add(box)
        self.pack_start(sw, True, True, 0)
        self.show_all()

    def _open(self, button, tool):
        if not self.open_uri("x-fileharbor-tool:///" + tool, True):
            import subprocess
            subprocess.Popen(["fileharbor-tools", tool])
