# Tekstuur: eigen firmware voor de M-VAVE FM-1

Een volledig eigen instrument: eigen klankmotor, eigen schermen, eigen bediening. Het platform eronder (scherm, knoppen,
audio, USB, installeren) is [fm1-base](https://github.com/charlesvestal/fm1-base) met felucca-bsp (beide MPL-2.0);
alles in deze map is van BARNIR.

## Wat het doet

Vier lagen (A, B, C, D). Elke laag bespeelt een opgenomen bron: negen ingebouwd (STRINGS, CELLO, HORN, ORGAN, GLASS,
PSALTR, VIBES, REED, NOISE) en tot acht eigen bronnen die je vanaf de Mac stuurt. Een laag speelt zijn bron op een van
vier manieren:

- LOOP: drie koppen per noot draaien elk een eigen lus van een andere lengte, en kunnen een octaaf of kwint uit elkaar
  liggen. Lang, rustig, steeds verschuivend.
- GRAIN: een wolk korte korrels rond een plek in de bron.
- STRETCH: de bron uitgerekt tot hij bijna stilstaat; SPEED bepaalt hoe snel hij er toch doorheen kruipt.
- RESON: de bron slaat drie gestemde snaren aan die meeklinken met de noot. RING is hoe lang ze naklinken, EXCITE hoe
  vaak ze worden aangeslagen. Met NOISE als bron klinkt het als een windharp, met een instrument als een sitar of klok.

Elke laag heeft een eigen filter met resonantie. De vier lagen worden gemengd met een vectorpunt (een hoek is een laag,
het midden alles). Evolve laat dat punt, de plek in de bron en het filter langzaam uit zichzelf bewegen. Daarna chorus,
delay en galm. De galm heeft SHIMMER (de galm komt een octaaf hoger terug) en FREEZE (de galm houdt vast wat erin zit).

## Bediening

| Knop | Doet |
| --- | --- |
| Toetsen | spelen, aanslaggevoelig |
| OCT- / OCT+ | octaaf lager of hoger |
| PLAY | noten vasthouden (een toets zet zijn noot aan, nog eens: uit); nog eens PLAY: alles los |
| SEQ | FREEZE: de galm houdt vast wat erin zit, tot je nog eens drukt (FRZ licht op) |
| PRESET | preset kiezen: 56 ingebouwd, daarna je eigen (U01 tot U16) |
| SAVE | de klank van nu bewaren als eigen preset (het lampje brandt als er iets veranderd is) |
| SELECT of SEL | laag A tot D kiezen |
| ALGORITHM | door de pagina's bladeren |
| HOME, GLO, LFO, EDIT, ENV, FX | direct naar PLAY, MIX, MOVE, SOURCE/PITCH/TONE, ENVELOPE, EFFECTS/REVERB |
| K1 tot K4 | de vier regelaars van de pagina |
| REC | toont de belasting: procent, haperingen (X), overbelastingen (O), ruimte (P) |

Pagina's: PLAY (vector X en Y, Evolve, tijd), MIX (niveau per laag), MOVE (Evolve per laag), SOURCE (bron, engine, plek,
lengte of RING), PITCH (interval, toonhoogte, dichtheid of SPEED of EXCITE, spreiding), TONE (filter, resonantie, zweving,
breedte), ENVELOPE (aanzet, uitklank, galm, delay), EFFECTS (chorus, delaytijd, terugkoppeling, volume), REVERB (grootte,
demping, shimmer).

Presets: 1 tot 12 lussen en korrels, 13 tot 20 snaren, 21 tot 28 uitgerekt, 29 tot 31 wind en ruis, 32 tot 35 shimmer,
36 tot 38 bewegend filter, 39 tot 41 donker, 42 tot 44 licht, 45 tot 48 gemengd, 49 tot 56 met de extra bronnen (zie
hieronder; zonder die bank speelt een ingebouwde bron in hun plaats). Alle presets staan op gelijke sterkte
(`dsp/tk_levels.h`, uitgerekend door `build/tk_host levels`).

Geluid over USB: de FM-1 is op de computer een geluidsingang ("Tekstuur FM-1") en stuurt altijd op vol niveau; de
MASTER-knop regelt alleen de speaker en de koptelefoon.

MIDI over USB en de TRS-ingang: noten, pedaal (CC 64), modulatiewiel (CC 1) op Evolve, programmawissel kiest een preset.

## Eigen bronnen

Naast de ingebouwde bronnen is er ruimte voor een bank: hooguit 8 opnamen, samen 8,5 seconde. De bank staat los van de
firmware in het geheugen van de FM-1 en blijft staan bij een update. Een nieuwe bank vervangt de vorige.

```
PY=~/Desktop/fm1/venv-midi/bin/python
$PY tools/tekstuur_samples.py stuur bank/extra.tkb              # de extra bronnen: FLUTE, PIANO, HARMON, GONG, BELL
$PY tools/tekstuur_samples.py eigen mijn1.wav mijn2.wav         # je eigen opnamen (elk bestand dat ffmpeg leest)
$PY tools/tekstuur_samples.py eigen ZEE=zee.wav:start=3:duur=2 STEM=stem.wav:noot=57
$PY tools/tekstuur_samples.py wis                               # de bank weghalen
```

De toonhoogte van een opname wordt gemeten; geef `:noot=` als dat niet klopt (60 is midden-C). Zonder `:duur=` wordt de
ruimte eerlijk verdeeld. `--hz 11025` geeft dubbele tijd tegen halve kwaliteit. Sturen duurt ongeveer tien seconden; het
geluid stopt even. De nieuwe bronnen staan in SOURCE voorbij NOISE.

## Bouwen

```
./bouw.sh host          # simulator op de Mac, draait tests/*.txt (schermen en geluid in build/host/)
./bouw.sh device --no-ble   # de firmware: build/tekstuur.fwsc (Docker moet draaien)
```

Installeren: `python tools/fm1_install.py tekstuur.fwsc --yes` uit fm1-base. De motor los meten en beluisteren:
`cc -O2 -o build/tk_host test/tk_host.c dsp/tk_engine.c -lm && build/tk_host && build/tk_host demo build/demo`.

Bronnen: zie BRONNEN.md (CC0).
