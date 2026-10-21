#!/usr/bin/env python3
"""Draws Blur5's pictures from scratch: our own art, nothing taken from the game.

  art/make-art.py [OUT_DIR]   (default: art/)

Writes the files the app build looks for (art name "blur5"):
  blur5.png        512x512 icon: the tile on the PS5 Home screen
  blur5-bg.png     3840x2160 background behind the tile (the build needs this exact size)
  blur5-page.png   1024x576 background for Blur5's own page (the app reads pictures of
                   up to 1024x1024 and 2 MB)

The pictures are neon light streaks racing down a road at night, and the icon carries the
name BLUR5 in Inter SemiBold (SIL Open Font License, launcher/third_party/fonts). Every random
choice comes from a fixed seed, so the same script always gives the same files.
"""
import math, pathlib, random, sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = pathlib.Path(__file__).resolve().parent
FONT = HERE.parent / "launcher/third_party/fonts/Inter-SemiBold.ttf"

NIGHT_TOP, NIGHT_BOTTOM = (16, 12, 46), (3, 3, 12)
VIOLET, MAGENTA, CYAN, AMBER, WHITE = (200, 107, 255), (255, 64, 170), (64, 200, 255), (255, 186, 90), (255, 244, 255)
LIGHTS = [VIOLET, MAGENTA, CYAN, AMBER, WHITE]


def gradient(w, h, top, bottom):
    """top fading to bottom, from one 1xH ramp."""
    ramp = Image.linear_gradient("L").resize((1, h), Image.BILINEAR)
    bands = [ramp.point(lambda v, a=a, b=b: round(a + (b - a) * v / 255)) for a, b in zip(top, bottom)]
    return Image.merge("RGB", bands).resize((w, h), Image.NEAREST)


def glow_layer(size, draw, blur):
    """A layer drawn by draw(ImageDraw), softened by blur, plus its sharp core."""
    soft = Image.new("RGBA", size, (0, 0, 0, 0))
    draw(ImageDraw.Draw(soft), True)
    sharp = Image.new("RGBA", size, (0, 0, 0, 0))
    draw(ImageDraw.Draw(sharp), False)
    return soft.filter(ImageFilter.GaussianBlur(blur)), sharp


def add(base, layer):
    """Light adds up: the layer's colour, scaled by its alpha, added to base (RGB)."""
    r, g, b, a = layer.split()
    lit = Image.merge("RGB", [ImageChops.multiply(c, a) for c in (r, g, b)])
    return ImageChops.add(base, lit)


def scene(w, h, seed=5):
    """The road at night: city lights far off, a glow on the horizon, streaks racing in."""
    rng = random.Random(seed)
    k = w / 1920
    img = gradient(w, h, NIGHT_TOP, NIGHT_BOTTOM)
    vx, vy = w * 0.64, h * 0.50  # where the road meets the horizon

    # The horizon's glow.
    halo = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(halo)
    for radius, colour, alpha in ((620, VIOLET, 70), (380, MAGENTA, 70), (170, WHITE, 60)):
        r = radius * k
        d.ellipse([vx - r * 1.6, vy - r * 0.55, vx + r * 1.6, vy + r * 0.55], fill=colour + (alpha,))
    img = add(img, halo.filter(ImageFilter.GaussianBlur(90 * k)))

    # City lights far off: soft dots above the horizon.
    dots = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(dots)
    for _ in range(260):
        x, y = rng.uniform(0, w), rng.uniform(h * 0.05, vy + 30 * k)
        r = rng.uniform(3, 16) * k * (0.5 + y / vy * 0.6)
        colour = rng.choice(LIGHTS)
        d.ellipse([x - r, y - r, x + r, y + r], fill=colour + (rng.randrange(40, 140),))
    img = add(img, dots.filter(ImageFilter.GaussianBlur(7 * k)))

    # The road: lane lines running from the bottom edge to the horizon.
    def road(draw, soft):
        for i, x in enumerate((-0.45, -0.05, 0.32, 0.62, 0.92, 1.35)):
            colour = (CYAN, VIOLET, WHITE, VIOLET, MAGENTA, CYAN)[i]
            width = (26 if soft else 5) * k
            draw.line([(x * w, h), (vx, vy)], fill=colour + (150 if soft else 210,), width=max(1, int(width)))
    soft, sharp = glow_layer((w, h), road, 22 * k)
    img = add(add(img, soft), sharp)

    # Light streaks racing toward the horizon: each a line from far off to near, fading in.
    streaks = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    cores = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ds, dc = ImageDraw.Draw(streaks), ImageDraw.Draw(cores)
    for n in range(90):
        # Most from the left, some from the right, a few low along the road.
        side = n % 10
        low, high = (150, 212) if side < 5 else (-32, 30) if side < 8 else (196, 250)
        angle = math.radians(rng.uniform(low, high))
        far = rng.uniform(0.08, 0.45)
        near = far + rng.uniform(0.25, 0.75)
        reach = w * 1.1
        x0, y0 = vx + math.cos(angle) * reach * far, vy - math.sin(angle) * reach * far * 0.55
        x1, y1 = vx + math.cos(angle) * reach * near, vy - math.sin(angle) * reach * near * 0.55
        colour = rng.choice(LIGHTS)
        steps = 12
        for s in range(steps):
            t0, t1 = s / steps, (s + 1) / steps
            a = int(30 + 200 * t1)
            seg = [(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0), (x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1)]
            width = (2 + 10 * t1) * k
            ds.line(seg, fill=colour + (a // 2,), width=max(1, int(width * 3)))
            dc.line(seg, fill=colour + (a,), width=max(1, int(width * 0.5)))
    img = add(add(img, streaks.filter(ImageFilter.GaussianBlur(14 * k))), cores)

    # Darker corners, so words and tiles read over it.
    shade = Image.new("L", (w, h), 0)
    ImageDraw.Draw(shade).ellipse([-w * 0.25, -h * 0.35, w * 1.25, h * 1.35], fill=255)
    shade = shade.filter(ImageFilter.GaussianBlur(220 * k)).point(lambda v: 90 + v * 165 // 255)
    img = ImageChops.multiply(img, Image.merge("RGB", [shade] * 3))
    return img


def icon():
    """512x512: the night road behind BLUR5, its letters lit from violet to cyan."""
    size = 512
    img = scene(1024, 1024, seed=11).crop((256, 128, 768, 640))
    img = ImageChops.multiply(img, Image.new("RGB", (size, size), (150, 150, 165)))
    font = ImageFont.truetype(str(FONT), 128)
    text = "BLUR5"
    box = ImageDraw.Draw(Image.new("L", (1, 1))).textbbox((0, 0), text, font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).text(((size - tw) / 2 - box[0] + 10, (size - th) / 2 - box[1]), text,
                              font=font, fill=255)
    # Leaning forward, as if moving.
    mask = mask.transform((size, size), Image.AFFINE, (1, 0.22, -0.22 * size / 2, 0, 1, 0), Image.BICUBIC)
    fill = Image.linear_gradient("L").rotate(90).resize((size, size))
    fill = Image.merge("RGB", [fill.point(lambda v, a=a, b=b: round(a + (b - a) * v / 255))
                               for a, b in zip(CYAN, MAGENTA)])
    shadow = mask.filter(ImageFilter.GaussianBlur(14))
    img = Image.composite(Image.new("RGB", (size, size), (0, 0, 0)), img, shadow.point(lambda v: v * 3 // 4))
    img = Image.composite(fill, img, mask)
    # A white edge along the letters' tops, and a speed line under them.
    edge = ImageChops.subtract(mask, ImageChops.offset(mask, 0, 4))
    img = Image.composite(Image.new("RGB", (size, size), WHITE), img, edge.point(lambda v: v * 3 // 5))
    line = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(line).line([(96, 330), (436, 330)], fill=WHITE + (230,), width=6)
    img = add(img, line.filter(ImageFilter.GaussianBlur(3)))
    img = add(img, line)
    return img


def main(out):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    icon().save(out / "blur5.png", optimize=False)
    big = scene(3840, 2160)
    big.save(out / "blur5-bg.png", optimize=False)
    big.resize((1024, 576), Image.LANCZOS).save(out / "blur5-page.png", optimize=True)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else HERE)
