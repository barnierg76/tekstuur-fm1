#!/usr/bin/env python3
"""Tabellen voor de Tekstuur-motor: eigen/dsp/tk_tables.h (ingecheckt).
  TK_SIN2[257]   sin^2 van 0 tot een kwart periode, Q15 (vensters van lussen en korrels)
  TK_POW2[193]   2^(i/192), Q15 over een octaaf (toonhoogte in 1/16 halve toon)
  TK_SINE[257]   een hele sinusperiode, Q15 (chorus, galmmodulatie)"""
import math
from pathlib import Path
out = ["/* gemaakt door eigen/tools/gen_tables.py, niet bewerken */", "#pragma once", "#include <stdint.h>"]
def emit(name, typ, vals):
    out.append(f"static const {typ} {name}[{len(vals)}] = {{")
    for i in range(0, len(vals), 12):
        out.append("    " + ", ".join(str(v) for v in vals[i:i + 12]) + ",")
    out.append("};")
emit("TK_SIN2", "uint16_t", [round(32767 * math.sin(math.pi / 2 * i / 256) ** 2) for i in range(257)])
emit("TK_POW2", "uint32_t", [round(32768 * 2 ** (i / 192)) for i in range(193)])
emit("TK_SINE", "int16_t", [round(32767 * math.sin(2 * math.pi * i / 256)) for i in range(257)])
Path(__file__).resolve().parents[1].joinpath("dsp", "tk_tables.h").write_text("\n".join(out) + "\n")
print("tk_tables.h")
