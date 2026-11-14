# Tekstuur voor de M-VAVE FM-1

Een kleine versie van Tekstuur als klankmotor voor de M-VAVE FM-1. Geen overzetting van de plugin:
de FM-1 heeft een kleine chip en weinig samplegeheugen. Deze motor neemt drie dingen uit Tekstuur
mee die de FM-1 (en de GRAIN-motor van de Felucca-firmware) niet heeft:

- **Multi-Loop**: per noot tot drie afspeelkoppen over één korte sample, elk met een eigen luslengte
  (verhoudingen 256 : 331 : 199, priemgetallen, dus het patroon herhaalt pas na uren). Elke lus
  vloeit over in de volgende zonder naad.
- **Intervallen**: de koppen spelen unisono, in octaven, met een kwint, of een octaaf en twee lager.
- **Evolve**: de leespositie reist vanzelf langzaam door de sample (twee trage golven van 23 en 37 s).

Regelaars: POS, LEN (50 ms tot 4 s), INTV, PTCH, DRFT (verstemming en positiespreiding), EVOL, TONE.

## Eigen firmware (14 november 2026)

Op de FM-1 staat nu een volledig eigen Tekstuur: eigen klankmotor en eigen schermen, gebouwd op fm1-base. Zie
[eigen/README.md](eigen/README.md) voor wat het doet en hoe je het bedient.

- `firmware/tekstuur-eigen-fm1.fwsc`: het pakket. Vier engines per laag (LOOP, GRAIN, STRETCH, RESON), filter met
  resonantie, galm met shimmer en freeze, 56 presets.
- `firmware/tekstuur-extra-bronnen.tkb`: vijf extra bronnen (fluit, piano, mondharmonica, gong, buisklok); sturen met
  `eigen/tools/tekstuur_samples.py stuur`. Met hetzelfde gereedschap stuur je eigen opnamen.
- Luisteren: `luisteren/eigen-firmware/` (alle 56 presets, en een opname van het apparaat zelf). Schermen: `schermen/`.

De eerdere versie op Felucca staat er nog als `firmware/tekstuur-fm1.fwsc`.

## Stand

De FM-1 ís nu Tekstuur (14 november 2026, versie v1.0). Het apparaat start op als Tekstuur, kent alleen de
Tekstuur-motor, de vier sporen zijn vier lagen (SLOW TIDE, OCTAVES, FIFTH SKY, DEEP) en er zijn tien presets.
Sequencer, effecten, projecten en de update-functie komen van Felucca (GPL-3.0, Leo Kuroshita / Hügelton
Instruments); dat staat in MENU > ABOUT en moet daar blijven staan.

- `firmware/tekstuur-fm1.fwsc`: het pakket dat erop staat (SHA-256 b23449c9…13fe36c, identiteit FM-1_900).
- `firmware/felucca-tekstuur.patch` en `firmware/felucca-bsp-tekstuur.patch`: alle wijzigingen op Felucca v1.8.2.
- `firmware/felucca-tekstuur.fwsc`: de eerste versie (heel Felucca met Tekstuur als extra motor).
- `schermen-tekstuur.png`: de schermen uit de simulator.
- Terug naar de fabriek: `fm1_install.py FM-1_015.fwsc --yes` (officiële V15).

Niet gecontroleerd: het geluid en de processorbelasting op het apparaat zelf (vier lagen tegelijk is nooit
gemeten), en het opstartscherm is alleen in de simulator gezien.

## Bouwen en testen

    cc -O2 -o build/tx_test test/tx_test.c src/tx_engine.c -lm
    ./build/tx_test          # metingen
    ./build/tx_test demo     # luistervoorbeelden in build/ (bronnen: build/cello.raw, build/kane.raw)

De tabellen in `src/tx_tables.h` zijn sin²(π/2 · i/128) in Q15 en 2^(i/256) in Q16.
