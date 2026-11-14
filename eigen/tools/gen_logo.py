#!/usr/bin/env python3
"""Het Tekstuur-woordmerk voor de FM-1: tk_logo.h. Copyright (C) 2026 BARNIR.
  tools/gen_logo.py FONT.ttf [VOORBEELD.png]
Zoals in de plugin (Tekstuur src/ui/Style.h misregText, glowDot): TEKSTUUR in een halfvet lettertype met ruime
spatiering, papierwit op donker papier, een flamingo-afdruk linksonder ervan en een aqua rechtsboven (riso-misregistratie),
met eronder de inktstippen van de vier lagen. FONT.ttf: Inter Tight (variabel, SIL OFL 1.1).
16 vaste kleuren (RGB565), 4 bits per pixel."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
BG, PAPER = (0x12, 0x11, 0x18), (0xf1, 0xe9, 0xda)
PINK, AQUA, YELLOW, VIOLET = (0xf2, 0x8b, 0x9b), (0x79, 0xcf, 0xe0), (0xf3, 0xc6, 0x4d), (0xb3, 0xa3, 0xee)
SS = 4                                  # drawn SS times as large, then reduced


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


# the 16 colours: the inks and paper, and the blends their edges need (a free quantiser drops the dots' sunflower
# and mauve for more greys). 0 is the paper: the firmware leaves those pixels alone
COLS = [BG, PAPER, PINK, AQUA, YELLOW, VIOLET, mix(BG, PAPER, 0.33), mix(BG, PAPER, 0.66), mix(BG, PINK, 0.5),
        mix(BG, AQUA, 0.5), mix(PINK, PAPER, 0.5), mix(AQUA, PAPER, 0.5), mix(BG, YELLOW, 0.5), mix(BG, VIOLET, 0.5),
        mix(PINK, AQUA, 0.5), mix(PINK, YELLOW, 0.5)]


def render(w, h, px, track, off, dots):
    """-> (the 4-bit pixels, the wordmark's right edge); dots: (radius, centre y, pitch) or None"""
    font = ImageFont.truetype(sys.argv[1], px * SS)
    font.set_variation_by_axes([600])
    im = Image.new("RGBA", (w * SS, h * SS), BG + (255,))
    x0 = 2 + off
    for colour, alpha, dx, dy in ((PINK, 0.95, -off, off * 0.7), (AQUA, 0.95, off, -off * 0.5), (PAPER, 1.0, 0, 0)):
        layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        x = (x0 + dx) * SS
        for ch in "TEKSTUUR":
            d.text((x, (off * 0.5 + dy) * SS), ch, font=font, fill=colour + (int(alpha * 255),))
            x += font.getlength(ch) + track * px * SS
        end = x / SS - track * px + off - dx
        im = Image.alpha_composite(im, layer)
    assert end <= w, (end, w)
    if dots:
        r, y, pitch = dots
        d = ImageDraw.Draw(im, "RGBA")
        for k, c in enumerate((PINK, AQUA, YELLOW, VIOLET)):
            x = x0 + r + k * pitch
            second = AQUA if c == PINK else PINK       # (Style.h secondInk)
            o = r * 0.42
            for cc, ox, oy, a in ((second, o, o * 0.8, 217), (c, 0, 0, 255)):
                d.ellipse([(x - r + ox) * SS, (y - r + oy) * SS, (x + r + ox) * SS, (y + r + oy) * SS], fill=cc + (a,))
    small = im.convert("RGB").resize((w, h), Image.LANCZOS)
    pim = Image.new("P", (1, 1))
    pim.putpalette([v for c in COLS for v in c] + list(BG) * 240)
    q = small.quantize(palette=pim, dither=Image.Dither.NONE)
    return [v if v < 16 else 0 for v in q.get_flattened_data()], end


def emit(out, name, w, h, px):
    out += [f"#define {name}_W {w}", f"#define {name}_H {h}", f"static const uint8_t {name}_PX[{name}_W * {name}_H / 2] = {{"]
    by = [px[i] << 4 | px[i + 1] for i in range(0, w * h, 2)]
    for i in range(0, len(by), 24):
        out.append("    " + ",".join(f"0x{b:02x}" for b in by[i:i + 24]) + ",")
    out.append("};")
    return len(by)


def main():
    big = (232, 96, 37, 0.105, 3.6, (9.0, 78.0, 34.0))
    out = ["/* Tekstuur voor de FM-1: het woordmerk voor het opstartscherm (tools/gen_logo.py, niet bewerken).",
           " * Copyright (C) 2026 BARNIR. Alle rechten voorbehouden. 4 bits per pixel, kleur 0 is het papier. */",
           "static const uint16_t TK_LOGO_PAL[16] = {",
           "    " + ", ".join(f"0x{((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3):04x}" for r, g, b in COLS) + ",", "};"]
    w, h, px, track, off, dots = big
    p, end = render(w, h, px, track, off, dots)
    total = emit(out, "TK_LOGO", w, h, p)
    (ROOT / "tk_logo.h").write_text("\n".join(out) + "\n")
    if len(sys.argv) > 2:
        im = Image.new("RGB", (w, h))
        im.putdata([COLS[v] for v in p])
        im.resize((w * 3, h * 3), Image.NEAREST).save(sys.argv[2])
    print(f"tk_logo.h: {total} B, het woordmerk eindigt op x = {end:.1f}")


if __name__ == "__main__":
    main()
