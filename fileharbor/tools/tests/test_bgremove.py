# Run: scripts/test.sh   (or python3 fileharbor/tools/tests/test_bgremove.py)   prints PASS/FAIL per check
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from PIL import Image, ImageDraw
from fhtools import bgremove


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  " + str(detail)))


def sample(bg=(240, 240, 240)):
    im = Image.new("RGB", (200, 150), bg)
    d = ImageDraw.Draw(im)
    d.ellipse((50, 25, 150, 125), fill=(200, 30, 30))
    d.ellipse((90, 60, 110, 90), fill=bg)          # a hole of background colour inside the subject
    return im


im = sample()
check("background colour is guessed from the border", bgremove.guess_background(im) == (240, 240, 240), bgremove.guess_background(im))

out, colour = bgremove.remove_background(im, tolerance=20, soften=0, shrink=0)
check("result is RGBA at the original size", out.mode == "RGBA" and out.size == im.size)
check("corner becomes transparent", out.getpixel((2, 2))[3] == 0, out.getpixel((2, 2)))
check("subject stays opaque", out.getpixel((70, 75))[3] == 255, out.getpixel((70, 75)))
check("enclosed hole is kept when only edge-connected background goes", out.getpixel((100, 75))[3] == 255, out.getpixel((100, 75)))

out2, _ = bgremove.remove_background(im, tolerance=20, edges_only=False, soften=0, shrink=0)
check("enclosed hole is removed when edges-only is off", out2.getpixel((100, 75))[3] == 0, out2.getpixel((100, 75)))

noisy = sample((10, 10, 10))
out3, _ = bgremove.remove_background(noisy, tolerance=20, soften=0, shrink=0)
check("works on a dark background", out3.getpixel((2, 2))[3] == 0 and out3.getpixel((70, 75))[3] == 255)

out4, _ = bgremove.remove_background(im, tolerance=20, soften=0, shrink=0, colour=(200, 30, 30), seed=(70, 75))
check("a picked colour overrides the border guess", out4.getpixel((70, 75))[3] == 0 and out4.getpixel((2, 2))[3] == 255)

soft, _ = bgremove.remove_background(im, tolerance=20, soften=2, shrink=2)
check("shrink and soften leave the centre opaque", soft.getpixel((70, 75))[3] == 255)

rgba = im.convert("RGBA")
rgba.putpixel((70, 75), (200, 30, 30, 0))
out5, _ = bgremove.remove_background(rgba, tolerance=20, soften=0, shrink=0)
check("existing transparency is kept", out5.getpixel((70, 75))[3] == 0)

big = Image.new("RGB", (3000, 2000), (250, 250, 250))
ImageDraw.Draw(big).rectangle((800, 500, 2200, 1500), fill=(10, 80, 200))
out6, _ = bgremove.remove_background(big)
check("large pictures work", out6.size == big.size and out6.getpixel((5, 5))[3] == 0 and out6.getpixel((1500, 1000))[3] == 255)
