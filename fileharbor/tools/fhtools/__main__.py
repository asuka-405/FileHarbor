"""fileharbor-tools <tool> [files...]   (tools: hub, devtools, image, media, editor, preview; also resize, music, video, markdown)"""
import os
import sys

USAGE = __doc__


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(USAGE)
        return 0
    tool, args = argv[1], argv[2:]
    import gi
    gi.require_version("Gtk", "3.0")
    from gi.repository import Gtk
    from .common import apply_app_theme
    apply_app_theme()
    from .base import standalone, ToolFailed
    if tool == "hub":
        from .hub import HubPage
        page = HubPage()
    elif tool in ("devtools", "image", "media", "editor", "preview", "resize", "music", "video", "markdown"):
        from .embed import build
        files = [os.path.abspath(a) for a in args if not a.startswith("--")]
        if tool == "devtools":
            from . import devtools as m
            page = m.run(args)
        else:
            try:
                page = build(tool, files)
            except ToolFailed as e:
                print(str(e), file=sys.stderr)
                return 1
    else:
        print("Unknown tool: %s\n%s" % (tool, USAGE), file=sys.stderr)
        return 2
    standalone(page)
    Gtk.main()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
