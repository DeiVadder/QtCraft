# Musik-Merkmale: Buckley-Stücke und erzeugte Musik (Paket 88.3)

Gemessen mit `scripts/music-traits.py` (E-1155) am 2026-11-09: die 14 Stücke von Scott Buckley aus `content/qtcraft/music` (88.1) und die erzeugten Stücke vor und nach 88.3 (`qtcraft-headless --no-mods --seed <s> --export-sounds <dir>`, je Platz drei Seeds: 1, 2, 20261109). Gruppe eines Stücks ist sein erster Platz in der JSON. Die Werte sind allgemeine Merkmale; das Skript zieht weder Melodien noch Motive noch Akkordfolgen heraus, und der Komponist übernimmt keine (E-1157).

Neu messen:

```sh
python3 scripts/music-traits.py --tracks content/qtcraft/music --per-piece
build/debug/src/headless/qtcraft-headless --no-mods --seed 1 --export-sounds /tmp/qc-music
python3 scripts/music-traits.py --pieces /tmp/qc-music
```

## Was die Spalten messen

| Spalte | Methode | Grenzen |
|--------|---------|---------|
| Länge | Dauer der Datei | – |
| Tempo | Autokorrelation des Spektralflusses (ohne gleitenden Mittelwert über 1 s), 40–180 bpm, zu 90 bpm gewichtet | Die Buckley-Stücke haben fast nie einen festen Puls (Sicherheit 0,05–0,18, nur „Intervention“ 0,62); die Zahl ist dann der Abstand der Anschläge, oft das Doppelte eines langsamen Pulses. Nicht als Ziel benutzt. |
| Modus | Chroma 55–2000 Hz, Korrelation mit den Krumhansl-Profilen | Natürliches Moll und paralleles Dur teilen die Töne; bei schwacher Tonart (Stärke < 0,6) unsicher. |
| Akkordwechsel/min | je Sekunde der ähnlichste Dur- oder Moll-Dreiklang, Mehrheit über 5 s, Wechsel je Minute | Flächen und Arpeggien machen Rauschen; vergleichbar nur mit sich selbst. |
| Tonklassen | Tonklassen über halber Stärke je Sekunde | grobe Dichte des Satzes |
| Einsätze/s | Spitzen des Spektralflusses über Mittel + 2 Standardabweichungen, mindestens 140 ms auseinander | relativ zum Stück: ein sehr ruhiges Stück zählt kleine Ereignisse mit |
| Register | Energie-gewichtete Perzentile 10/50/90 % der MIDI-Tonhöhe, 30–4000 Hz | Obertöne heben die Werte |
| Schwerpunkt, über 1 kHz | spektraler Schwerpunkt (energiegewichtet), Energieanteil über 1 kHz | Helligkeit |
| Nachhall | steilste 5 % der Abfälle (dB/s über 0,4 s, 300–4000 Hz), umgerechnet auf 60 dB | misst den freien Abfall des ganzen Satzes, nicht nur den Raum: durchgehende Flächen ohne Pausen messen länger als ihr Hall (die trockenen Stücke vor 88.3 messen 2,5 s ohne jeden Hall) |
| lautestes Zehntel, Bogen | mittlerer Pegel je Zehntel des Stücks relativ zum lautesten | Dynamikbogen |
| Dynamik | 95. minus 10. Perzentil der Sekundenpegel | |
| Ein-/Ausblenden | Sekunden bis zum ersten bzw. nach dem letzten Pegel über Median − 6 dB | |

## Buckley-Stücke (Ziel)

| Platz | n | Länge s | Modus | Akkordwechsel/min | Tonklassen | Einsätze/s | Register 10/50/90 | Schwerpunkt Hz | über 1 kHz % | Nachhall s | lautestes Zehntel | Dynamik dB | Ein-/Ausblenden s | Bogen (dB je Zehntel) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| menu | 2 | 405 (392–418) | Dur | 8,8 (8,5–9,2) | 2,5 | 0,89 (0,79–0,98) | 41/58/69 | 277 | 1,3 | 3,3 (3,2–3,4) | 0,70 | 14,3 | 18/27 | -10 -7 -5 -4 -2 0 0 -2 -3 -10 |
| day | 3 | 248 (200–313) | Dur | 9,7 (6,8–15,5) | 2,6 | 0,74 (0,50–1,09) | 40/57/71 | 281 | 2,1 | 2,3 (1,5–2,9) | 0,55 | 13,6 | 3/13 | -6 -4 -3 -2 -2 -2 -1 -3 -4 -10 |
| night | 2 | 349 (254–444) | Moll | 10,2 (9,6–10,9) | 2,5 | 0,74 (0,60–0,89) | 37/58/78 | 336 | 3,2 | 3,3 (2,7–3,9) | 0,40 | 14,4 | 7/12 | -8 -2 -2 -1 -2 -1 -6 -3 -6 -11 |
| cave | 2 | 352 (258–445) | Dur/Moll | 9,4 (8,7–10,2) | 3,0 | 0,81 (0,59–1,03) | 40/58/70 | 298 | 3,5 | 3,3 (2,6–4,1) | 0,80 | 14,9 | 21/19 | -11 -6 -3 -4 -5 -4 -3 -1 -1 -9 |
| nether_wastes | 1 | 590 | Moll | 12,8 | 3,6 | 0,63 | 37/52/81 | 406 | 8,3 | 4,3 | 0,15 | 10,7 | 5/7 | -2 0 -2 -5 -6 -4 -7 -7 -2 -7 |
| soul_sand_valley | 1 | 408 | Dur | 6,2 | 3,0 | 0,21 | 43/60/79 | 350 | 4,9 | 4,4 | 0,35 | 13,4 | 26/19 | -10 -4 -3 0 -2 -4 -5 -7 -6 -5 |
| crimson_forest | 1 | 338 | Moll | 9,7 | 3,1 | 0,71 | 42/66/79 | 418 | 6,0 | 4,6 | 0,65 | 14,5 | 16/11 | -6 -1 -2 -1 0 0 0 0 0 -4 |
| warped_forest | 1 | 302 | Dur | 8,2 | 2,9 | 0,60 | 26/57/74 | 243 | 2,3 | 4,0 | 0,95 | 14,4 | 14/4 | -15 -12 -7 -6 -5 -7 -9 -10 -5 0 |
| basalt_deltas | 1 | 291 | Dur | 3,8 | 3,2 | 0,94 | 29/42/70 | 199 | 2,1 | 3,5 | 0,75 | 7,7 | 1/7 | -7 -6 -6 -6 -6 -5 -4 0 0 -3 |

Gemeinsam: lang (4–10 min), ohne festen Puls, Dur am Tag und im Menü, Moll in der Nacht, ein Akkord hält rund 4–10 s (im Nether 5–16 s), wenige Anschläge (0,2–1 je Sekunde), tiefes bis mittleres Register (Median um MIDI 58, also um A#3), dunkel (Schwerpunkt 200–420 Hz, kaum Energie über 1 kHz), langer Abfall (2,3–4,6 s), ein langsamer Bogen mit 8–15 dB Dynamik und weichem Ein- und Ausblenden.

## Erzeugt vor 88.3 (Komponist aus 84.9)

Menü, Tag, Nacht und Höhle sind dasselbe Stück (Standardstil), deshalb eine Zeile.

| Platz | Länge s | Modus | Akkordwechsel/min | Tonklassen | Einsätze/s | Register 10/50/90 | Schwerpunkt Hz | über 1 kHz % | Nachhall s | lautestes Zehntel | Dynamik dB | Ein-/Ausblenden s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| menu, day, night, cave | 30 (29–31) | Dur/Moll | 14,7 | 1,7 | 0,87 | 46/53/82 | 351 | 4,4 | 2,7 (trocken) | 0,52 | 5,9 | 0/1 |
| nether_wastes | 39 | Dur/Moll | 14,6 | 2,4 | 0,68 | 40/47/75 | 230 | 0,1 | 2,5 | 0,25 | 6,8 | 0/1 |
| soul_sand_valley | 45 | Dur/Moll | 13,4 | 2,3 | 0,58 | 37/45/73 | 220 | 0,0 | 2,7 | 0,42 | 6,8 | 0/1 |
| crimson_forest | 34 | Dur/Moll | 15,6 | 2,0 | 0,76 | 42/49/76 | 231 | 2,8 | 2,6 | 0,62 | 7,9 | 0/1 |
| warped_forest | 37 | Dur | 8,9 | 2,1 | 0,71 | 43/51/79 | 326 | 3,1 | 2,5 | 0,45 | 6,5 | 0/1 |
| basalt_deltas | 41 | Moll | 18,0 | 2,1 | 0,63 | 36/47/73 | 257 | 0,7 | 2,5 | 0,38 | 6,4 | 0/1 |

Abstand zum Ziel: ein Zehntel der Länge, kein Bogen (fast gleich laut von Anfang bis Ende, 6–8 dB), kein Ein- und Ausblenden, Akkord- und Basswechsel jeden Takt, hohe Melodie (90 % bei MIDI 73–82), kein Hall, im Overworld keine Unterschiede zwischen Tag, Nacht, Höhle und Menü.

## Erzeugt nach 88.3

| Platz | Länge s | Tonleiter (Stil) | Akkordwechsel/min | Tonklassen | Einsätze/s | Register 10/50/90 | Schwerpunkt Hz | über 1 kHz % | Nachhall s | lautestes Zehntel | Dynamik dB | Ein-/Ausblenden s | Bogen (dB je Zehntel) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| menu | 283 (269–299) | Dur | 7,9 | 2,1 | 0,72 | 46/60/74 | 311 | 0,9 | 5,8 | 0,55 | 11,4 | 18/13 | -10 -4 -3 -2 -1 -1 0 -1 -2 -8 |
| day | 237 (197–269) | Dur | 9,0 | 2,0 | 0,70 | 46/59/74 | 307 | 1,1 | 6,0 | 0,55 | 8,0 | 7/7 | -6 -2 -2 -1 -1 -1 0 -2 -2 -6 |
| night | 272 (259–278) | Moll | 8,0 | 2,1 | 0,72 | 43/62/74 | 335 | 0,6 | 5,5 | 0,45 | 8,8 | 8/7 | -6 -2 -2 -1 0 -1 -2 -3 -3 -6 |
| cave | 269 (243–296) | je Stück | 8,3 | 2,3 | 0,53 | 43/56/70 | 254 | 0,6 | 6,1 | 0,75 | 12,3 | 25/7 | -13 -4 -3 -3 -2 -1 -1 0 -2 -7 |
| nether_wastes | 286 (283–290) | Moll | 14,8 | 2,6 | 0,45 | 40/50/72 | 261 | 0,8 | 6,4 | 0,18 | 8,0 | 5/7 | -3 0 -1 -1 -2 -2 -3 -4 -5 -7 |
| soul_sand_valley | 267 (249–278) | Dur | 6,1 | 2,0 | 0,78 | 34/45/66 | 171 | 0,5 | 10,0 | 0,48 | 9,3 | 4/9 | -7 -2 -1 -1 -1 0 -2 -2 -4 -7 |
| crimson_forest | 285 (274–299) | Moll | 8,5 | 2,4 | 0,53 | 42/58/74 | 315 | 2,5 | 6,0 | 0,58 | 10,7 | 17/7 | -10 -4 -2 -2 0 0 0 -1 -2 -6 |
| warped_forest | 276 (264–292) | Dur | 7,3 | 2,1 | 0,45 | 39/54/74 | 292 | 0,5 | 6,4 | 0,62 | 7,8 | 8/5 | -5 -3 -3 -2 -2 -3 -1 -2 -1 -2 |
| basalt_deltas | 246 (221–288) | Dur | 6,6 | 2,4 | 0,65 | 36/46/71 | 236 | 0,8 | 4,9 | 0,72 | 9,9 | 4/5 | -8 -5 -4 -3 -3 -1 -1 0 -3 -5 |
| nether (Dimension, für Biome ohne eigene Anordnung) | 279 | je Stück | 9,5 | 2,0 | 0,57 | 46/59/78 | 379 | 2,5 | 5,8 | 0,52 | 8,7 | 13/7 | -7 -3 -3 -2 -1 -1 -1 -2 -3 -6 |

Die Spalte nennt die Tonleiter des Stils; die Messung hält natürliches Moll oft für das parallele Dur (Nacht, Ödland, Basaltdeltas: „Dur/Moll“).

**Getroffen:** Akkordwechsel je Minute (Tag 9,0 gegen 9,7; Ödland 14,8 gegen 12,8), Register (Median 54–62 gegen 52–66), Schwerpunkt und Helligkeit (dunkel, 170–380 Hz), Lage des lautesten Zehntels je Platz (Nacht früh, Höhle spät, Ödland am Anfang), Ein- und Ausblenden, Dynamik 8–12 dB (Ziel 8–15), Einsätze je Sekunde im Overworld 0,5–0,7 (Ziel 0,7–0,9), Dur und Moll je Platz.

**Bewusst abweichend:** Länge höchstens 300 s (Ziel 250–590 s): ein erzeugtes Stück liegt ganz im Speicher (rund 58 MB bei 48 kHz), die Stücke aus Dateien werden gestreamt (E-1159). Der gemessene Nachhall ist länger (5–10 s gegen 2–5 s), obwohl der Hall selbst die Zeit des Ziels hat (`test_sound_synthesis::reverbDecaysInItsTime`): die Flächen liegen ohne Pausen übereinander, freie Abfälle gibt es kaum. Das Seelensandtal misst mehr Einsätze als sein Vorbild (0,78 gegen 0,21): das Maß ist relativ zum Stück und zählt in sehr ruhigen Stücken kleine Ereignisse; es hat die wenigsten Noten aller Plätze. Weniger Tonklassen (2,0–2,6 gegen 2,5–3,6): Dreiklang mit None, keine dichteren Klänge. Ein Stück endet nach ganzen Zyklen von vier Akkorden; in den Basaltdeltas (vier Takte je Akkord) dauert ein Zyklus rund eine Minute, die Stücke sind dort 220–290 s lang.

**Seelensandtal, Einblenden und Register (Review 88.3):** Die Zeile ist mit dem Stand nach E-1158 gemessen (Dur, Grundton 40–44). Das Einblenden misst 4 s, obwohl die Anordnung 25 s sagt: der Bordun des Bioms (`drone`) folgt dem Dynamikbogen nicht und ist nach 4 s Anschwellen voll da, und das Maß zählt ab Median − 6 dB; die übrigen Stimmen beginnen bei 30 % ihres Pegels. Der Median des Registers bleibt bei 45, weil Bordun (eine Oktave unter dem Grundton) und Bass die Energie tragen und die Melodie nur in 15 % der Phrasen spielt; der höhere Grundton hob vor allem die Flächen. Die Review-Fixes (Rendern in die volle Länge, eine Normierung) ändern keine Note: nach ihnen neu gemessen (Seeds 1, 2, 20261109), gibt jede Spalte der Tabelle „Erzeugt nach 88.3“ denselben Wert.
