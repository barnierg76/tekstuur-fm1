# Lessen: Tekstuur voor de FM-1

## Onderzoek (november 2026)
- Het apparaat is de M-VAVE FM-1 (USB 0x4c4a:0xc755, JieLi AC791N, pi32v2). M-VAVE heeft een open kit
  (`m-vave/fm1_open`): de app-laag is open (GPLv3 met uitzondering), de klankmotor is een gesloten bibliotheek,
  en bouwen kan alleen op Windows.
- Felucca (`hugelton/Felucca`, GPL-3.0) is een volledig open firmware met eigen motoren, bouwt op macOS via
  Docker, heeft hosttests en een web-installer die terug kan naar de fabrieksfirmware V15. Dat is de basis.
- Felucca heeft al een korrelmotor (GRAIN: 12 korrels per part, 3 stemmen, korrels 10 tot 390 ms, ADPCM-bron
  met zoekindex). Een "Tekstuur-versie" moet dus iets anders doen dan korrels: lange lussen, intervallen, Evolve.
- Een motor in Felucca: `engine_t` met 8 regelaars, `note_on`, `render` (mono, per stem), `block`, en geheugen
  per part (hooguit 12888 bytes). Motornummers worden opgeslagen: alleen achteraan toevoegen. Plek 14 is van
  de ADPCM-motor die standaard uit staat; overal waar `FELUCCA_ADPCM` staat moet een nieuwe motor mee.

## De motor
- Naad: een lus krijgt een sin²-helling aan begin en eind en de volgende start één helling voor het eind
  (sin² + cos² = 1). Starten kan alleen op blokgrenzen; zonder correctie start de nieuwe lus tot een blok te laat
  en rimpelt de som 1,9 %. Met correctie (de lus begint zoveel samples verder in zijn venster en in de sample)
  is de rimpel 0,05 %. De naadtest faalde op de versie zonder correctie.
- Luslengtes in priemverhoudingen, koppen die een kwart seconde na elkaar inzetten.
- Rekenlast: 4 lussen tegelijk bij één stem, 8 bij drie stemmen; GRAIN mag er 12. De binnenste lus is dezelfde
  als die van een voorwaartse korrel, dus de last blijft onder die van GRAIN. Niet gemeten op het apparaat.
- Niveau op ruis: spreiding 2,4 dB over 30 s.

## Niet gecontroleerd
- Niets is beluisterd. Bij de belsample kan de aanslag in een lus vallen en dan hoor je de bel opnieuw aanslaan.
- De motor is nog niet in Felucca gebouwd; daar leest hij ADPCM uit flash via de zoekindex in plaats van een
  16-bit buffer.

## In de firmware (14 november 2026)
- De motor zit in een eigen tak van Felucca (`~/Desktop/fm1/Felucca`, tak `tekstuur`, v1.8.2-basis) als motor 14
  "TEXTUR": `firmware/src/eng_texture.c`. Hij gebruikt GRAIN's ADPCM-zoekindex en zones. De wijziging staat als
  patch in `firmware/felucca-tekstuur.patch`, het pakket in `firmware/felucca-tekstuur.fwsc` (identiteit FM-1_900).
- Motor 14 was de plek van ADPCM (standaard uit). Aangemeld op: core.h (NENGINES), engines.c (tabel, volgorde,
  ontbrekende sample), category.c, params.c (naam), midi_control.c (CC-tabel), fx.c (kostentabel), favorites.c,
  ui_layer.c, tools/build.py. Vergeten plekken vond Felucca's eigen testreeks (categorieën, volgorde).
- Metingen in de firmwarecode (`tests/texture_test.c`): naadrimpel 0,23 % (1,22 % zonder correctie, de test faalt
  dan), leessnelheid exact, 11 van 12 lussen bij drie noten, Evolve reist 57027 van 65535, rekentijd 0,86 keer
  die van GRAIN op de Mac.
- Felucca's testreeks: 127 onderdelen, alles groen behalve twee tests van de web-editor (die heeft een eigen
  kopie van de motorlijst en kent TEXTUR niet). Vier nieuwe renders staan niet in `tests/golden.txt`.
- Bouwen op de Mac: venv met Pillow en fontTools, `brew install fribidi` (anders weigert Pillow fontkenmerken),
  Docker, JieLi-toolchain via `tools/get_toolchain.sh`. De SDK van gitee klonen brak steeds af; de drie nodige
  bestanden los ophalen via `.../raw/<tag>/cpu/wl82/tools/...` werkt en de bouw controleert hun SHA-256.
- Installer: `tools/fm1_install.py` heeft mido en python-rtmidi nodig; python-rtmidi bouwt niet op Python 3.14,
  wel op de 3.9 van macOS (`~/Desktop/fm1/venv-midi`).

## Geflasht (14 november 2026)
- Het apparaat draaide Baud Girl FM-1+VA versie 102 (meldt zich als `FM-1_102`). Route, op aanwijzing van Barnier:
  eerst terug naar de officiële V15, dan de Tekstuur-firmware. Beide met `tools/fm1_install.py PAKKET --yes`
  (Python uit `~/Desktop/fm1/venv-midi`). FM-1+VA nam de officiële V15 zonder problemen aan via het standaard
  updateprotocol; daarna V15 -> Tekstuur-build. Het apparaat meldt nu `FM-1_900`, "FELUCCA v1.8.2".
- De installer schrijft pas in stap 2; als stap 1 (de draaiende firmware controleert het pakket) mislukt, is er
  niets geschreven.
- Controle op het apparaat via het editorprotocol (`F0 7D 46 4C <cmd> ... F7`): INFO (1) noemt TEXTUR in de
  motorlijst, PRESET (8) met motor 14 wordt aangenomen, NAMES (10) geeft SLOW TIDE, OCTAVES, FIFTH SKY, DEEP.
- Niet gecontroleerd: het geluid op het apparaat zelf. Opnemen via USB-audio (apparaat heet nu "Felucca") met
  ffmpeg bleef hangen, vermoedelijk op de microfoontoestemming van macOS. Ook de rekenlast op de chip is niet gemeten.
- Terug naar Baud Girl kan via hun installpagina (FM-1+VA-102.fwsc); terug naar fabriek met
  `~/Desktop/fm1/fm1_open/sdk/firmware/FM-1_015.fwsc`.

## Het hele apparaat is Tekstuur (14 november 2026)

Barnier wilde geen Felucca met één extra motor, maar een apparaat dat alleen Tekstuur is.

- Eén schakelaar, `FELUCCA_TEKSTUUR`. In `core.h` staat hij op 0, `tools/build.py` zet hem op 1. Zo blijft de
  upstream-testreeks (129 onderdelen) het volledige Felucca testen. Eerst stond de standaard op 1: toen faalden
  tientallen upstream-tests die niets met Tekstuur te maken hebben.
- Wat de schakelaar doet: `NENG_SHOWN` 1 en `ENGINE_ORDER` {14}, `eng_ok` alleen 14, `TRK_DEF` vier keer motor 14
  met preset 0..3, `set_engine_of` maakt van elke gevraagde motor 14, een project of gebruikerspreset met een andere
  motor laadt als Tekstuur met standaardklank (mix, effecten en stappen blijven), EDIT-laag toont TEXTUR, INIT, CLEAN.
- De andere motoren zitten nog in de firmware (onbereikbaar). Weghalen scheelt flash, maar raakt projectformaat en
  tientallen tests; niet gedaan.
- Naam: opstartscherm, MENU > ABOUT, het editor-antwoord ("TEKSTUUR v1.0") en de USB-naam "Tekstuur FM-1". "FM-1" moet
  in de USB-naam blijven, want de installer zoekt het apparaat op naam; `tekstuur` is ook aan dat patroon toegevoegd.
  macOS toont de poort nog als "FM-1" (naam uit de cache).
- De USB-naam zit in de submodule `firmware/bsp` (eigen commit en eigen patch).
- `tests/tekstuur_test.c`: 8 controles, slagen met de schakelaar aan en 7 ervan falen met de schakelaar uit.
- Testreeks na de ombouw: alleen de twee bekende web-editor-controles falen (de webeditor is niet aangepast).
- Tien presets, gerenderd op gelijke sterkte (gemiddeld -12,8 tot -13,9 dB, piek rond -5 dB).
- Geflasht over de eerste versie heen (Felucca naar Felucca, zelfde identiteit FM-1_900); het apparaat antwoordt
  "TEKSTUUR v1.0" en geeft tien presetnamen voor motor 14.
- Niet gecontroleerd: geluid en belasting op het apparaat, vier lagen tegelijk.

## Logo en kleuren van de plugin (14 november 2026)

- Het "logo" van Tekstuur is het woord TEKSTUUR in riso-druk: papierwit, met een roze afdruk linksonder en een aqua
  afdruk rechtsboven ernaast, en de inktstip in de vier laagkleuren (roze, aqua, geel, mauve). Bron:
  `Tekstuur/src/ui/Style.h` (`misregText`, `glowDot`, `col::`).
- Op de FM-1 is het een bitmap van 208 x 60, 16 vaste kleuren, 4 bits per pixel (6240 bytes), gemaakt door
  `tools/gen_tekstuur_logo.py`. Een vrije kleurreductie gooide geel en mauve weg; een vast inktpalet lost dat op.
- Lettertype: Inter Tight 600 (OFL, zit al in Felucca). De plugin gebruikt Avenir Next; dat is een Apple-lettertype
  en hoort niet in firmware die je kunt verspreiden.
- Kleuren: een extra palet in de tabel breekt upstream-tests (MONO moet het laatste zijn). Daarom gebruikt Tekstuur
  het bestaande CUSTOM-vak, hier RISO genoemd, en zet het aan zolang er niets anders is opgeslagen.
- `tests/ui_render.c` rendert alleen de vaste paletten; met `TK_RISO=1` komen de riso-kleuren op de plek van GREY.
- Bevestigd op het apparaat via de editor: paletlijst eindigt op RISO en palet 10 (RISO) is actief.
- Niet gezien op het apparaat zelf: het opstartscherm (alleen in de simulator).
- Niet gedaan: de vier sporen elk hun eigen laagkleur geven; de interface gebruikt één themakleur (aqua).

## Volledig eigen firmware (begonnen 14 november 2026)

Barnier vond de Felucca-versie niet vet genoeg ("de originele Tekstuur is veel vetter") en wil een volledig eigen
firmware. Hij hoorde op het apparaat vooral fluit en sax: de drie korte bronsamples van Felucca.

- Basis: `charlesvestal/fm1-base` (MPL-2.0, "an app built on it may use any licence") bovenop `felucca-bsp`. Het
  levert scherm, tekenen en lettertypes, toetsen, LED's, audio, MIDI, opslag, een simulator, een bouw- en
  installatieketen, USB-audio naar de computer en schermspiegeling. De app ziet alleen `include/plat.h` en levert
  `app_init`, `app_frame`, `app_render`. Kloon staat in `~/Desktop/fm1/ref/fm1-base`.
- Het uitvoeren van de bouwscripts van die verse kloon werd geblokkeerd (code van buiten); daar is toestemming van
  Barnier voor nodig. Lezen mag wel.
- Ruimte: flash 1 MiB; gegevensgebied 0x97000..0xE0000 (292 KB) plus twee extra vensters (16 en 92 KB); de app zelf
  mag tot 0x93000. Processor 360 MHz zonder drijvende komma; 64-bits vermenigvuldigen is op dit apparaat verdacht
  (felucca-bsp docs/CPU.md), dus de motor vermijdt het in de binnenste lus.
- Eigen motor: `eigen/dsp/tk_engine.c`. Alles is een "doorgang" (stuk bron, snelheid, venster, plek in het beeld);
  LOOP en GRAIN verschillen alleen in hoe ze doorgangen starten. Vier lagen, vectormenging, Evolve, per laag een
  filter en sends, chorus (gekruist, voor breedte), heen-en-weer-delay, galm (3 allpass + 4 lijnen, Hadamard).
- Bronnen: 12 bits per sample (3 bytes per 2 samples). 8 bits mu-law haalt maar ongeveer 38 dB signaal-ruis; 16 bits
  kost een derde meer ruimte.
- Meetpunten (`eigen/build/tk_host`): stil zonder noten; akkoord op vier lagen piek -2,6 dBFS; zij/midden -5,5 dB;
  lus zonder klik (grootste sprong 1,7 % van de piek); stil na uitklinken (-84 dBFS); 225 x realtime op de Mac.
- Fout gevonden met de stiltemeting: een eenpolig filter met `>> 15` of delen blijft op een restje staan (tot -62
  dBFS gelijkspanning). Oplossing: de rest van de vermenigvuldiging bewaren en de volgende keer meetellen.
- De pool van 48 doorgangen loopt vol bij drie noten op vier lagen; de zachtste doorgang dooft dan uit. Hoeveel het
  apparaat aankan is niet gemeten.
- Nog te doen: `% lengte` per sample in de galm vervangen door tellers (delen is duur op dit apparaat), resonator,
  echte bronnen kiezen (VSCO-2-CE en VCSL op GitHub, CC0), schermen, bouwen, flashen.

## Eigen firmware af en op het apparaat (14 november 2026)

Wat werkte:
- fm1-base als platform: app.h, app.c en fm1.toml zijn genoeg. De simulator (`--host`) draait de hele app met een script
  en maakt schermafbeeldingen en WAV: de eerste bouw slaagde voor alle 33 controles voordat er iets geflasht was.
- `mirror = true`: met `fm1_mirror.py` lees je het echte scherm uit en met `fm1_remote.py` bedien je het apparaat vanaf de
  Mac. Zo is op het apparaat zelf gemeten zonder dat iemand een knop aanraakte.
- De motor op de samplefrequentie van de bronnen (22,05 kHz) en in de app verdubbelen met (9(b+c) - (a+d))/16.

Getallen (FM-1, preset STRIJKZEE, USB-audio aan):
- Eerste versie op 44,1 kHz: 20 % in rust, 64 % bij 33 doorgangen. 48 doorgangen zou rond 85 % uitkomen.
- Op 22,05 kHz: ongeveer 17 % in rust en 0,56 % per doorgang. Drie noten (41 doorgangen) 40 %, vijf noten (54) 54 %,
  0 haperingen, 0 overbelastingen. Daarom nu 64 doorgangen en 5 noten.
- Afbeelding 507 KB van 581 KB (bronnen 397 KB: 8 x 1,5 s, 12 bits), RAM 45 KB van 96, pool 148 KB van 336.

Wat misging:
- De bestandsnamen van VSCO-2 CE en VCSL noemen de toon een octaaf lager dan hij klinkt (C4 in de naam is 523 Hz).
  Altijd de toonhoogte meten (autocorrelatie); GLASS zat bovendien 0,27 halve toon naast de naam. De rest gaat in de
  opgegeven samplefrequentie van de bron, zodat de grondtoon een hele noot blijft.
- Een pad met een spatie breekt de bouwscripts: `bouw.sh` kopieert de app eerst naar fm1-base.
- `words.tsv` is verplicht en moet een `# url:`-regel hebben (nu een plaatshouder tot de site er is).
- Een eigen `ST_MAGIC` botste met die van het platform (unity-build: alles in een bestand). Eigen namen een voorvoegsel geven.
- 64-bitsvermenigvuldigingen staan in een aparte niet-inline functie en nooit in een lus per sample; de bouw controleert dit.
- Het lineaire filtercoëfficiënt klopt niet meer dicht bij de halve samplefrequentie: a = x / (1 + x) gebruikt.
- `fm1_remote.py master 4096` zet het volume los van de knop: Barnier hoorde geluid met de knop dicht. Na een meting
  altijd `release-all master pot`, en het volume liever niet op afstand zetten als hij erbij zit.
- Stopt een remote- of mirror-opdracht, dan laat het apparaat alle toetsen los. Een meting met vastgehouden noten moet
  in een opdracht, en de toestand lees je van het scherm (REC toont hem 15 s).

Niet gecontroleerd: het opstartscherm op het echte apparaat (de simulator toont het niet), de aanslaggevoeligheid met
echte vingers, opslaan op het echte apparaat, MIDI via de TRS-ingang. De schermspiegel geeft bij bewegend beeld soms
losse streepjes die niet op het scherm zelf staan.

## USB-geluid op vol niveau (14 november 2026)

`plat_usb_audio_full(1)` in `app_init`: het geluid gaat altijd op vol niveau naar de computer, MASTER regelt alleen speaker en
koptelefoon. Gemeten op het apparaat met MASTER op 0: piek -3,9 dBFS, gemiddeld -19,2 dBFS over USB.

## Vier engines, 56 presets, eigen bronnen (14 november 2026)

Erbij gekomen: STRETCH en RESON als engine, een filter met resonantie per laag, shimmer en freeze in de galm, de bron
NOISE, 56 presets, en een bank met eigen bronnen die de Mac over USB stuurt.

Wat werkte:
- Het volume van presets niet met de hand zetten. `build/tk_host levels` speelt elk preset met een akkoord, regelt master
  tot RMS -18 dBFS en schrijft `dsp/tk_levels.h`; de app leest die tabel. 56 presets liggen zo binnen 1 dB van elkaar.
- Dezelfde `tk_presets.h` in de firmware en in de luistertest: wat op de Mac gemeten is, is wat het apparaat speelt.
- Eigen data naar het apparaat: `APP_USB_CMD` + `plat_host_read` + `plat_data_erase/write` (fm1-base). De app vraagt de
  computer om blokken van 512 bytes, zoals een update dat doet; 282 KB gaat in 9 seconden. De kop van de bank wordt als
  eerste gewist en een CRC-32 over het geheel beslist bij het opstarten of de bank meetelt: een afgebroken overdracht
  laat nooit een halve bank achter. De bank staat in het datagebied (0x97000) en overleeft een firmware-update (gemeten).
- De simulator test dit zonder apparaat: `data BESTAND 0x97000`, `reboot`, en `data_write` om een byte kapot te maken.

Getallen (FM-1, USB-audio aan, drie noten tenzij anders):
- SNARENSPEL (2 x RESON + LOOP) 46 %, KOOR VAN IJZER (3 x RESON + LOOP) 55 %, TIJDLOOS (3 x STRETCH + GRAIN) 47 %,
  NOORDERLICHT 43 %, GONGZEE 41 %. Vijf noten: TREINSTATION 68 %, AVONDLIED 71 %. Overal 0 haperingen, 0 overbelastingen.
- Afbeelding 550 KB van 581 KB, RAM 49 KB van 96, pool 250 KB van 336 (de snaren: 8 x 3 x 1024 woorden van 32 bits).

Wat misging:
- Filter met resonantie werd instabiel bij een hoge afsnijfrequentie: de demping begrenzen op 0,9 x (2/f - f/2).
- Een kamfilter en een shimmerlus bouwen gelijkspanning op tot ze vastlopen: een DC-blokker in elke lus en aan de uitgang.
- Snaren met 16-bits vertragingslijnen doven niet uit maar blijven hangen op een afrondingsrest: 32 bits (256 x de
  waarde) en alleen het verlies afronden. Een `>> 12` van een klein negatief getal geeft -1 en dus een rest die nooit
  nul wordt; delen (`/ 4096`) rondt naar nul af.
- RESON met ruis als aanslag is veel zachter dan met een toon die op de snaar past. Een lang naklinken verkleint de
  ingang nog eens. Presets met ruis-snaren hebben een hoge EXCITE en een niet te lange RING nodig.
- `turn PRESET N` van fm1_remote zet in deze app een stap per opdracht; kies presets op afstand met een programmawissel
  over MIDI. De tekst van REC komt niet in `screen`; lees hem van de schermspiegel.

Niet gecontroleerd: hoe de nieuwe klanken op de speaker van de FM-1 zelf klinken (alleen over USB opgenomen), de
FREEZE-knop met een echte vinger, en eigen opnamen van Barnier in de bank (getest met de vijf extra bronnen).

