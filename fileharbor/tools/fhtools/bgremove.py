"""Background removal for the image editor. Pure Pillow, no extra libraries.

It works on plain-coloured or gently shaded backgrounds (product photos, scans, logos, screenshots,
studio portraits): the background colour is taken from the picture's border, every pixel close enough to it
becomes transparent, and the edge is shrunk and softened. It is not a subject-detection model, so busy
photographic backgrounds need a different tool.
"""
from collections import Counter

from PIL import Image, ImageChops, ImageDraw, ImageFilter

WORK_SIZE = 900          # the mask is computed on a copy no larger than this, then scaled up


def _border_points(w, h, step):
    for x in range(0, w, step):
        yield x, 0
        yield x, h - 1
    for y in range(0, h, step):
        yield 0, y
        yield w - 1, y


def guess_background(im):
    """Most common colour along the border, as an (r, g, b) tuple."""
    rgb = im.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    step = max(1, max(w, h) // 200)
    votes = Counter()
    for x, y in _border_points(w, h, step):
        r, g, b = px[x, y]
        votes[(r // 16, g // 16, b // 16)] += 1
    (qr, qg, qb), _ = votes.most_common(1)[0]
    # average the real colours that fell in the winning bucket
    sel = [px[x, y] for x, y in _border_points(w, h, step) if (px[x, y][0] // 16, px[x, y][1] // 16, px[x, y][2] // 16) == (qr, qg, qb)]
    n = len(sel)
    return tuple(sum(c[i] for c in sel) // n for i in range(3))


def _similar(rgb, colour, threshold):
    """L image: 255 where the pixel is within `threshold` (sum of channel differences, 0-765) of colour."""
    diff = ImageChops.difference(rgb, Image.new("RGB", rgb.size, colour))
    r, g, b = diff.split()
    total = ImageChops.add(ImageChops.add(r, g), b)         # clips at 255, fine: thresholds stay below it
    return total.point(lambda v: 255 if v <= threshold else 0)


def background_mask(im, tolerance=30, edges_only=True, colour=None, seed=None):
    """Returns (mask, colour). mask is an L image at the picture's size, 255 = background.
    seed is a picked (x, y) point: the region around it counts as background even away from the edges."""
    w, h = im.size
    scale = min(1.0, WORK_SIZE / max(w, h))
    small = im.convert("RGB")
    if scale < 1.0:
        small = small.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.BILINEAR)
    small = small.filter(ImageFilter.GaussianBlur(1))        # keeps film grain and JPEG noise out of the test
    colour = colour or guess_background(small)
    threshold = int(max(0, min(100, tolerance)) * 2.5)        # slider 0-100 -> 0-250
    similar = _similar(small, colour, threshold)
    if edges_only:
        # only background that is connected to the picture's edge: flood fill from every border pixel that qualifies
        sw, sh = similar.size
        px = similar.load()
        seeds = list(_border_points(sw, sh, 3))
        if seed:
            seeds.append((min(sw - 1, int(seed[0] * scale)), min(sh - 1, int(seed[1] * scale))))
        for x, y in seeds:
            if px[x, y] == 255:
                ImageDraw.floodfill(similar, (x, y), 128)
        similar = similar.point(lambda v: 255 if v == 128 else 0)
    if similar.size != (w, h):
        similar = similar.resize((w, h), Image.BILINEAR)
    return similar, colour


def apply_mask(im, mask, soften=1.0, shrink=1):
    """Makes the masked area transparent. shrink eats into the subject's edge (px), soften feathers it."""
    fg = ImageChops.invert(mask)                              # 255 = keep
    if shrink > 0:
        size = int(shrink) * 2 + 1
        fg = fg.filter(ImageFilter.MinFilter(size))
    if soften > 0:
        fg = fg.filter(ImageFilter.GaussianBlur(soften))
    rgba = im.convert("RGBA")
    alpha = ImageChops.multiply(rgba.getchannel("A"), fg)
    rgba.putalpha(alpha)
    return rgba


def remove_background(im, tolerance=30, edges_only=True, soften=1.0, shrink=1, colour=None, seed=None):
    mask, colour = background_mask(im, tolerance, edges_only, colour, seed)
    return apply_mask(im, mask, soften, shrink), colour
