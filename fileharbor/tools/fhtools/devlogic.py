"""Pure logic for the developer tools (no GUI), so it can be tested on its own."""
import base64
import binascii
import colorsys
import datetime as dt
import difflib
import hashlib
import html
import json
import random
import re
import secrets
import string
import time
import urllib.parse
import uuid
import zlib

try:
    import yaml
except ImportError:          # YAML tools are hidden when PyYAML is missing
    yaml = None


class ToolError(Exception):
    """A readable problem with the user's input (shown in place of the output)."""


# ---------------------------------------------------------------- JSON / YAML
def json_format(text, indent=2, sort_keys=False):
    try:
        data = json.loads(text)
    except json.JSONDecodeError as e:
        raise ToolError("Invalid JSON: %s (line %d, column %d)" % (e.msg, e.lineno, e.colno))
    return json.dumps(data, indent=int(indent) if indent else None, sort_keys=sort_keys, ensure_ascii=False)


def json_minify(text):
    try:
        return json.dumps(json.loads(text), separators=(",", ":"), ensure_ascii=False)
    except json.JSONDecodeError as e:
        raise ToolError("Invalid JSON: %s (line %d, column %d)" % (e.msg, e.lineno, e.colno))


def json_to_yaml(text):
    if yaml is None:
        raise ToolError("PyYAML is not installed")
    try:
        return yaml.safe_dump(json.loads(text), sort_keys=False, allow_unicode=True)
    except json.JSONDecodeError as e:
        raise ToolError("Invalid JSON: %s" % e.msg)


def yaml_to_json(text, indent=2):
    if yaml is None:
        raise ToolError("PyYAML is not installed")
    try:
        return json.dumps(yaml.safe_load(text), indent=indent, ensure_ascii=False, default=str)
    except yaml.YAMLError as e:
        raise ToolError("Invalid YAML: %s" % e)


# ------------------------------------------------------------- encode / decode
def base64_encode(text, urlsafe=False):
    raw = text.encode("utf-8")
    return (base64.urlsafe_b64encode if urlsafe else base64.b64encode)(raw).decode("ascii")


def base64_decode(text, urlsafe=False):
    s = "".join(text.split())
    s += "=" * (-len(s) % 4)
    try:
        raw = (base64.urlsafe_b64decode if urlsafe else base64.b64decode)(s)
    except (binascii.Error, ValueError) as e:
        raise ToolError("Not valid Base64: %s" % e)
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return "(binary data, %d bytes)\n%s" % (len(raw), raw.hex(" "))


def url_encode(text, full=False):
    return urllib.parse.quote(text, safe="/:?&=#" if full else "")


def url_decode(text):
    return urllib.parse.unquote(text)


def html_encode(text):
    return html.escape(text, quote=True)


def html_decode(text):
    return html.unescape(text)


def hex_encode(text):
    return text.encode("utf-8").hex(" ")


def hex_decode(text):
    try:
        return bytes.fromhex(text).decode("utf-8", "replace")
    except ValueError as e:
        raise ToolError("Not valid hex: %s" % e)


def gzip_size(text):
    raw = text.encode("utf-8")
    comp = zlib.compress(raw, 9)
    ratio = (100.0 * len(comp) / len(raw)) if raw else 0
    return "Original: %d bytes\nDeflate: %d bytes (%.1f%%)" % (len(raw), len(comp), ratio)


# --------------------------------------------------------------------- hashes
HASH_NAMES = ("md5", "sha1", "sha256", "sha512", "sha3_256", "blake2b")


def hashes(data, names=HASH_NAMES):
    if isinstance(data, str):
        data = data.encode("utf-8")
    out = []
    for n in names:
        h = hashlib.new(n)
        h.update(data)
        out.append("%-9s %s" % (n.upper(), h.hexdigest()))
    return "\n".join(out)


def hash_file(path, names=HASH_NAMES):
    hs = {n: hashlib.new(n) for n in names}
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            for h in hs.values():
                h.update(chunk)
    return "\n".join("%-9s %s" % (n.upper(), h.hexdigest()) for n, h in hs.items())


# ---------------------------------------------------------------- generators
def uuids(count=5, version="4", upper=False):
    out = []
    for _ in range(max(1, min(int(count), 500))):
        u = uuid.uuid1() if version == "1" else uuid.uuid4()
        out.append(str(u).upper() if upper else str(u))
    return "\n".join(out)


_ULID_ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"


def ulid():
    t = int(time.time() * 1000)
    r = int.from_bytes(secrets.token_bytes(10), "big")
    n = (t << 80) | r
    return "".join(_ULID_ALPHABET[(n >> (5 * i)) & 31] for i in range(25, -1, -1))


def ulids(count=5):
    return "\n".join(ulid() for _ in range(max(1, min(int(count), 500))))


def password(length=20, lower=True, upper=True, digits=True, symbols=True, count=5):
    pool = ""
    groups = []
    for on, chars in ((lower, string.ascii_lowercase), (upper, string.ascii_uppercase),
                      (digits, string.digits), (symbols, "!@#$%^&*()-_=+[]{};:,.?")):
        if on:
            pool += chars
            groups.append(chars)
    if not pool:
        raise ToolError("Choose at least one kind of character")
    length = max(len(groups), min(int(length), 256))
    out = []
    for _ in range(max(1, min(int(count), 50))):
        chars = [secrets.choice(g) for g in groups]
        chars += [secrets.choice(pool) for _ in range(length - len(chars))]
        random.SystemRandom().shuffle(chars)
        out.append("".join(chars))
    return "\n".join(out)


_LOREM = ("lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor incididunt ut "
          "labore et dolore magna aliqua enim ad minim veniam quis nostrud exercitation ullamco laboris "
          "nisi aliquip ex ea commodo consequat duis aute irure in reprehenderit voluptate velit esse "
          "cillum fugiat nulla pariatur excepteur sint occaecat cupidatat non proident sunt culpa qui "
          "officia deserunt mollit anim id est laborum").split()


def lorem(paragraphs=3, sentences=5):
    rnd = random.Random(42)
    paras = []
    for _ in range(max(1, min(int(paragraphs), 50))):
        sents = []
        for _ in range(max(1, min(int(sentences), 30))):
            words = [rnd.choice(_LOREM) for _ in range(rnd.randint(6, 14))]
            sents.append(" ".join(words).capitalize() + ".")
        paras.append(" ".join(sents))
    return "\n\n".join(paras)


# ------------------------------------------------------------- time and numbers
def timestamp(text):
    text = text.strip()
    lines = []
    if not text or text.lower() == "now":
        n = time.time()
        lines.append("Now: %d (seconds), %d (milliseconds)" % (n, n * 1000))
        text = str(int(n))
    if re.fullmatch(r"-?\d+(\.\d+)?", text):
        v = float(text)
        if abs(v) > 1e14:
            v /= 1e6        # microseconds
        elif abs(v) > 1e11:
            v /= 1e3        # milliseconds
        d = dt.datetime.fromtimestamp(v, dt.timezone.utc)
        lines.append("UTC:    " + d.strftime("%Y-%m-%d %H:%M:%S"))
        lines.append("Local:  " + d.astimezone().strftime("%Y-%m-%d %H:%M:%S %Z"))
        lines.append("ISO:    " + d.isoformat())
        return "\n".join(lines)
    try:
        d = dt.datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        raise ToolError("Enter a Unix timestamp, an ISO date (2024-05-01 12:30:00), or 'now'")
    if d.tzinfo is None:
        d = d.astimezone()
    return "Unix seconds:      %d\nUnix milliseconds: %d\nUTC: %s" % (
        d.timestamp(), d.timestamp() * 1000, d.astimezone(dt.timezone.utc).isoformat())


def number_base(text):
    text = text.strip().replace("_", "")
    if not text:
        return ""
    try:
        if text.lower().startswith("0x"):
            n = int(text, 16)
        elif text.lower().startswith("0b"):
            n = int(text, 2)
        elif text.lower().startswith("0o"):
            n = int(text, 8)
        else:
            n = int(text, 10)
    except ValueError:
        raise ToolError("Enter a number: 255, 0xFF, 0b1111, or 0o17")
    return "Decimal: %d\nHex:     %s\nBinary:  %s\nOctal:   %s" % (n, hex(n), bin(n), oct(n))


def color_convert(text):
    t = text.strip().lower()
    m = re.fullmatch(r"#?([0-9a-f]{3}|[0-9a-f]{6})", t)
    if m:
        h = m.group(1)
        if len(h) == 3:
            h = "".join(c * 2 for c in h)
        r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4))
    else:
        m = re.fullmatch(r"rgb\(\s*(\d+)[ ,]+(\d+)[ ,]+(\d+)\s*\)", t)
        if m:
            r, g, b = (min(255, int(x)) for x in m.groups())
        else:
            m = re.fullmatch(r"hsl\(\s*(\d+)[ ,]+(\d+)%?[ ,]+(\d+)%?\s*\)", t)
            if not m:
                raise ToolError("Enter #ff9933, rgb(255,153,51) or hsl(30,100%,60%)")
            hh, ss, ll = int(m.group(1)) / 360.0, int(m.group(2)) / 100.0, int(m.group(3)) / 100.0
            r, g, b = (round(x * 255) for x in colorsys.hls_to_rgb(hh, ll, ss))
    hh, ll, ss = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)
    hv, sv, vv = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    lum = 0.2126 * (r / 255.0) ** 2.2 + 0.7152 * (g / 255.0) ** 2.2 + 0.0722 * (b / 255.0) ** 2.2
    return ("HEX: #%02x%02x%02x\nRGB: rgb(%d, %d, %d)\nHSL: hsl(%d, %d%%, %d%%)\nHSV: hsv(%d, %d%%, %d%%)\n"
            "Contrast with white: %.2f:1\nContrast with black: %.2f:1") % (
        r, g, b, r, g, b, round(hh * 360), round(ss * 100), round(ll * 100),
        round(hv * 360), round(sv * 100), round(vv * 100), 1.05 / (lum + 0.05), (lum + 0.05) / 0.05)


# --------------------------------------------------------------------- text
def case_convert(text, style="snake_case"):
    words = re.findall(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|\d+", text.replace("_", " ").replace("-", " "))
    low = [w.lower() for w in words]
    if style == "snake_case":
        return "_".join(low)
    if style == "kebab-case":
        return "-".join(low)
    if style == "camelCase":
        return (low[0] + "".join(w.capitalize() for w in low[1:])) if low else ""
    if style == "PascalCase":
        return "".join(w.capitalize() for w in low)
    if style == "CONSTANT_CASE":
        return "_".join(low).upper()
    if style == "Title Case":
        return " ".join(w.capitalize() for w in low)
    if style == "UPPER":
        return text.upper()
    if style == "lower":
        return text.lower()
    raise ToolError("Unknown style")


def text_stats(text):
    words = re.findall(r"\S+", text)
    return "Characters: %d\nCharacters (no spaces): %d\nWords: %d\nLines: %d\nBytes (UTF-8): %d\nReading time: ~%.1f min" % (
        len(text), len("".join(text.split())), len(words), (text.count("\n") + 1) if text else 0,
        len(text.encode("utf-8")), len(words) / 200.0)


def sort_lines(text, order="a-z", unique=False):
    lines = text.splitlines()
    if unique:
        seen, kept = set(), []
        for l in lines:
            if l not in seen:
                seen.add(l)
                kept.append(l)
        lines = kept
    if order == "a-z":
        lines.sort(key=str.lower)
    elif order == "z-a":
        lines.sort(key=str.lower, reverse=True)
    elif order == "length":
        lines.sort(key=len)
    elif order == "reverse":
        lines.reverse()
    return "\n".join(lines)


def text_diff(a, b):
    out = list(difflib.unified_diff(a.splitlines(), b.splitlines(), "left", "right", lineterm=""))
    return "\n".join(out) if out else "(no differences)"


# ------------------------------------------------------------------- web/dev
def jwt_decode(text):
    parts = text.strip().split(".")
    if len(parts) < 2:
        raise ToolError("A JWT has three parts separated by dots")
    out = []
    for name, p in zip(("Header", "Payload"), parts[:2]):
        try:
            raw = base64.urlsafe_b64decode(p + "=" * (-len(p) % 4))
            data = json.loads(raw)
        except (binascii.Error, ValueError):
            raise ToolError("The %s is not valid Base64/JSON" % name.lower())
        out.append("%s\n%s" % (name, json.dumps(data, indent=2, ensure_ascii=False)))
        if name == "Payload":
            for k in ("exp", "iat", "nbf"):
                if isinstance(data, dict) and isinstance(data.get(k), (int, float)):
                    when = dt.datetime.fromtimestamp(data[k], dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
                    out.append("%s = %s%s" % (k, when, "  (EXPIRED)" if k == "exp" and data[k] < time.time() else ""))
    out.append("Signature: %s  (not verified)" % (parts[2] if len(parts) > 2 else "(none)"))
    return "\n\n".join(out)


def regex_test(pattern, text, ignore_case=False, multiline=True, dotall=False):
    if not pattern:
        return ""
    flags = (re.I if ignore_case else 0) | (re.M if multiline else 0) | (re.S if dotall else 0)
    try:
        rx = re.compile(pattern, flags)
    except re.error as e:
        raise ToolError("Invalid pattern: %s" % e)
    ms = list(rx.finditer(text))
    if not ms:
        return "No matches"
    lines = ["%d match%s" % (len(ms), "" if len(ms) == 1 else "es")]
    for i, m in enumerate(ms[:200], 1):
        lines.append("#%d  [%d:%d]  %r" % (i, m.start(), m.end(), m.group(0)))
        for gi, g in enumerate(m.groups(), 1):
            lines.append("      group %d: %r" % (gi, g))
        for k, v in m.groupdict().items():
            lines.append("      <%s>: %r" % (k, v))
    if len(ms) > 200:
        lines.append("... and %d more" % (len(ms) - 200))
    return "\n".join(lines)


def cron_explain(text):
    f = text.split()
    if len(f) != 5:
        raise ToolError("A cron expression has 5 fields: minute hour day-of-month month day-of-week")
    names = ("minute", "hour", "day of month", "month", "day of week")
    lines = []
    for n, v in zip(names, f):
        if v == "*":
            d = "every %s" % n
        elif v.startswith("*/"):
            d = "every %s %s" % (v[2:], n)
        elif "," in v:
            d = "%s %s" % (n, "or ".join(x + " " for x in v.split(",")).strip())
        elif "-" in v:
            a, b = v.split("-", 1)
            d = "%s from %s to %s" % (n, a, b)
        else:
            d = "%s %s" % (n, v)
        lines.append("%-13s %-8s %s" % (n, v, d))
    return "\n".join(lines)


def escape_string(text, mode="json"):
    if mode == "json":
        return json.dumps(text, ensure_ascii=False)[1:-1]
    if mode == "unjson":
        try:
            return json.loads('"%s"' % text)
        except json.JSONDecodeError as e:
            raise ToolError("Not a valid JSON string body: %s" % e.msg)
    if mode == "regex":
        return re.escape(text)
    if mode == "shell":
        import shlex
        return shlex.quote(text)
    raise ToolError("Unknown mode")
