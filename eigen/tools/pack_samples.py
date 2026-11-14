#!/usr/bin/env python3
"""Bronsamples inpakken voor de Tekstuur-motor: MANIFEST -> dsp/tk_samples.h (12 bits per sample, mono).
  tools/pack_samples.py tools/samples.txt
Een regel van het manifest:  NAAM  PAD  GRONDTOON(MIDI)  START(s)  DUUR(s)  FREQUENTIE(Hz)
Elke bron wordt mono gemaakt, naar de frequentie omgezet (ffmpeg), op gelijke sterkte gebracht (RMS -20 dBFS,
piek hooguit -1 dBFS) en krijgt 10 ms in- en uitloop."""
import math, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def load(path, start, secs, rate):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-ss", str(start), "-t", str(secs), "-i", path, "-ac", "1", "-ar", str(rate),
                          "-f", "s16le", "-"], capture_output=True, check=True).stdout
    return list(struct.unpack(f"<{len(raw) // 2}h", raw))

def main():
    out = ["/* gemaakt door eigen/tools/pack_samples.py, niet bewerken */", "#pragma once", '#include "tk_engine.h"']
    table, total = [], 0
    for line in Path(sys.argv[1]).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, path, root, start, secs, rate = line.split("\t")
        path = str((Path(sys.argv[1]).parent / path).resolve()) if not path.startswith("/") else path
        if path.endswith("ruis:"):                 # eigen ruis, roze-achtig, altijd dezelfde (een vaste generator)
            import random
            rnd, b, x = random.Random(1), [0.0] * 5, []
            for _ in range(int(float(secs) * int(rate))):
                w = rnd.uniform(-1, 1)
                for k, c in enumerate((0.5, 0.75, 0.9, 0.97, 0.99)):
                    b[k] = c * b[k] + (1 - c) * w
                x.append(int(6000 * (w * 0.2 + sum(b))))
        elif path.endswith(".raw"):
            raw = Path(path).read_bytes()
            x = list(struct.unpack(f"<{len(raw) // 2}h", raw))[int(float(start) * int(rate)):][:int(float(secs) * int(rate))]
        else:
            x = load(path, float(start), float(secs), int(rate))
        n = len(x) & ~1
        x = x[:n]
        rms = math.sqrt(sum(v * v for v in x) / n) or 1
        peak = max(abs(v) for v in x) or 1
        g = min(32768 * 10 ** (-20 / 20) / rms, 32768 * 10 ** (-1 / 20) / peak)
        f = int(0.010 * int(rate))
        y = []
        for i, v in enumerate(x):
            w = min(1.0, i / f, (n - 1 - i) / f)
            y.append(max(-2048, min(2047, round(v * g * w / 16))))
        by = bytearray()
        for i in range(0, n, 2):
            a, b = y[i] & 0xFFF, y[i + 1] & 0xFFF
            by += bytes((a & 0xFF, (a >> 8) | ((b & 15) << 4), b >> 4))
        out.append(f"static const uint8_t TK_SMP_{name}[{len(by)}] = {{")
        for i in range(0, len(by), 32):
            out.append("    " + ",".join(str(v) for v in by[i:i + 32]) + ",")
        out.append("};")
        # een gemeten grondtoon mag een breuk zijn: de hele noot gaat in de tabel, de rest in de opgegeven frequentie
        rooti = round(float(root))
        rate_d = round(int(rate) * 2 ** ((rooti - float(root)) / 12))
        table.append(f'    {{TK_SMP_{name}, {n}, {rate_d}, {rooti}, "{name}"}},')
        total += len(by)
        print(f"{name:8s} {n / int(rate):5.2f} s  {rate_d} Hz  grondtoon {rooti}  {len(by)} B  versterking {20 * math.log10(g):+.1f} dB")
    out += ["static const tk_src_t TK_SOURCES[] = {"] + table + ["};", f"#define TK_NSOURCES {len(table)}u"]
    (ROOT / "dsp" / "tk_samples.h").write_text("\n".join(out) + "\n")
    print(f"tk_samples.h: {len(table)} bronnen, {total} B")

main()
