"""Image loading for the viewer: Pillow for common formats, libraw (dcraw_emu) for camera RAW."""
import io
import os
import shutil
import subprocess

from PIL import Image, ImageOps

RAW_EXTS = {".cr2", ".cr3", ".crw", ".nef", ".nrw", ".arw", ".srf", ".sr2", ".dng", ".orf", ".rw2", ".raf",
            ".pef", ".srw", ".x3f", ".3fr", ".erf", ".kdc", ".mrw", ".mef", ".mos", ".raw", ".rwl", ".iiq", ".gpr"}
PIL_EXTS = {".jpg", ".jpeg", ".jpe", ".png", ".gif", ".bmp", ".webp", ".tif", ".tiff", ".ico", ".ppm", ".pgm",
            ".pbm", ".pnm", ".tga", ".dds", ".sgi", ".pcx", ".psd", ".avif", ".heic", ".heif", ".jp2", ".j2k",
            ".qoi", ".icns", ".xbm", ".xpm", ".svg"}
SAVE_FORMATS = {"JPEG": ".jpg", "PNG": ".png", "WebP": ".webp", "TIFF": ".tif", "BMP": ".bmp", "GIF": ".gif", "AVIF": ".avif"}


def is_image(path):
    e = os.path.splitext(path)[1].lower()
    return e in RAW_EXTS or e in PIL_EXTS


def is_raw(path):
    return os.path.splitext(path)[1].lower() in RAW_EXTS


def _dcraw(path, extra):
    exe = shutil.which("dcraw_emu")
    if not exe:
        raise OSError("RAW support needs libraw (the dcraw_emu program)")
    # -T TIFF, -w camera white balance, -Z - writes the picture to standard output
    r = subprocess.run([exe, "-T", "-w", *extra, "-Z", "-", path], check=True, capture_output=True, timeout=300)
    with Image.open(io.BytesIO(r.stdout)) as im:
        return im.copy()


def _raw_preview(path):
    """Half-size development: several times faster than a full one, good enough to browse."""
    return _dcraw(path, ["-h"])


def _raw_develop(path):
    return _dcraw(path, ["-q", "3"])


def load(path, develop_raw=True):
    """Returns (PIL image in RGB/RGBA, info dict). Raises OSError with a readable message."""
    ext = os.path.splitext(path)[1].lower()
    info = {"raw": False}
    try:
        if ext in RAW_EXTS:
            info["raw"] = True
            im = _raw_develop(path) if develop_raw else (_raw_preview(path) or _raw_develop(path))
            if im is None:
                raise OSError("Could not read this RAW file")
        elif ext == ".svg":
            im = _svg(path)
        else:
            im = Image.open(path)
            info["format"] = im.format
            info["frames"] = getattr(im, "n_frames", 1)
            try:
                info["exif"] = dict(im.getexif())
            except Exception:
                info["exif"] = {}
            im = ImageOps.exif_transpose(im)
            im.load()
    except subprocess.CalledProcessError as e:
        raise OSError("RAW conversion failed: %s" % (e.stderr or b"").decode(errors="replace")[:200])
    except Image.DecompressionBombError:
        raise OSError("This image is too large to open safely")
    if im.mode not in ("RGB", "RGBA"):
        im = im.convert("RGBA" if "A" in im.getbands() or im.mode in ("P", "LA") and "transparency" in im.info else "RGB")
    return im, info


def _svg(path):
    import gi
    gi.require_version("GdkPixbuf", "2.0")
    from gi.repository import GdkPixbuf
    pb = GdkPixbuf.Pixbuf.new_from_file_at_size(path, 2048, 2048)
    mode = "RGBA" if pb.get_has_alpha() else "RGB"
    return Image.frombytes(mode, (pb.get_width(), pb.get_height()), pb.get_pixels(), "raw", mode, pb.get_rowstride())


def save(im, path, fmt=None, quality=90):
    fmt = fmt or {v: k for k, v in SAVE_FORMATS.items()}.get(os.path.splitext(path)[1].lower(), "PNG")
    kw = {}
    if fmt in ("JPEG", "WebP", "AVIF"):
        kw["quality"] = int(quality)
    if fmt == "JPEG":
        if im.mode == "RGBA":
            bg = Image.new("RGB", im.size, (255, 255, 255))
            bg.paste(im, mask=im.split()[3])
            im = bg
        kw["optimize"] = True
    if fmt == "PNG":
        kw["optimize"] = True
    tmp = path + ".fh-tmp"
    im.save(tmp, format=fmt, **kw)
    os.replace(tmp, path)
