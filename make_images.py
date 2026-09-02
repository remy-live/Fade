#!/usr/bin/env python3
"""Draws the plugin bank images.

The thumbnail is the one that matters. mod-ui's plugin strip constrains it
to max-width 256px and max-height 64px, so a tall pedal shape scaled down
becomes a sliver about 52px wide - that is the "sardine can". A WIDE
nameplate at roughly 256x64 fills the slot properly.

The screenshot is used on the plugin info page and can be taller.
"""
from PIL import Image, ImageDraw, ImageFont
import os

OUT = 'modgui'
BG_TOP, BG_BOT = (51, 64, 78), (19, 25, 32)
TEAL, AMBER, TEXT, DIM = (76, 208, 180), (240, 196, 106), (238, 245, 251), (111, 133, 152)


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


def fade_bar(d, x, y, w, pos):
    """The crossfade rail: the plugin's one recognisable graphic."""
    d.rounded_rectangle([x, y, x + w, y + 9], radius=4, fill=(13, 18, 24))
    fw = int(w * pos)
    if fw > 8:
        d.rounded_rectangle([x, y, x + fw, y + 9], radius=4, fill=TEAL)
    cx = x + fw
    d.ellipse([cx - 8, y - 4, cx + 8, y + 13], fill=TEXT, outline=(22, 28, 36), width=2)


def thumbnail(name, label, path, stereo=False):
    """256x64 nameplate: wide, so the bank strip does not squeeze it."""
    W, H = 256, 64
    img = gradient(W, H)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=8, outline=(11, 14, 18))

    d.text((14, 8), 'REMY', font=font(9), fill=DIM)
    d.text((14, 18), label, font=font(22, True), fill=TEXT)
    if stereo:
        d.text((15, 44), 'STEREO', font=font(11, True), fill=AMBER)

    fade_bar(d, 150, 22, 92, 0.62)
    d.text((150, 6), 'IN 1', font=font(8), fill=DIM)
    d.text((222, 6), 'IN 2', font=font(8), fill=TEAL)
    if stereo:
        fade_bar(d, 150, 42, 92, 0.62)
    else:
        d.text((150, 42), name, font=font(8), fill=DIM)

    img.save(path)
    return img.size


def screenshot(path, stereo=False):
    """The pedal itself, for the plugin info page."""
    W, H = 260, 322
    img = gradient(W, H)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=12, outline=(11, 14, 18))

    d.text((W // 2, 9), 'REMY', font=font(9), fill=DIM, anchor='ma')
    d.text((W // 2, 22), 'FADE', font=font(21, True), fill=TEXT, anchor='ma')
    if stereo:
        d.text((W // 2, 44), 'STEREO', font=font(11, True), fill=AMBER, anchor='ma')
    d.ellipse([W - 24, 14, W - 16, 22], fill=(76, 227, 160))

    if stereo:
        # six jacks instead of three: worth showing, it changes the wiring
        for j in range(4):
            d.ellipse([2, 96 + j * 15, 10, 104 + j * 15], outline=DIM)
        for j in range(2):
            d.ellipse([W - 10, 103 + j * 15, W - 2, 111 + j * 15], outline=DIM)
    d.text((26, 58), 'IN 1', font=font(9), fill=DIM)
    d.text((W - 50, 58), 'IN 2', font=font(9), fill=DIM)
    d.text((W // 2, 58), '62%', font=font(9, True), fill=TEXT, anchor='ma')
    fade_bar(d, 26, 73, 208, 0.62)

    for i, (title, colour) in enumerate([('1>2', TEAL), ('2>1', TEAL),
                                         ('GAIN 1', AMBER), ('GAIN 2', AMBER)]):
        cx = 37 + i * 62
        d.ellipse([cx - 21, 92, cx + 21, 134], fill=(56, 67, 79), outline=(14, 19, 26))
        d.line([cx, 96, cx, 109], fill=colour, width=3)
        d.text((cx, 141), title, font=font(9), fill=DIM, anchor='ma')

    d.rounded_rectangle([40, 186, 92, 211], radius=13, fill=(13, 18, 24))
    d.ellipse([69, 189, 88, 208], fill=TEAL)
    d.text((66, 218), 'TOGGLE', font=font(9), fill=DIM, anchor='ma')

    d.rounded_rectangle([173, 186, 215, 211], radius=6, fill=(58, 70, 82),
                        outline=(14, 19, 26))
    d.ellipse([190, 194, 198, 202], fill=AMBER)
    d.text((194, 218), 'TRIGGER', font=font(9), fill=DIM, anchor='ma')

    d.ellipse([W // 2 - 25, H - 64, W // 2 + 25, H - 14], fill=(61, 73, 86),
              outline=(14, 19, 26))

    img.save(path)
    return img.size


if __name__ == '__main__':
    print('thumbnail mono  :', thumbnail('CROSSFADE', 'FADE', f'{OUT}/thumbnail-fade.png'))
    print('screenshot mono :', screenshot(f'{OUT}/screenshot-fade.png'))
    print('thumbnail stereo:', thumbnail('CROSSFADE', 'FADE',
                                         f'{OUT}/thumbnail-fade-stereo.png', stereo=True))
    print('screenshot stereo:', screenshot(f'{OUT}/screenshot-fade-stereo.png', stereo=True))
