#!/usr/bin/env python3
"""Draws the bank images for voice.lv2.

The thumbnail is the one that matters. mod-ui's plugin strip constrains it
to 256x64, so a tall pedal scaled down becomes a sliver about 50px wide -
the "sardine can". A WIDE nameplate at 256x64 fills the slot properly.

The screenshots are NOT drawn here: they are photographs of the real web
UI, taken by make_screenshot.js through the same Chromium that renders it
in the browser. A drawn screenshot is a drawing of what the author hoped
the interface looked like.
"""
import os

from PIL import Image, ImageDraw, ImageFont

OUT = 'modgui'
BG_TOP, BG_BOT = (74, 86, 102), (43, 50, 61)
GREEN, AMBER = (70, 224, 138), (255, 207, 92)
TEXT, DIM = (244, 249, 255), (159, 182, 201)


def font(size, bold=False):
    for p in ('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf' if bold else
              '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def gradient(w, h):
    img = Image.new('RGB', (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)],
               fill=tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3)))
    return img


def switch(d, x, y, on):
    """The same switch the web UI draws, at thumbnail size: the ON state
    has to read from across the plugin strip."""
    d.rounded_rectangle([x, y, x + 21, y + 10], radius=5,
                        fill=(53, 196, 124) if on else (35, 42, 51))
    cx = x + 15 if on else x + 6
    d.ellipse([cx - 4, y + 1, cx + 4, y + 9], fill=(255, 255, 255) if on else (140, 154, 168))


def thumbnail(path, stereo=False):
    W, H = 256, 64
    img = gradient(W, H)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=8, outline=(23, 28, 35))

    d.text((13, 7), 'REMY', font=font(9), fill=DIM)
    d.text((13, 17), 'VOICE', font=font(22, True), fill=TEXT)
    if stereo:
        d.text((14, 44), 'STEREO', font=font(10, True), fill=AMBER)
    else:
        d.text((14, 45), 'NO PITCH TRACKING', font=font(8), fill=DIM)

    for i, on in enumerate((True, True, False, True, True, False)):
        switch(d, 118 + (i % 3) * 46, 14 + (i // 3) * 22, on)
    d.ellipse([232, 12, 246, 26], fill=(58, 68, 82), outline=(23, 28, 35))
    d.line([239, 14, 239, 19], fill=AMBER, width=2)

    img.save(path)
    return img.size


def alleger(path, largeur=560, couleurs=128):
    """Shrinks a photograph of the web UI to something a bundle should
    carry. Chromium writes 24-bit PNGs of 300 kB; the plugin browser shows
    the image at half that size anyway, and a bundle that arrives over a
    Dwarf's network share should not be most of a megabyte of screenshot.
    Idempotent: once shrunk, the image is left alone."""
    img = Image.open(path)
    if img.width <= largeur and img.mode == 'P':
        return img.size, os.path.getsize(path)
    h = int(img.height * largeur / img.width)
    img = img.convert('RGB').resize((largeur, h), Image.LANCZOS)
    img = img.quantize(colors=couleurs, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    img.save(path, optimize=True)
    return img.size, os.path.getsize(path)


if __name__ == '__main__':
    print('thumbnail mono  :', thumbnail(f'{OUT}/thumbnail-voice.png'))
    print('thumbnail stereo:', thumbnail(f'{OUT}/thumbnail-voice-stereo.png', stereo=True))
    for name in ('screenshot-voice.png', 'screenshot-voice-stereo.png'):
        path = os.path.join(OUT, name)
        if not os.path.exists(path):
            raise SystemExit('%s is missing - run: node make_screenshot.js' % name)
        size, octets = alleger(path)
        print('%-16s: %dx%d, %d bytes' % (name.split('.')[0], size[0], size[1], octets))
