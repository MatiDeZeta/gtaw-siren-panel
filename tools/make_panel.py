#!/usr/bin/env python3
"""Builds the Siren Master panel's layers from the photo in art/.

The photo shows the panel straight on with every key backlit white and all three stage LEDs on.
Every layer the HUD stacks is taken out of it:

  base        the panel as it sits unpowered: keys dark, LEDs off, the knob taken out of its slot
  backlight   the legends backlit blue and the key rings softly lit, for while the engine runs
  <key>       one per key: its ring glowing blue and its legend lit
  led1..3     the stage LEDs, as photographed
  knob0..3    the slide knob at OFF, 1, 2 and 3

Each layer is a full-size picture of the whole panel with only its own part drawn in, so they
stack. The light in the photo is separated from the faceplate by how far each pixel is above the
faceplate's own black, which is then dimmed, recoloured or kept depending on the layer.

Needs Pillow and numpy, which nothing else in the repository does. Only run it to regenerate
assets/kabel/ after changing the photo or the look:

    python3 tools/make_panel.py [art/siren-master.jpg] [assets/kabel]
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---- where things are in the photo, in its own pixels ------------------------------------------
CROP = (71, 306, 1927, 1203)          # the housing, with a few pixels of white round it
OUT_W = 1200                          # the width the layers are written at

KEY = 159                             # keys are this square
COLS = [180, 389, 599, 808, 1017, 1227, 1436, 1645]
ROWS = [439, 672, 903]
KEYS = {
    "lrco": (0, 0), "warn": (0, 1), "dim": (0, 6), "flood": (0, 7),
    "stby": (1, 0), "radio": (1, 1), "sub": (1, 2), "sbs": (1, 3),
    "wail": (1, 4), "yelp": (1, 5), "wig": (1, 6), "bumr": (1, 7),
    "leftalley": (2, 3), "takedown": (2, 4), "rightalley": (2, 5),
    "crimeject": (2, 6), "gunlock": (2, 7),
}
HALO = 24                             # how far a key's own light may reach past its ring
REGION = 60                           # the margin each key is worked in, room for its bloom
CAP_INSET = 16                        # the black cap the legend is printed on

LEDS = [(283, 905), (467, 905), (652, 905)]
LED_R = 42

# The slot, end to end, and the knob as it sits at the right-hand end of it (stage 3).
SLOT_X = (187, 742)
SLOT_Y = (948, 1095)
KNOB = (553, 950, 722, 1084)
KNOB_RADIUS = 24
# The knob's left edge sits as far in from the slot's left end at OFF as it does from the right
# end at 3; the stops between are spaced evenly.
KNOB_OFF_DX = (SLOT_X[0] + (SLOT_X[1] - KNOB[2])) - KNOB[0]
KNOB_STOPS = [KNOB_OFF_DX * (1 - i / 3) for i in range(4)]

# ---- the look -----------------------------------------------------------------------------------
UNLIT_RING = 0.16                     # how much of its light an unpowered ring keeps
UNLIT_LEGEND = 0.30                   # and a legend, which is printed white
LEGEND_BLUE = np.array([96, 162, 255], float)
LEGEND_HOT = np.array([170, 210, 255], float)
RING_SOFT = np.array([196, 210, 230], float)
RING_SOFT_ALPHA = 0.32
RING_BLUE = np.array([30, 128, 255], float)
RING_HOT = np.array([185, 222, 255], float)
LEGEND_ON = np.array([226, 240, 255], float)
LED_DARK = np.array([0.02, 0.06, 0.03])
# JPEG noise on the black faceplate, which is not light and would otherwise tint every layer.
NOISE = 7


def lum(a):
    return a.mean(axis=2)


def face_level(L, box, margin):
    """The faceplate's black just outside a box: the median of the dark pixels in a band."""
    x0, y0, x1, y1 = box
    band = np.concatenate([
        L[y0 - margin - 12:y0 - margin, x0:x1].ravel(),
        L[y1 + margin:y1 + margin + 12, x0:x1].ravel(),
        L[y0:y1, x0 - margin - 12:x0 - margin].ravel(),
        L[y0:y1, x1 + margin:x1 + margin + 12].ravel(),
    ])
    band = band[band < 80]
    return float(np.median(band)) if band.size else 42.0


def blur(mask, radius):
    img = Image.fromarray(np.clip(mask * 255, 0, 255).astype(np.uint8), "L")
    return np.asarray(img.filter(ImageFilter.GaussianBlur(radius))).astype(float) / 255


def rounded(shape, box, radius, feather):
    img = Image.new("L", (shape[1], shape[0]), 0)
    ImageDraw.Draw(img).rounded_rectangle(box, radius=radius, fill=255)
    if feather:
        img = img.filter(ImageFilter.GaussianBlur(feather))
    return np.asarray(img).astype(float) / 255


def key_masks(light):
    """The ring's and the legend's own pixels in one key's region, as two boolean masks.

    Only light that belongs to the key is kept. The region is a square, but the faceplate inside
    it carries JPEG speckle that clears the noise floor here and there; taking every lit pixel
    in the square dimmed and lit that speckle too, and the square's edge showed as a faint box
    round the key and a seam between neighbours. So the ring is what is lit and joined to it --
    its inner corners, its glow -- within a rounded reach of the key, and the legend is what is
    lit but stands apart from the ring inside the cap.
    """
    m = REGION
    reach = rounded(light.shape, (m - HALO, m - HALO, m + KEY + HALO - 1, m + KEY + HALO - 1),
                    HALO + 22, 0) > 0.5
    lit = Image.fromarray((((light > 0.06) & reach) * 255).astype(np.uint8), "L").copy()
    mid = m + KEY // 2
    for seed in [(m + 4, mid), (m + KEY - 5, mid), (mid, m + 4), (mid, m + KEY - 5)]:
        if lit.getpixel(seed) == 255:
            ImageDraw.floodfill(lit, seed, 128)
    a = np.asarray(lit)
    ring = a == 128
    inside = np.zeros_like(ring)
    inside[m + CAP_INSET:m + KEY - CAP_INSET, m + CAP_INSET:m + KEY - CAP_INSET] = True
    cap = (a == 255) & inside
    # The glyphs' antialiased edges fall under the threshold; take them in too.
    grown = Image.fromarray((cap * 255).astype(np.uint8), "L").filter(ImageFilter.MaxFilter(5))
    cap = (np.asarray(grown) > 0) & inside & ~ring
    return ring, cap


def own(mask, grow, feather):
    """A soft weight over a mask: 1 on it, falling to 0 a few pixels past it."""
    img = Image.fromarray((mask * 255).astype(np.uint8), "L")
    if grow:
        img = img.filter(ImageFilter.MaxFilter(grow * 2 + 1))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(feather))).astype(float) / 255


def housing_alpha(src):
    """Opaque inside the housing, clear on the white round it, antialiased at the edge."""
    L = lum(src)
    h, w = L.shape
    # A copy: an image straight out of fromarray shares the array's memory, and floodfill
    # silently leaves it untouched.
    outside = Image.fromarray(((L > 150) * 255).astype(np.uint8), "L").copy()
    for seed in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        ImageDraw.floodfill(outside, seed, 128)
    bg = np.asarray(outside) == 128
    edge = np.asarray(Image.fromarray((bg * 255).astype(np.uint8), "L")
                      .filter(ImageFilter.MaxFilter(7))) > 0
    alpha = np.ones_like(L)
    soft = np.clip((250 - L) / (250 - 55), 0, 1)
    alpha[edge] = soft[edge]
    alpha[bg & (L > 248)] = 0
    return alpha


def unmix_from_white(rgb, alpha):
    """The colour a pixel had before it was blended onto the white background."""
    a = np.maximum(alpha, 1e-3)[..., None]
    return np.clip((rgb - (1 - a) * 255) / a, 0, 255)


def main():
    src_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "art", "siren-master.jpg")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "assets", "kabel")
    os.makedirs(out_dir, exist_ok=True)

    photo = np.asarray(Image.open(src_path).convert("RGB")).astype(float)
    L = lum(photo)
    H, W = L.shape
    layers = {}

    def empty():
        return np.zeros((H, W, 4), float)

    base = photo.copy()

    # ---- keys ----------------------------------------------------------------------------------
    backlight = empty()
    for name, (row, col) in KEYS.items():
        x0, y0 = COLS[col], ROWS[row]
        box = (x0, y0, x0 + KEY, y0 + KEY)
        F = face_level(L, box, HALO)
        rx0, ry0, rx1, ry1 = x0 - REGION, y0 - REGION, x0 + KEY + REGION, y0 + KEY + REGION
        sl = (slice(ry0, ry1), slice(rx0, rx1))

        light = np.clip((L[sl] - F - NOISE) / (255 - F - NOISE), 0, 1)
        ring, cap = key_masks(light)
        # Everything this key does is weighted by how much a pixel is the key's own, so nothing
        # it draws or dims has an edge of its own.
        w = own(ring | cap, 1, 1.2)
        light = light * w

        # Unpowered: what light there is, dimmed to the plastic's own grey, over the whole key
        # including the cap's face. Taken off what is there already, since the regions overlap
        # and a neighbour's ring may have been dimmed into this one.
        face = rounded(light.shape, (REGION, REGION, REGION + KEY - 1, REGION + KEY - 1), 26, 0)
        wk = np.maximum(w, own(face > 0.5, 0, 1.2))
        region = photo[sl]
        k = np.where(cap, UNLIT_LEGEND, UNLIT_RING)[..., None]
        lifted = np.clip(region - F, 0, None)
        base[sl] = base[sl] - lifted * (1 - k) * wk[..., None]

        # Backlit: legends blue, hottest where the print is brightest, rings a soft white.
        legend_a = np.where(cap, light, 0)
        hot = legend_a[..., None] ** 3
        col_leg = LEGEND_BLUE * (1 - hot) + LEGEND_HOT * hot
        ring_a = np.where(cap, 0, light * RING_SOFT_ALPHA)
        a = np.maximum(legend_a, ring_a)
        rgb = np.where(cap[..., None], col_leg, RING_SOFT)
        bl = backlight[sl]
        bl[..., :3] = np.where(a[..., None] > bl[..., 3:4], rgb, bl[..., :3])
        bl[..., 3] = np.maximum(bl[..., 3], a)

        # On: the ring glows blue with a bloom past its edge, and the legend goes white. The
        # region leaves room for the bloom to fade out before its edge.
        lay = empty()
        ring_light = np.where(cap, 0, light)
        bloom = blur(ring_light, 9) * 0.55
        ring_a = np.clip(np.maximum(ring_light * 1.15, bloom), 0, 1)
        hot = (ring_light ** 2.5)[..., None]
        ring_rgb = RING_BLUE * (1 - hot) + RING_HOT * hot
        a = np.where(cap, light, ring_a)
        rgb = np.where(cap[..., None], LEGEND_ON, ring_rgb)
        lay[sl][..., :3] = rgb
        lay[sl][..., 3] = a
        layers[name] = lay

    # ---- the stage LEDs ------------------------------------------------------------------------
    green = photo[..., 1] - (photo[..., 0] + photo[..., 2]) / 2
    for i, (cx, cy) in enumerate(LEDS):
        box = (cx - LED_R, cy - LED_R, cx + LED_R, cy + LED_R)
        sl = (slice(box[1], box[3]), slice(box[0], box[2]))
        F = face_level(L, box, 4)
        glow = np.clip(green[sl] / 120, 0, 1)
        bright = np.clip((L[sl] - F) / (255 - F), 0, 1)
        a = np.clip(np.maximum(glow, bright) * 1.3, 0, 1)
        # Off is a dark green lens.
        region = photo[sl]
        face = np.full_like(region, F)
        dark = face + np.clip(region - face, 0, None) * LED_DARK
        base[sl] = region * (1 - a[..., None]) + dark * a[..., None]
        lay = empty()
        lay[sl][..., :3] = region
        lay[sl][..., 3] = a
        layers["led%d" % (i + 1)] = lay

    # ---- the slot and the knob -----------------------------------------------------------------
    # The knob is cut out first, from the untouched photo, with the green of the LED above it
    # taken out of its highlight so it does not glow at a stop whose LED is off.
    kx0, ky0, kx1, ky1 = KNOB
    knob_mask = rounded(L.shape, (kx0, ky0, kx1, ky1), KNOB_RADIUS, 1.2)
    knob_rgb = photo.copy()
    g = knob_rgb[..., 1:2]
    grey = knob_rgb.mean(axis=2, keepdims=True)
    knob_rgb = np.where(g > grey + 4, np.repeat(grey, 3, axis=2), knob_rgb)

    # Under the knob, the slot's right-hand end is the mirror of its left-hand one. The knob
    # shades the faceplate round it, so the mirrored face is brought down to the level of what
    # surrounds it there, and blended in through a soft edge.
    mx0, mx1 = 530, SLOT_X[1] + 14
    y0, y1 = SLOT_Y
    mirror = SLOT_X[0] + SLOT_X[1]
    xs = np.arange(mx0, mx1)
    patch = photo[y0:y1, mirror - xs]
    around = np.concatenate([L[y0 - 10:y0, mx0:mx1].ravel(), L[y1:y1 + 10, mx0:mx1].ravel()])
    mirrored = np.concatenate([L[y0 - 10:y0, mirror - xs].ravel(), L[y1:y1 + 10, mirror - xs].ravel()])
    patch = patch * (np.median(around) / np.median(mirrored))
    fill = rounded(L.shape, (mx0 + 10, y0 + 6, mx1 - 10, y1 - 6), 20, 7)[y0:y1, mx0:mx1, None]
    base[y0:y1, mx0:mx1] = base[y0:y1, mx0:mx1] * (1 - fill) + patch * fill

    for i, dx in enumerate(KNOB_STOPS):
        dx = int(round(dx))
        lay = empty()
        ys, xs_ = slice(ky0 - 4, ky1 + 4), slice(kx0 - 4, kx1 + 4)
        tx = slice(kx0 - 4 + dx, kx1 + 4 + dx)
        lay[ys, tx, :3] = knob_rgb[ys, xs_]
        lay[ys, tx, 3] = knob_mask[ys, xs_]
        layers["knob%d" % i] = lay

    layers["backlight"] = backlight

    # ---- the housing's outline -----------------------------------------------------------------
    alpha = housing_alpha(photo)
    base_rgb = unmix_from_white(base, alpha)
    layers["base"] = np.dstack([base_rgb, alpha])

    # ---- crop, scale and write -----------------------------------------------------------------
    x0, y0, x1, y1 = CROP
    out_h = round((y1 - y0) * OUT_W / (x1 - x0))
    for name, lay in layers.items():
        lay = lay[y0:y1, x0:x1].copy()
        lay[..., 3] *= alpha[y0:y1, x0:x1]
        rgba = np.clip(np.rint(np.dstack([lay[..., :3], lay[..., 3:] * 255])), 0, 255).astype(np.uint8)
        img = Image.fromarray(rgba, "RGBA").convert("RGBa")
        img = img.resize((OUT_W, out_h), Image.LANCZOS).convert("RGBA")
        img.save(os.path.join(out_dir, name + ".png"), optimize=True)
        print(f"  wrote {name}.png")
    print(f"{len(layers)} layers at {OUT_W}x{out_h}")


if __name__ == "__main__":
    main()
