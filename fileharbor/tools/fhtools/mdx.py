"""MDX support for the Markdown viewer.

MDX is Markdown with JSX components, `import`/`export` lines and `{expressions}`. A viewer cannot run
the components, so they are shown as labelled boxes whose Markdown content still renders, callouts
(Callout, Note, Warning, Tip ...) get a coloured edge, imports/exports collapse into a small block, and
`{expressions}` are shown as code chips. Front matter is rendered as a small table for .md as well."""
import html
import re

try:
    import yaml
except Exception:
    yaml = None

FENCE = re.compile(r"^([ \t]*)(`{3,}|~{3,})[^\n]*\n.*?^\1\2[ \t]*$|^([ \t]*)(`{3,}|~{3,})[^\n]*\n.*\Z",
                   re.M | re.S)
INLINE_CODE = re.compile(r"(`+)(?!`).+?(?<!`)\1(?!`)", re.S)
CALLOUTS = {"callout", "note", "warning", "tip", "info", "danger", "caution", "admonition", "alert", "success", "error"}

TAG_OPEN = re.compile(
    r"<(?P<name>[A-Z][\w]*(?:\.[\w]+)*)"
    r"(?P<props>(?:\"[^\"]*\"|'[^']*'|\{(?:[^{}]|\{(?:[^{}]|\{[^{}]*\})*\})*\}|[^<>\"'{}])*?)"
    r"(?P<self>/?)>", re.S)
TAG_CLOSE = re.compile(r"</(?P<name>[A-Z][\w]*(?:\.[\w]+)*)\s*>")
FRAGMENT = re.compile(r"</?>")
TOKEN = re.compile(TAG_OPEN.pattern + "|" + TAG_CLOSE.pattern.replace("(?P<name>", "(?P<cname>"), re.S)
EXPR = re.compile(r"\{((?:[^{}\n]|\{[^{}\n]*\})+)\}")
COMMENT = re.compile(r"\{\s*/\*.*?\*/\s*\}", re.S)


def split_frontmatter(text):
    """Returns (front matter html or "", remaining text)."""
    m = re.match(r"\A---[ \t]*\n(.*?)\n(?:---|\.\.\.)[ \t]*(?:\n|\Z)", text, re.S)
    if not m:
        return "", text
    raw = m.group(1)
    data = None
    if yaml is not None:
        try:
            data = yaml.safe_load(raw)
        except Exception:
            data = None
    if not isinstance(data, dict):
        data = {}
        for line in raw.splitlines():
            if ":" in line and not line.startswith((" ", "\t", "-")):
                k, v = line.split(":", 1)
                data[k.strip()] = v.strip()
    if not data:
        return "", text[m.end():]
    rows = "".join("<tr><th>%s</th><td>%s</td></tr>" % (html.escape(str(k)), html.escape(_show(v)))
                   for k, v in data.items())
    return '<div class="frontmatter"><table>%s</table></div>' % rows, text[m.end():]


def _show(v):
    if isinstance(v, (list, tuple)):
        return ", ".join(_show(i) for i in v)
    if isinstance(v, dict):
        return ", ".join("%s: %s" % (k, _show(i)) for k, i in v.items())
    return str(v)


def _extract_esm(prose):
    """Pulls top-level import/export statements out; returns (text, [statements])."""
    out, esm = [], []
    lines = prose.split("\n")
    i = 0
    while i < len(lines):
        ln = lines[i]
        if re.match(r"(import|export)\s", ln):
            block, depth = [], 0
            while i < len(lines):
                cur = lines[i]
                if not cur.strip() and depth <= 0:
                    break
                block.append(cur)
                depth += sum(cur.count(c) for c in "({[") - sum(cur.count(c) for c in ")}]")
                i += 1
            esm.append("\n".join(block))
            continue
        out.append(ln)
        i += 1
    return "\n".join(out), esm


def _props(p):
    p = " ".join(p.split())
    return p if len(p) <= 160 else p[:157] + "..."


def _convert_prose(prose, stash):
    """Components, comments and expressions in one stretch of prose (no code blocks in it)."""
    def keep(s):
        stash.append(s)
        return "\x01%d\x01" % (len(stash) - 1)

    prose = COMMENT.sub("", prose)
    prose = FRAGMENT.sub("", prose)
    stack = []

    def tag(m):
        before = m.string[:m.start()]
        line_start = before.rsplit("\n", 1)[-1].strip() == ""
        after = m.string[m.end():].split("\n", 1)[0].strip() == ""
        if m.group("cname"):
            if not stack:
                return ""
            kind = stack.pop()
            return keep("\n\n</div>\n\n" if kind == "block" else "</span>")
        name, props, selfc = m.group("name"), _props(m.group("props")), bool(m.group("self"))
        base = name.split(".")[-1].lower()
        kind = re.search(r"type\s*=\s*[\"']?(\w+)", props)
        if base in CALLOUTS and kind:
            base = kind.group(1).lower()
        cls = "mdx-component" + (" mdx-callout mdx-kind-" + base if name.split(".")[-1].lower() in CALLOUTS else "")
        label = '<div class="mdx-label">%s <span class="props">%s</span></div>' % (html.escape(name), html.escape(props))
        if line_start and after:
            if selfc:
                return keep('\n\n<div class="%s">%s</div>\n\n' % (cls, label))
            stack.append("block")
            return keep('\n\n<div class="%s" markdown="1">\n%s\n\n' % (cls, label))
        if selfc:
            return keep('<code class="mdx-tag">&lt;%s%s/&gt;</code>' % (html.escape(name), " …" if props else ""))
        stack.append("inline")
        return keep('<span class="mdx-inline"><code class="mdx-tag">&lt;%s&gt;</code> ' % html.escape(name))

    prose = TOKEN.sub(tag, prose)
    while stack:                                            # unclosed components: close them
        prose += "\n\n</div>\n\n" if stack.pop() == "block" else "</span>"
    prose = EXPR.sub(lambda m: keep('<code class="mdx-expr">{%s}</code>' % html.escape(m.group(1))), prose)
    return prose


def convert(text, mdx):
    """Returns (markdown text, html to put before it). Plain Markdown only gets front matter handled."""
    head, text = split_frontmatter(text)
    if not mdx:
        return text, head
    stash, esm = [], []
    parts, pos = [], 0
    for m in FENCE.finditer(text):
        parts.append((False, text[pos:m.start()]))
        parts.append((True, m.group(0)))
        pos = m.end()
    parts.append((False, text[pos:]))
    out = []
    for is_code, chunk in parts:
        if is_code:
            out.append(chunk)
            continue
        chunk, found = _extract_esm(chunk)
        esm += found
        spans = []
        chunk = INLINE_CODE.sub(lambda m: (spans.append(m.group(0)), "\x00%d\x00" % (len(spans) - 1))[1], chunk)
        chunk = _convert_prose(chunk, stash)
        chunk = re.sub("\x00(\\d+)\x00", lambda m: spans[int(m.group(1))], chunk)
        out.append(chunk)
    body = "".join(out)
    body = re.sub("\x01(\\d+)\x01", lambda m: stash[int(m.group(1))], body)
    if esm:
        n = len(esm)
        head += ('<details class="mdx-esm"><summary>%d import/export statement%s</summary><pre>%s</pre></details>'
                 % (n, "" if n == 1 else "s", html.escape("\n\n".join(esm))))
    return body, head
