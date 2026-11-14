#!/usr/bin/env python3
"""Schermafbeeldingen van de simulator naast elkaar: tools/overzicht.py UIT.png A.png B.png ... (vier per rij, 2x)."""
import sys
from PIL import Image
ims = [Image.open(p).convert("RGB") for p in sys.argv[2:]]
w, h = ims[0].size
rows = (len(ims) + 3) // 4
sheet = Image.new("RGB", (4 * (w * 2 + 8) + 8, rows * (h * 2 + 8) + 8), (60, 60, 60))
for i, im in enumerate(ims):
    sheet.paste(im.resize((w * 2, h * 2), Image.NEAREST), (8 + (i % 4) * (w * 2 + 8), 8 + (i // 4) * (h * 2 + 8)))
sheet.save(sys.argv[1])
