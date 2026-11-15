"""GtkSourceView colour schemes for the text editor, generated from the same themes as the Markdown preview
(so a theme looks the same in the editor and in the rendered document)."""
import html
import os

from gi.repository import GLib

from . import mdthemes

try:
    from pygments.styles import get_style_by_name
    from pygments.token import Token
except Exception:
    get_style_by_name = None


def _rgb(h):
    h = h.lstrip("#")
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def mix(a, b, t):
    """t=0 gives a, t=1 gives b."""
    ra, rb = _rgb(a), _rgb(b)
    return "#%02x%02x%02x" % tuple(round(x + (y - x) * t) for x, y in zip(ra, rb))


def _tok(style, token, *fallbacks):
    for t in (token,) + fallbacks:
        try:
            d = style.style_for_token(t)
        except Exception:
            continue
        if d.get("color"):
            return "#" + d["color"], d
    return None, {}


def scheme_xml(sid, name, th):
    fg, bg, accent, dim, line, code = th["fg"], th["bg"], th["accent"], th["dim"], th["line"], th["code"]
    style = None
    if get_style_by_name is not None:
        try:
            style = get_style_by_name(th.get("pygments", "default"))
        except Exception:
            style = None

    def syn(*toks, default=None):
        if style is None:
            return default or fg, {}
        c, d = _tok(style, *toks)
        return (c or default or fg), d

    rows = []

    def add(gs_name, color, d=None, **kw):
        attrs = ['name="%s"' % gs_name]
        if color:
            attrs.append('foreground="%s"' % color)
        d = d or {}
        if d.get("bold") or kw.get("bold"):
            attrs.append('bold="true"')
        if d.get("italic") or kw.get("italic"):
            attrs.append('italic="true"')
        if kw.get("underline"):
            attrs.append('underline="true"')
        if kw.get("background"):
            attrs.append('background="%s"' % kw["background"])
        rows.append("  <style %s/>" % " ".join(attrs))

    ui = [
        '<style name="text" foreground="%s" background="%s"/>' % (fg, bg),
        '<style name="selection" background="%s"/>' % mix(bg, accent, 0.35),
        '<style name="cursor" foreground="%s"/>' % fg,
        '<style name="current-line" background="%s"/>' % mix(bg, fg, 0.06),
        '<style name="line-numbers" foreground="%s" background="%s"/>' % (mix(bg, dim, 0.85), bg),
        '<style name="current-line-number" foreground="%s" bold="true"/>' % fg,
        '<style name="bracket-match" foreground="%s" background="%s" bold="true"/>' % (fg, mix(bg, accent, 0.45)),
        '<style name="bracket-mismatch" foreground="#ffffff" background="#c83c3c"/>',
        '<style name="right-margin" foreground="%s"/>' % mix(bg, fg, 0.15),
        '<style name="draw-spaces" foreground="%s"/>' % mix(bg, fg, 0.28),
        '<style name="search-match" background="%s"/>' % mix(bg, accent, 0.4),
    ]
    c, d = syn(Token.Comment if style else None, default=dim)
    add("def:comment", c, d, italic=True)
    add("def:shebang", c, d)
    c2, d2 = syn(Token.Comment.Special, Token.Comment, default=dim)
    add("def:note", c2, d2, bold=True)
    add("def:doc-comment-element", c2, d2)
    cs, ds = syn(Token.String, default=accent)
    add("def:string", cs, ds)
    add("def:inline-code", cs, ds)
    add("def:preformatted-section", cs, ds)
    add("def:character", cs, ds)
    ce, de = syn(Token.String.Escape, Token.String, default=accent)
    add("def:special-char", ce, de)
    cn, dn = syn(Token.Number, default=accent)
    for k in ("def:number", "def:decimal", "def:floating-point", "def:base-n-integer", "def:boolean"):
        add(k, cn, dn)
    cc, dc = syn(Token.Name.Constant, Token.Keyword.Constant, Token.Number, default=accent)
    add("def:constant", cc, dc)
    ck, dk = syn(Token.Keyword, default=accent)
    add("def:keyword", ck, dk, bold=bool(dk.get("bold")))
    add("def:statement", ck, dk)
    add("def:operator", syn(Token.Operator, default=fg)[0])
    ct, dt = syn(Token.Keyword.Type, Token.Name.Class, default=accent)
    add("def:type", ct, dt)
    cf, df = syn(Token.Name.Function, Token.Name, default=accent)
    add("def:function", cf, df)
    cb, db = syn(Token.Name.Builtin, Token.Name.Function, default=accent)
    add("def:builtin", cb, db)
    cp, dp = syn(Token.Comment.Preproc, Token.Keyword.Namespace, Token.Keyword, default=accent)
    add("def:preprocessor", cp, dp)
    add("def:identifier", None)
    ch, dh = syn(Token.Generic.Heading, Token.Keyword, default=accent)
    add("def:heading", ch, dh, bold=True)
    add("def:emphasis", None, italic=True)
    add("def:strong-emphasis", None, bold=True)
    add("def:link-text", accent, underline=True)
    add("def:link-destination", accent, underline=True)
    add("def:underlined", accent, underline=True)
    add("def:deletion", syn(Token.Generic.Deleted, default="#d9534f")[0])
    add("def:insertion", syn(Token.Generic.Inserted, default="#3fa66b")[0])
    add("def:error", syn(Token.Error, default="#e5484d")[0], bold=True)
    add("def:warning", "#e5a50a")
    add("def:type-hint", ct, dt)
    add("def:specials", ce, de)
    add("def:reserved", ck, dk)
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<style-scheme id="%s" name="%s" version="1.0">\n  %s\n%s\n</style-scheme>\n'
            % (sid, html.escape(name), "\n  ".join(ui), "\n".join(rows)))


def scheme_id(theme_id, dark):
    return "fh-" + (theme_id if theme_id in mdthemes.THEMES else ("auto-dark" if dark else "auto-light"))


def install(manager):
    """Writes every scheme into the user cache and tells GtkSourceView to look there."""
    d = os.path.join(GLib.get_user_cache_dir(), "fileharbor", "schemes")
    try:
        os.makedirs(d, exist_ok=True)
        items = [(scheme_id(t, True), mdthemes.THEMES[t]["name"], mdthemes.THEMES[t]) for t in mdthemes.ORDER]
        items += [("fh-auto-dark", "Follow FileHarbor (dark)", mdthemes.auto_theme(True)),
                  ("fh-auto-light", "Follow FileHarbor (light)", mdthemes.auto_theme(False))]
        for sid, name, th in items:
            xml = scheme_xml(sid, name, th)
            p = os.path.join(d, sid + ".xml")
            try:
                with open(p) as f:
                    if f.read() == xml:
                        continue
            except OSError:
                pass
            with open(p, "w") as f:
                f.write(xml)
    except OSError:
        return
    if d not in (manager.get_search_path() or []):
        manager.append_search_path(d)
        manager.force_rescan()
