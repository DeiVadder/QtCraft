# Performance-Verlauf

Messung pro Etappe mit `scripts/bench.sh` (Release-Build `RelWithDebInfo`, Sichtweite 8 Chunks, Seed 42, je Szenario 3 Läufe à 20 s nach 3 s Aufwärmen, **Median** der Läufe). Die Läufe laufen allein, nie parallel zu Builds oder Tests.

Szenarien:

- **stand:** Stehen am Spawn.
- **sprint:** Sprinten und Springen geradeaus, laufendes Chunk-Streaming.
- **crowd:** Stehen zwischen 56 gerufenen Mobs (Schweine, Zombies mit Humanoid-Skelett, Schleime) und 24 gedroppten Items.

Werte: durchschnittliche und 99.-Perzentil-Frame-Zeit, durchschnittliche und schlechteste Server-Tick-Zeit, Spitzenspeicher (resident), Zahl der Entities beim Client am Ende.

Hinweis: Das Spiel rendert mit VSync; auf dem Messrechner (120-Hz-Display) liegt die mittlere Frame-Zeit daher bei etwa 8,3 ms, solange ein Frame in das Budget passt. Eine Verschlechterung zeigt sich zuerst in der 99.-Perzentil-Frame-Zeit, der Tick-Zeit und dem Speicher. Einzelne Ausreißer der schlechtesten Tick-Zeit (Chunk-Generierung, Speichern) schwanken stark; als Regression zählt dort erst ein wiederholbarer Anstieg.

Schwelle: Verschlechtert sich ein Wert um mehr als 10 % gegenüber der letzten Messung (oder fällt das Ziel von 60 FPS bei 8 Chunks), wird die Ursache per Profiling eingegrenzt und behoben oder begründet (`docs/Entscheidungen.md`).

| Etappe | Szenario | Frame Ø ms | Frame p99 ms | Tick Ø ms | Tick max ms | Speicher MB | Entities | Differenz zur Vorgängerin | Bemerkung |
|--------|----------|-----------:|-------------:|----------:|------------:|------------:|---------:|---------------------------|-----------|
| 10 (Baseline, `dev` f641052) | stand | 8,35 | 8,98 | 0,18 | 1,48 | 275 | 14 | – | Baseline vor Etappe 11 |
| 10 (Baseline, `dev` f641052) | sprint | 8,35 | 10,35 | 0,17 | 1,51 | 344 | 8 | – | |
| 10 (Baseline, `dev` f641052) | crowd | 8,39 | 10,06 | 0,26 | 1,10 | 279 | 80 | – | |
| 11 (`feature/11-furnace` 46912c7), Lauf 1 | stand | 8,36 | 9,39 | 0,14 | 0,41 | 290 | 14 | Speicher +5 % | |
| 11, Lauf 1 | sprint | 8,39 | 9,92 | 0,19 | 2,52 | 359 | 8 | Tick Ø +12 %, Speicher +4 % | Tick Ø über der Schwelle, daher A/B-Messung (unten) |
| 11, Lauf 1 | crowd | 8,40 | 10,24 | 0,26 | 2,06 | 295 | 80 | Speicher +6 % | |
| 11, Lauf 2 | stand | 8,39 | 9,40 | 0,19 | 2,14 | 288 | 14 | | |
| 11, Lauf 2 | sprint | 8,36 | 9,34 | 0,27 | 2,53 | 361 | 8 | | |
| 11, Lauf 2 | crowd | 8,36 | 9,31 | 0,38 | 2,35 | 294 | 80 | | |
| 10 (Baseline 89b1251, Wiederholung direkt danach) | stand | 8,36 | 9,21 | 0,14 | 0,57 | 275 | 14 | | A/B-Gegenmessung |
| 10 (Wiederholung) | sprint | 8,36 | 9,35 | 0,23 | 1,85 | 346 | 8 | | |
| 10 (Wiederholung) | crowd | 8,36 | 9,31 | 0,48 | 2,83 | 280 | 80 | | |

**Bewertung Etappe 11:** Die Server-Tick-Zeiten schwanken zwischen Messreihen derselben Version um bis zu 80 % (Baseline: Crowd-Tick Ø 0,26 ms, in der Wiederholung 0,48 ms), weil sie im Bereich von Zehntel-Millisekunden liegen und vom Takt des Rechners abhängen. Die Etappe-11-Werte liegen innerhalb der Spannweite der Baseline; die Frame-Zeiten (Ø und p99) sind unverändert. Wiederholbar ist nur der höhere Spitzenspeicher (+13 bis +15 MB, rund 5 %, neue Texturen, Maschinen- und Rezepttabellen), unter der Schwelle von 10 %. Kein tiefes Profiling nötig. Folgerung für künftige Messungen: Tick-Zeiten nur im A/B-Vergleich (Vorgänger-Stand direkt davor oder danach messen) bewerten.
| 11 (`dev` 412cd7c, A/B-Basis) | stand | 8,37 | 9,32 | 0,23 | 0,74 | 291 | 14 | – | gleiche Messreihe wie Etappe 12 |
| 11 (A/B-Basis) | sprint | 8,35 | 9,44 | 0,28 | 2,00 | 359 | 8 | – | |
| 11 (A/B-Basis) | crowd | 8,37 | 10,36 | 0,40 | 3,04 | 295 | 80 | – | |
| 12 (`feature/12-sound`, Wiederholung) | stand | 8,34 | 10,26 | 0,18 | 0,76 | 311 | 14 | p99 +10 %, Speicher +7 % | erster Lauf direkt nach dem Build: p99 15,87 (zwei Läufe mit Hängern), nicht reproduzierbar |
| 12 (Wiederholung) | sprint | 8,35 | 10,38 | 0,26 | 4,18 | 382 | 8 | p99 +10 %, Speicher +6 % | |
| 12 (Wiederholung) | crowd | 8,33 | 10,32 | 0,40 | 1,60 | 311 | 80 | Speicher +5 % | |

**Bewertung Etappe 12:** p99 lag in der Wiederholung rund 1 ms (10 %) über der Basis. Eingrenzung: (1) Mit `--no-audio` bleibt p99 bei 10,2–10,4 ms, der Mischer ist also nicht die Ursache. (2) Ein `sample`-Profil eines Stand-Laufs zeigt den Hauptthread zu über 95 % im Warten und keinen Etappe-12-Code im Frame-Pfad. (3) Eine verschränkte A/B-Messreihe (Basis und Etappe 12 abwechselnd, je 4 Läufe stehen und sprinten) ergibt für beide Stände dieselben Werte: p99 pendelt bei beiden zwischen etwa 9,3 und 10,3 ms, abhängig vom Zeitpunkt der Messung, nicht vom Stand. Die Hänger im ersten Lauf traten in 13 weiteren Läufen nicht auf (vermutlich Systemlast direkt nach dem Build). Damit keine Regression der Frame-Zeiten. Wiederholbar ist nur der Spitzenspeicher (+16 bis +23 MB, 5–7 %, Klangbank und Musikpuffer), unter der Schwelle. Folgerung für künftige Messungen: p99 ebenfalls nur verschränkt (A/B abwechselnd) bewerten; `bench.sh` sollte nicht direkt nach einem Build laufen.
