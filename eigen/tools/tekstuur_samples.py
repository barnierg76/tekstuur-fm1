#!/usr/bin/env python3
"""Eigen bronnen voor Tekstuur op de FM-1: een bank van hooguit 8 opnamen, samen 8,5 seconde, die naast de negen
ingebouwde bronnen komt te staan (SOURCE op het apparaat). De bank staat los van de firmware in het geheugen van de
FM-1 en blijft staan bij een update.

  tekstuur_samples.py eigen A.wav B.wav ...     maak een bank van deze opnamen en stuur hem naar de FM-1
  tekstuur_samples.py bank UIT.tkb BRON ...     alleen de bank maken
  tekstuur_samples.py stuur BANK.tkb            een bank naar de FM-1 sturen (bijv. bank/extra.tkb)
  tekstuur_samples.py wis                       de bank van de FM-1 halen
  tekstuur_samples.py toon BANK.tkb             wat er in een bank zit

Een BRON is een geluidsbestand (alles wat ffmpeg leest), met naar wens  NAAM=bestand  en daarachter
  :noot=60     de toonhoogte van de opname als MIDI-noot (60 = midden-C; een breuk mag). Zonder: gemeten
  :start=1.2   vanaf deze seconde
  :duur=1.5    zoveel seconden (zonder: de ruimte eerlijk verdeeld)
Opties: --hz 11025 (halve kwaliteit, dubbele tijd), --base MAP (fm1-base, voor het sturen).
Sturen heeft mido en python-rtmidi nodig (~/Desktop/fm1/venv-midi/bin/python)."""
import math, os, struct, subprocess, sys, time, zlib
from pathlib import Path

MAGIC = b"TKB1"
BANK_MAX = 0xDC000 - 0x97000            # de ruimte op de FM-1 (app.c BANK_BASE, BANK_MAX)
NMAX = 8
CMD = 0x60                              # app.c app_usb_cmd
OK_ADDR, FAIL_ADDR = 0xC0000000, 0xCF000000

def ffmpeg(path, start, secs, rate):
    cmd = ["ffmpeg", "-v", "error", "-ss", str(start)] + (["-t", str(secs)] if secs else []) + \
          ["-i", str(path), "-ac", "1", "-ar", str(rate), "-f", "s16le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    return list(struct.unpack(f"<{len(raw) // 2}h", raw))

def pitch(x, rate):
    """de toonhoogte (MIDI, breuk) uit de autocorrelatie van 0,2 s in het midden; None als er geen duidelijke toon is"""
    n = min(len(x), int(0.2 * rate))
    a = len(x) // 2 - n // 2
    w = x[a:a + n]
    m = sum(w) / n
    w = [v - m for v in w]
    e = sum(v * v for v in w) or 1.0
    lo, hi = int(rate / 1200), min(int(rate / 40), n // 2)
    r = [0.0] * (hi + 2)
    for lag in range(lo - 1, hi + 2):
        r[lag] = sum(w[i] * w[i + lag] for i in range(0, n - lag)) / e * n / (n - lag)
    best, peak = 0, 0.0
    for lag in range(lo, hi + 1):       # de eerste duidelijke top: niet de sterkste, anders valt de keuze een octaaf te laag
        if r[lag] > r[lag - 1] and r[lag] >= r[lag + 1] and r[lag] > peak:
            if not best or r[lag] > peak * 1.15:
                best, peak = lag, r[lag]
    if not best or peak < 0.6:
        return None
    y0, y1, y2 = r[best - 1], r[best], r[best + 1]
    d = (y0 - y2) / (2 * (y0 - 2 * y1 + y2)) if y0 - 2 * y1 + y2 else 0.0
    return 69 + 12 * math.log2(rate / (best + d) / 440)

def pack(x):
    """gelijke sterkte (RMS -20 dBFS, piek hooguit -1), 10 ms in- en uitloop, 12 bits: paren in 3 bytes"""
    n = len(x) & ~1
    x = x[:n]
    rms = math.sqrt(sum(v * v for v in x) / n) or 1
    peak = max(abs(v) for v in x) or 1
    g = min(32768 * 10 ** (-20 / 20) / rms, 32768 * 10 ** (-1 / 20) / peak)
    f = 220
    y = [max(-2048, min(2047, round(v * g * min(1.0, i / f, (n - 1 - i) / f) / 16))) for i, v in enumerate(x)]
    by = bytearray()
    for i in range(0, n, 2):
        a, b = y[i] & 0xFFF, y[i + 1] & 0xFFF
        by += bytes((a & 0xFF, (a >> 8) | ((b & 15) << 4), b >> 4))
    return n, bytes(by)

def make(specs, rate, out=sys.stdout):
    if not 1 <= len(specs) <= NMAX:
        raise SystemExit(f"een bank heeft 1 tot {NMAX} bronnen")
    room = (BANK_MAX - 16 - 24 * len(specs)) * 2 // 3          # samples in totaal
    items = []
    for s in specs:
        parts = s.split(":")
        head, opt = parts[0], dict(p.split("=", 1) for p in parts[1:])
        name, path = head.split("=", 1) if "=" in head else (Path(head).stem, head)
        name = "".join(c for c in name.upper() if c.isalnum())[:6] or "EIGEN"
        if not Path(path).is_file():
            raise SystemExit(f"{path}: geen bestand")
        items.append([name, path, opt])
    fixed = sum(int(float(o["duur"]) * rate) for _, _, o in items if "duur" in o)
    free = [i for i in items if "duur" not in i[2]]
    share = (room - fixed) // len(free) if free else 0
    entries, data = [], b""
    for name, path, opt in items:
        want = int(float(opt["duur"]) * rate) if "duur" in opt else share
        x = ffmpeg(path, float(opt.get("start", 0)), want / rate, rate)[:want]
        if len(x) < rate // 2:
            raise SystemExit(f"{path}: te kort (minstens een halve seconde)")
        root = float(opt["noot"]) if "noot" in opt else pitch(x, rate)
        note = "" if "noot" in opt else " (gemeten)" if root is not None else " (geen duidelijke toon: 60)"
        root = 60.0 if root is None else root
        rooti = max(0, min(127, round(root)))
        rate_d = round(rate * 2 ** ((rooti - root) / 12))        # de rest van een breuk gaat in de frequentie
        n, by = pack(x)
        entries.append((name, n, rate_d, rooti))
        data += by
        print(f"  {name:6s} {n / rate:5.2f} s  noot {root:6.2f}{note}  {len(by)} B", file=out)
    off, table = 16 + 24 * len(entries), b""
    for (name, n, rate_d, rooti), in zip(entries):
        table += struct.pack("<8sIIIB3x", name.encode(), off, n, rate_d, rooti)
        off += n * 3 // 2
    body = struct.pack("<BBH", len(entries), 0, 0) + table + data
    bank = MAGIC + struct.pack("<I", 12 + len(body)) + b"\0\0\0\0" + body
    bank = bank[:8] + struct.pack("<I", zlib.crc32(bank[12:])) + bank[12:]
    if len(bank) > BANK_MAX:
        raise SystemExit(f"de bank is {len(bank)} B, er past {BANK_MAX} B: kortere opnamen (:duur=) of --hz 11025")
    print(f"bank: {len(entries)} bronnen, {len(bank)} B van {BANK_MAX}", file=out)
    return bank

def show(bank, out=sys.stdout):
    if bank[:4] != MAGIC or struct.unpack_from("<I", bank, 4)[0] != len(bank) or \
            struct.unpack_from("<I", bank, 8)[0] != zlib.crc32(bank[12:]):
        raise SystemExit("geen (hele) Tekstuur-bank")
    for i in range(bank[12]):
        name, off, n, rate, root = struct.unpack_from("<8sIIIB", bank, 16 + 24 * i)
        print(f"  {name.rstrip(bytes(1)).decode():6s} {n:6d} samples  {rate} Hz  noot {root}", file=out)

def send(bank, base, out=sys.stdout):
    sys.path.insert(0, str(Path(base) / "tools"))
    import fm1_install as I
    try:
        backend = I.MidoBackend()
    except ImportError:
        raise SystemExit("sturen heeft mido en python-rtmidi nodig: draai dit met ~/Desktop/fm1/venv-midi/bin/python")
    up = I.Updater(backend, "Tekstuur")
    dev = up.find(only_port=False)
    if not dev:
        raise SystemExit("geen FM-1 gevonden: zit de USB-kabel erin en draait Tekstuur?")
    if not dev.id.text.startswith("FM-1_27"):
        dev.link.close()
        raise SystemExit(f"de FM-1 draait {dev.id.text}, niet Tekstuur")
    link = dev.link
    body = struct.pack("<II", len(bank), zlib.crc32(bank))
    msg = bytes([0x00, 0x59, CMD, len(body), 0, 0]) + body + bytes([~sum(body) & 0xFF])
    try:
        link.drain()
        link.send(b"\xF0" + I.pack7(msg) + b"\xF7")
        last, asked = time.monotonic(), 0
        while True:
            pkt = link.read(0.5)
            if pkt is None:
                if link.lost:
                    raise SystemExit("\nde FM-1 is losgeraakt: stuur de bank opnieuw")
                if time.monotonic() - last > (12.0 if asked else 4.0):
                    raise SystemExit("\nde FM-1 vraagt niets meer" if asked else
                                     "de FM-1 antwoordt niet: een oudere Tekstuur, zonder eigen bronnen?")
                continue
            r = I.parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last, asked = time.monotonic(), asked + 1
            if addr & 0xFF000000 in (OK_ADDR, FAIL_ADDR):
                link.send(I.response(addr, b"success\0", fl))
                print(file=out)
                if addr & 0xFF000000 == FAIL_ADDR:
                    why = {1: "de bank is te groot", 2: "het geheugen wissen", 3: "de overdracht", 4: "het schrijven",
                           5: "de controle achteraf"}.get(addr & 0xFF, f"code {addr & 0xFF}")
                    raise SystemExit(f"mislukt: {why}. De FM-1 heeft nu geen eigen bronnen; stuur de bank opnieuw.")
                print(f"klaar: de FM-1 heeft nu {addr & 0xFF} eigen bron{'nen' if addr & 0xFF != 1 else ''} "
                      "(SOURCE, voorbij NOISE)" if bank else "klaar: de eigen bronnen zijn van de FM-1 gehaald", file=out)
                return
            if n > 512 or addr >= len(bank):
                raise SystemExit(f"\nde FM-1 vroeg om {addr:#x}+{n}, buiten de bank")
            d = bank[addr:addr + n]
            link.send(I.response(addr, d + bytes(n - len(d)), fl))
            print(f"\rsturen {min(addr + n, len(bank)) * 100 // len(bank):3d}%", end="", file=out, flush=True)
    finally:
        link.close()

def main(argv):
    rate, base, args, i = 22050, os.environ.get("FM1_BASE", str(Path.home() / "Desktop/fm1/ref/fm1-base")), [], 0
    while i < len(argv):
        if argv[i] == "--hz":
            rate, i = int(argv[i + 1]), i + 1
        elif argv[i] == "--base":
            base, i = argv[i + 1], i + 1
        else:
            args.append(argv[i])
        i += 1
    if not args or args[0] not in ("eigen", "bank", "stuur", "wis", "toon"):
        raise SystemExit(__doc__)
    if args[0] == "bank":
        Path(args[1]).write_bytes(make(args[2:], rate))
    elif args[0] == "toon":
        show(Path(args[1]).read_bytes())
    elif args[0] == "stuur":
        bank = Path(args[1]).read_bytes()
        show(bank)
        send(bank, base)
    elif args[0] == "wis":
        send(b"", base)
    else:
        send(make(args[1:], rate), base)

if __name__ == "__main__":
    main(sys.argv[1:])
