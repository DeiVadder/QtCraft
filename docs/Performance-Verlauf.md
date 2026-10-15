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

**Etappe 13 (vorläufig):** `scripts/bench.sh` hat das neue Szenario `light` (stehen auf einer freigeräumten Steinfläche bei Nacht, viermal pro Sekunde erscheint oder verschwindet eine Fackel auf einem Ring von 9 m: Blocklicht-Ausbreitung und -Entfernung über Sektions- und Chunkgrenzen, Neuvernetzung im Hintergrund). Einzelne Vorab-Läufe (Release, 10 s, kein Median, nicht verschränkt): `stand` Frame Ø 8,33 ms, p99 10,39 ms, Tick Ø 0,23 ms; `light` Frame Ø 8,33 ms, p99 10,49 ms, Tick Ø 0,16 ms, schlechtester Tick 1,3 ms. Ein früherer Stand berechnete das Licht von Wesen und Hand exakt im GUI-Thread und kam im Debug-Build auf p99 34,7 ms; seitdem schätzt der Client es aus den nahen Lichtquellen (siehe E-049). Die verschränkte A/B-Messung (je 3 Runden à 20 s, Ton stumm, aber gemischt, Mittelwerte) steht unten.

| Etappe | Szenario | Frame Ø ms | Frame p99 ms | Tick Ø ms | Tick max ms | Speicher MB | Entities | Differenz zur Vorgängerin | Bemerkung |
|--------|----------|-----------:|-------------:|----------:|------------:|------------:|---------:|---------------------------|-----------|
| 13 vor den Korrekturen (4c1b9c5), verschränkt | stand | – | – | 0,27 (Basis 0,20) | – | 360 (Basis 307) | 14 | Tick Ø +35 %, Speicher +17 % | Mittel aus 3 Runden, Basis `dev` a361a5b |
| 13 vor den Korrekturen | sprint | – | – | 0,30 (Basis 0,23) | 5,5 (Basis 2,3) | 437 (Basis 381) | 8 | Tick Ø +30 %, Speicher +15 % | Frame-Zeiten unverändert |
| 13 vor den Korrekturen | crowd | – | – | 0,53 (Basis 0,36) | – | 365 (Basis 313) | 80 | Tick Ø +45 %, Speicher +17 % | |
| 12 (`dev` a361a5b, A/B-Basis) | stand | 8,36 | 9,17 | 0,099 | 0,37 | 310 | 14 | – | gleiche Messreihe wie Etappe 13, Mittel aus 3 Runden |
| 12 (A/B-Basis) | sprint | 8,36 | 9,14 | 0,117 | 1,75 | 378 | 8 | – | |
| 12 (A/B-Basis) | crowd | 8,36 | 9,27 | 0,205 | 0,89 | 312 | 80 | – | |
| 13 (`feature/13-parts-light` c2c2cb2) | stand | 8,36 | 9,20 | 0,111 | 0,54 | 307 | 14 | Tick Ø +0,012 ms, Speicher −1 % | Tick in der Streuung (Basis 0,093–0,111, Etappe 13 0,103–0,120) |
| 13 | sprint | 8,36 | 9,52 | 0,115 | 1,27 | 385 | 8 | p99 +4 %, Speicher +2 % | p99: ein Lauf 10,3 ms, die anderen 9,0 und 9,3 |
| 13 | crowd | 8,36 | 9,36 | 0,189 | 0,73 | 311 | 80 | Speicher ±0 | |

**Bewertung Etappe 13:** Die erste verschränkte Messreihe (Basis und Etappe 13 abwechselnd, je 3 Runden) zeigte wiederholbar +53 bis +56 MB Spitzenspeicher (+15 bis +17 %) und +29 bis +45 % Server-Tick. Eingrenzung: (1) `vmmap`/`heap` und `malloc_history` (MallocStackLogging) im Stand-Szenario: +42 MB im Heap, davon +20 MB und +91 000 Allokationen unter dem Zurücksetzen des Inventarmodells (QML-Delegates) und +18 MB Vertexpuffer. Ursache des ersten Postens: Jeder `Repeater` der Bildschirme legte für jede Zeile des Inventarmodells ein `ItemSlot` an und blendete die unpassenden aus; mit dem Truhenbildschirm (zwei weitere Repeater) und 54 statt 3 Blockfeldern stieg das von 8 × 49 auf 10 × 100 Delegates (`QQuickImagePrivate` 412 → 1020). (2) `sample` des Server-Threads im Crowd-Szenario: 178 statt 125 Tick-Proben, fast alles in `QcPhysics::moveBox`: `collisionBoxesAt` legte pro Zelle der Umgebung (rund 50–80 Zellen pro Wesen und Tick) eine eigene `QList` an. Die Lichtprüfung beim Spawnen fiel nicht ins Gewicht (Paletten-Vorprüfung, keine Lichtquellen in der Messwelt). Korrekturen: (a) Kollisionsboxen werden direkt in ein Feld auf dem Stapel gesammelt (`QVarLengthArray`), ohne Liste pro Zelle: Crowd-Tick-Proben 125 (Basis) / 178 (vorher) → 87, Tick Ø im Einzellauf 0,62 → 0,33 ms. (b) `SlotRange` (`QcSlotRange`) reicht jedem Repeater nur seine Zeilen des Inventarmodells durch (Hotbar 9, Raster 9, Ergebnis 1, Ofen je 1, Truhe 27, Inventar 36): 157 statt 1000 Delegates, `QQuickImagePrivate` 1020 → 150, Spitzenspeicher im Stand-Lauf 363 → 309 MB. Ergebnis der zweiten verschränkten Messreihe (oben): Speicher und Tick wie die Basis, Frame-Zeiten unverändert. Bleibend und gewollt ist der um 4 Byte größere Block-Vertex (Blocklicht als eigener Kanal, 28 → 32 Byte, rund +18 MB Vertexpuffer bei 8 Chunks), siehe E-051; er fällt nur im Sprint-Szenario (+7 MB, +2 %) auf, weil die eingesparten Delegates ihn sonst ausgleichen.

**Etappe 13, inkrementelles Blocklicht (nach der unabhängigen Verifikation):** Das Blocklicht wird nicht mehr in jedem Mesh-Job für die 3 × 3-Chunk-Nachbarschaft neu geflutet, sondern von `QcBlockLightEngine` auf einem eigenen Thread inkrementell nachgeführt (E-049). Der Benchmark meldet dafür zusätzlich `change_mesh_avg_ms`/`change_mesh_max_ms` (Zeit von einer Blockänderung bis zum neuen Mesh jeder Sektion, die sie betrifft), `change_sections` (so gemessene Sektionen), `mesh_backlog_max` (höchste Zahl wartender Sektionen und Licht-Jobs) und `light_job_avg_ms`/`light_job_max_ms`. Verschränkte Messung im Szenario `light` (je 3 Runden à 20 s, Release), Basis ist der Stand dieses Branches direkt vor der Umstellung (79444e1, mit derselben Latenz- und Rückstau-Messung nachgerüstet; `dev` hat kein Szenario `light`):

| Stand | Lauf | Frame Ø ms | Frame p99 ms | Tick Ø ms | Speicher MB | Änderung bis Bild Ø ms | max ms | Sektionen | Rückstau max | Licht-Job Ø / max ms |
|-------|-----:|-----------:|-------------:|----------:|------------:|-----------------------:|-------:|----------:|-------------:|---------------------:|
| vorher (79444e1) | 1 | 8,35 | 10,36 | 0,13 | 317 | 13,5 | 41,9 | 4374 | 54 | – |
| inkrementell (d6f553e) | 1 | 8,34 | 10,37 | 0,16 | 315 | 8,5 | 25,0 | 674 | 10 | 0,24 / 0,51 |
| vorher | 2 | 8,33 | 10,09 | 0,18 | 323 | 18,6 | 50,0 | 4374 | 54 | – |
| inkrementell | 2 | 8,33 | 9,68 | 0,15 | 311 | 14,3 | 25,0 | 674 | 10 | 0,22 / 0,74 |
| vorher | 3 | 8,36 | 10,41 | 0,15 | 320 | 14,6 | 44,4 | 4374 | 54 | – |
| inkrementell | 3 | 8,34 | 9,85 | 0,14 | 311 | 14,8 | 25,3 | 674 | 10 | 0,21 / 0,55 |

**Bewertung:** Pro Lauf werden 85 % weniger Sektionen neu vernetzt (674 statt 4374; eine Fackel ändert das Himmelslicht nicht, also entfällt die Säule, und nur Sektionen mit geändertem Blocklicht werden markiert), der Rückstau sinkt von 54 auf 10, die schlechteste Zeit bis zum Bild von 42–50 auf 25 ms (rund drei Frames; der Mittelwert hängt am Frame-Takt, an dem die Ergebnisse abgeholt werden). Ein Licht-Job kostet im Mittel 0,2 ms auf dem Licht-Thread. Frame-Zeiten (p99 9,7–10,4 gegen 10,1–10,4 ms) und Speicher (−2 %) ohne Verschlechterung. Profil (`sample`, 10 s eines 30-s-Laufs `light`): Hauptthread in 6643 Proben zu 95 % wartend, `QcMeshScheduler::pump` 3 Proben, kein Lichtcode im GUI-Thread; Licht-Thread 7 Proben (0,1 %), Mesh-Worker je unter 0,5 %. Der einzige lange Frame des profilierten Laufs (643 ms) ist das Anhalten des Prozesses durch `sample` selbst; in den sechs Läufen ohne Profiler gab es keinen Frame über 100 ms.

**Etappe 13 nach den Verifikationsbefunden, verschränkt gegen `dev` (a361a5b):** Stand 9f02fd9 (inkrementelles Blocklicht, Stufen hochlaufen, priorisierte Update-Warteschlangen, Formbudget), Release, je Runde 20 s, Basis und Etappe 13 abwechselnd; stand 10 Runden, crowd 6, sprint 3 (Mittelwerte).

| Szenario | Frame p99 ms (Basis → 13) | Tick Ø ms (Basis → 13) | Speicher MB (Basis → 13) | Bemerkung |
|----------|---------------------------|------------------------|--------------------------|-----------|
| stand | 9,59 → 9,45 | 0,193 → 0,206 | 308 → 305 | ohne den Lauf mit Hänger (s. u.); Tick in der Streuung (Basis 0,16–0,24, Etappe 13 0,15–0,26) |
| sprint | 10,36 → 10,34 | 0,281 → 0,224 | 378 → 380 | |
| crowd | 9,79 → 9,69 | 0,325 → 0,350 | 312 → 311 | Tick +8 % unter der Schwelle; Stufen-Versuch nur bei blockierten Wesen am Boden (drei zusätzliche `moveBox`) |

**Bewertung:** Keine Regression über der Schwelle; Frame-Zeiten und Speicher wie die Basis. Ein einzelner Stand-Lauf der Etappe 13 hatte einen Frame von 14,4 s bei unauffälligem Server-Tick (GUI- und Render-Thread standen, der Server lief weiter); in 4 sofort wiederholten Runden und in den übrigen 25 Läufen trat nichts Vergleichbares auf (kein Frame über 100 ms). Im Szenario `stand` gibt es keine Blockänderungen und keine Licht-Jobs während der Messung, und der GUI-Thread wartet nie auf den Licht- oder Mesh-Thread; daher wird ein Systemhänger angenommen (wie die einmaligen Hänger in Etappe 12). Sollte er wieder auftreten, ist er mit `sample` des hängenden Prozesses einzugrenzen.

**Etappe 14 (`feature/14-liquids-details`):** Neues Szenario `liquids` in `scripts/bench.sh` (`SCENARIOS="liquids"` misst nur dieses): Der Spieler steht auf einer geräumten Steinfläche (37 × 37), zweimal pro Sekunde erscheint 4 m über ihr auf einem Ring (8 m) eine Wasser- oder Lavaquelle oder verschwindet wieder; die Fläche wird also dauernd geflutet und läuft leer (Flüssigkeits-Schritte im Server-Tick, Neuvernetzung der Flüssigkeitsoberflächen, Blocklicht der Lava). Ein kurzer Probelauf im Debug-Build (8 s, kein Messwert für den Verlauf) lief durch: Frame Ø 8,3 ms, p99 10,1 ms, Tick Ø 0,69 ms, schlechtester Tick 2,7 ms, 1482 neu vernetzte Sektionen, Rückstau höchstens 94. Die verschränkte Messung gegen `dev` steht unten.

**Etappe 14, erste verschränkte Messung (d66f074 gegen `dev` f6b3baf, je 3 Runden à 20 s, Release, Ton stumm, Mittel [Spanne]):** crowd Tick Ø 0,349 → 0,399 ms (+14 %), schlechtester Tick 1,06 → 2,17 ms; sprint Tick Ø 0,240 → 0,277 ms (+15 %, Spannen ohne Überlappung); stand p99 9,43 → 10,33 ms (+9,5 %, alle drei Läufe der Etappe 14 bei 10,31–10,34 ms); Spitzenspeicher +13 bis +15 MB (+4 bis +5 %). Damit lagen Tick-Werte über der Schwelle.

Eingrenzung: (1) Zeitmessung pro Phase des Server-Ticks (vorübergehend eingebaute Zähler, Mittel und Maximum pro Phase, Basis, Etappe 14 vor und nach den Korrekturen abwechselnd): neu und nennenswert war nur `applyContactDamage` (Lava-Schaden) mit 14–15 µs pro Tick im Szenario crowd und 5–7 µs in stand/sprint – je Wesen, Item und Spieler eine Hash-Suche nach dem Chunk und das Lesen der Paletten, also vor allem Cache-Fehlzugriffe. Der Flüssigkeits-Terminplan und die fallenden Blöcke kosteten im Leerlauf je 1,2–1,6 µs. (2) Im Szenario sprint war `streamChunks` der Unterschied (Basis 88 µs, Etappe 14 101–121 µs pro Tick): Etappe 14 läuft im Szenario weiter (Ende bei 78/−79 statt 75/−19, rund 111 statt 77 m, weil der Spieler nach E-061 im Sprung keine Stufe mehr steigt und an anderer Stelle hängen bleibt) und schickt dadurch rund 25 % mehr Chunks; das Kodieren eines Chunks kostete 47–60 µs, fast alles in `QDataStream`-Aufrufen pro Lauf-Wert. (3) `sample` des Server-Threads (55 s, crowd) zeigt keine neue heiße Funktion; `QcMobManager::tick` und `think` sind gleich teuer wie in der Basis. (4) Stand-p99: `sample` von Haupt- und Render-Thread zeigt keinen neuen Etappe-14-Code pro Frame; gezählt werden gleich viele sichtbare Sektionen (272, gleiche Zahl an Draw-Calls), aber 12 % mehr Dreiecks-Indizes (726 000 statt 650 000, die neue Vegetation als Kreuzformen), also mehr GPU-Arbeit, nicht mehr CPU-Arbeit.

Korrekturen (je Commit, Werte aus der Phasenmessung, Etappe 14 vorher → nachher): Kontaktschaden nur noch dort, wo ein Chunk-Abschnitt laut einer Bitmaske überhaupt Blöcke mit Kontaktschaden enthält, ohne solche Abschnitte gar nicht (crowd 14,3–15,1 → 0,6 µs, stand 5,1 → 0,6 µs, sprint 5,7–7,3 → 0,5 µs pro Tick); dabei behoben, dass ein in Lava sterbendes Wesen aus der Liste fiel, während sie durchlaufen wurde. Flüssigkeiten und fallende Blöcke kehren ohne fällige Arbeit sofort zurück (1,2–1,6 → 0,2–0,5 µs). Die Randprüfung stiller Quellen beim Laden eines Chunks entscheidet einmal pro Zustandspaar statt pro Randzelle (`handleLoadedChunks` im Sprint 3,4–3,7 → 2,3–2,4 µs, Basis 2,0–2,4). Die Läufe einer Sektion werden als ein Block Rohdaten geschrieben und gelesen, mit unveränderten Bytes (Kodieren eines Chunks 47–60 → 22–26 µs; das hebt die zusätzlichen Chunks des Sprint-Szenarios auf). Im Render-Thread nimmt das Sortieren der durchscheinenden Ebene nur noch Sektionen mit durchscheinenden Flächen. Neuvernetzung bei Flüssigkeiten: Eine Änderung der Lichtdurchlässigkeit (Luft ↔ Wasser) markierte alle Sektionen der 3 × 3 Chunks vom Boden bis eine Sektion über der Änderung; jetzt nur die Sektionen, deren Lichtvolumen (16 Blöcke darüber und darunter, wie im Mesher) die geänderte Zelle oder die Zellen zwischen altem und neuem höchstem lichtdämpfendem Block der Säule enthält (E-062); ein Test vergleicht nach vielen Änderungen jede Sektion mit einer frisch vernetzten.

**Etappe 14 nach den Korrekturen, verschränkt gegen `dev` (f6b3baf):** Stand ba90146, Release, je Runde 20 s, Basis und Etappe 14 abwechselnd, Ton stumm, Mittel [Spanne]; crowd zweimal je 3 Runden, light 3 Runden gegen dieselbe Basis, liquids 3 Runden gegen Etappe 14 vor den Korrekturen (d66f074; `dev` hat das Szenario nicht).

| Szenario | Frame Ø ms (Basis → 14) | Frame p99 ms (Basis → 14) | Tick Ø ms (Basis → 14) | Tick max ms (Basis → 14) | Speicher MB (Basis → 14) | Bemerkung |
|----------|-------------------------|---------------------------|------------------------|--------------------------|--------------------------|-----------|
| stand | 8,33 → 8,34 | 10,28 → 10,29 | 0,262 [0,254–0,268] → 0,273 [0,266–0,277] | 1,19 → 0,77 | 301 → 314 | Tick +4 %, Speicher +4 % |
| sprint | 8,34 → 8,34 | 10,29 → 10,27 | 0,294 [0,277–0,313] → 0,286 [0,270–0,311] | 3,58 → 3,26 | 379 → 382 | Tick −3 % trotz rund 25 % mehr Chunks |
| crowd (Reihe 1) | 8,34 → 8,34 | 10,24 → 10,24 | 0,436 [0,423–0,455] → 0,476 [0,435–0,521] | 1,37 → 2,33 | 306 → 319 | Spannen überlappen |
| crowd (Reihe 2) | 8,34 → 8,34 | 10,27 → 10,25 | 0,457 [0,434–0,482] → 0,452 [0,433–0,468] | 1,49 → 1,29 | 305 → 318 | beide Reihen zusammen +4 % |
| light | 8,34 → 8,33 | 10,32 → 10,32 | 0,215 [0,210–0,225] → 0,225 [0,221–0,230] | 1,42 → 0,80 | 309 → 324 | Änderung bis Bild Ø 8,8 → 8,2 ms, max 16,5 → 16,5 ms, 674 Sektionen, Rückstau 10 (gleich) |

| Szenario liquids | Frame p99 ms | Tick Ø ms | Speicher MB | Änderung bis Bild Ø / max ms | Sektionen | Rückstau max |
|------------------|-------------:|----------:|------------:|-----------------------------:|----------:|-------------:|
| Etappe 14 vor den Korrekturen (d66f074) | 10,29 | 0,282 [0,270–0,288] | 324 | 15,2 / 43,9 | 4543 | 84 |
| Etappe 14 (ba90146) | 10,31 | 0,305 [0,278–0,335] | 324 | 10,3 / 28,9 | 2619 (−42 %) | 55 |

**Bewertung Etappe 14:** Nach den Korrekturen liegt kein Wert mehr über der Schwelle von 10 %. Server-Tick: stand +4 %, sprint −3 %, crowd +9 % in der ersten und −1 % in der zweiten Reihe (zusammen +4 %, Spannen überlappen); eine Phasenmessung über drei weitere Crowd-Runden zeigt die Wesen-Logik gleich teuer wie in der Basis. Die schlechtesten Ticks schwanken in beiden Ständen zwischen etwa 0,8 und 2,5 ms (Sprint bis 6,6 ms beim Chunk-Streaming) und zeigen keinen wiederholbaren Anstieg. Frame p99 liegt bei beiden Ständen bei 10,2–10,3 ms; der Unterschied der ersten Messung (Basis 9,4 ms) war die bekannte Zweiteilung von p99 (9,1 bzw. 10,3 ms je nach Messzeitpunkt, siehe Etappe 12), die in dieser Reihe auch die Basis traf. Die Vegetation erhöht die Zahl der gezeichneten Dreiecke um rund 12 % bei gleicher Zahl von Draw-Calls (GPU-Last, gewollt). Wiederholbar ist der Spitzenspeicher +12 bis +15 MB (+4 bis +5 %, 44 neue Texturen samt Mod, Vegetation in den Vertexpuffern, Flüssigkeits- und Mob-Daten), unter der Schwelle. Im Szenario liquids vernetzt der Client 42 % weniger Sektionen neu, der Rückstau sinkt von 84 auf 55 und die Zeit von der Änderung bis zum Bild von 15,2 auf 10,3 ms im Mittel und von 43,9 auf 28,9 ms im schlechtesten Fall; der Tick (Flüssigkeits-Schritte) bleibt in der Streuung der Vorher-Läufe. Rest und Abhilfen sind in E-062 festgehalten.

**Etappe 15 (Spielhandbuch), verschränkt gegen `dev` 0b4679e, je 3 Runden (Mittel [Spanne]):**

| Szenario | Frame p99 ms (Basis → 15) | Tick Ø ms | Tick max ms | Speicher MB |
|----------|---------------------------|-----------|-------------|-------------|
| stand | 10,27 → 10,26 | 0,305 → 0,304 | 2,26 → 2,27 | 312 → 317 |
| sprint | 10,29 → 10,27 | 0,329 → 0,321 | 5,62 [2,95–8,51] → 3,13 [1,97–5,27] | 381 → 387 |
| crowd | 10,22 → 10,21 | 0,489 → 0,510 (+4 %) | 1,27 → 3,93 [0,98–9,35] | 318 → 324 |

**Bewertung Etappe 15:** Keine Verschlechterung über der Schwelle. Frame-Zeiten und mittlere Tick-Zeit sind gleich (±4 %, Spannen überlappen). Der schlechteste Tick schwankt in beiden Ständen um einzelne Spitzen bis 8–9 ms (Basis im Sprint, Etappe 15 im Gedränge) und ist nicht wiederholbar. Der Spitzenspeicher steigt um 5–6 MB (+2 %) durch das Handbuchmodell und rund 430 zusätzliche Übersetzungen je Sprache.

## Methode: verschränkte A/B-Messung

Seit Etappe 12 werden Werte nur verschränkt verglichen: Ein Skript startet für jedes Szenario abwechselnd die Basis und den neuen Stand (Runde 1: Basis stand, neu stand, Basis sprint, neu sprint …; dann Runde 2 usw.), je Lauf 15–20 s nach 3 s Aufwärmen, Ton gemischt, aber stumm, nach 30 s Ruhe zu Beginn; ausgewertet werden Mittel und Spanne pro Stand. Grund: Frame p99 pendelt je nach Messzeitpunkt zwischen etwa 9,1 und 10,3 ms (Display-Takt, Systemlast), Tick-Zeiten um bis zu ±80 % zwischen Messreihen desselben Stands; nur abwechselnd gemessene Werte sind vergleichbar. Beide Stände werden als RelWithDebInfo außerhalb des Repos gebaut (Basis aus `git archive` bzw. einem temporären Worktree). Die Szenarien kommen seit Etappe 16 aus `scripts/bench-common.sh` (`run_game`), das Bench-, Profil- und Perf-Check-Skript gemeinsam nutzen. Seit Etappe 16 laufen alle Skriptläufe auf dem eingebauten Display und im Hintergrund (`QTCRAFT_SCREEN=builtin`, `QTCRAFT_BACKGROUND=1`, E-068 ff., README); ältere Stände, die diese Haken nicht kennen, bekommen sie für den Vergleich nachgerüstet, sonst öffnet das Fenster unter dem Mauszeiger, unter Umständen auf einem externen 60-Hz-Monitor, und die Frame-Abstände sind nicht vergleichbar. Neue Werte seit Etappe 16: GUI-/Render-CPU und GPU pro Frame (unabhängig von VSync), Entity-Vernetzung, Sektionspuffer.

**Etappe 16 (Profiling und Optimierung), verschränkt gegen `dev` 9d0e81c (beide mit den Haken für Bildschirm und Hintergrund nachgerüstet), Endstand f1663f6 im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:**

| Szenario | Frame Ø ms (Basis → 16) | Frame p99 ms | Tick Ø ms | Tick max ms | Speicher MB | Bemerkung |
|----------|-------------------------|--------------|-----------|-------------|-------------|-----------|
| stand | 8,54 → 8,44 | 14,3 [12,4–15,8] → 13,2 [9,4–15,6] | 0,175 → 0,154 | 3,53 → 1,10 | 322 → 295 (−8 %) | GUI-CPU 0,30 ms, Render-CPU 1,07 ms, GPU 0,95 ms |
| sprint | 8,45 → 8,48 | 14,2 → 13,1 | 0,185 [0,167–0,205] → 0,228 [0,203–0,251] | 1,99 → 1,64 | 389 → 351 (−10 %) | Sektionspuffer 56,4 MB |
| crowd | 8,55 → 8,47 | 15,0 → 13,8 | 0,237 [0,168–0,276] → 0,287 [0,277–0,293] | 2,07 → 1,09 | 326 → 301 (−8 %) | Entity-Vernetzung 0,27 ms |
| light | 8,46 → 8,45 | 13,5 → 13,4 | 0,186 → 0,154 | 1,26 → 1,83 | 326 → 300 (−8 %) | Änderung bis Bild 9,5 → 9,4 ms, Rückstau 10 → 10 |
| liquids | 8,60 → 8,59 | 15,2 → 14,6 | 0,179 → 0,169 | 1,17 → 1,10 | 334 → 308 (−8 %) | Änderung bis Bild 10,7 → 10,3 ms, Rückstau 40 → 39 |

**Bewertung Etappe 16:** Keine Verschlechterung der Frame-Werte; p99 ist bei beiden Ständen gleich hoch, weil während der Messreihe am Rechner gearbeitet wurde (Fenster im Hintergrund; ruhig: 9,9–10,3 ms, siehe Perf-Check in `docs/Performance.md`), und beim Endstand im Mittel um 0,1–1,1 ms niedriger. Spitzenspeicher −8 bis −10 % (kompakte Block-Eckpunkte, E-069). Server-Tick: stand −12 %, light −17 %, liquids −6 %, sprint +23 % und crowd +21 %; der Server- und Common-Code ist bis auf den Handbuch-Builder unverändert (`git diff 9d0e81c -- src/server src/common`), im Gedränge liegen die Runden 2 und 3 nur 4–8 % über der Basis (die Basis-Runde 1 mit 0,168 ms ist der Ausreißer), und in der Vorher-Nachher-Messreihe gegen 562ecc8 schwankt derselbe Wert zwischen −24 % und +34 % je Szenario und Sichtweite bei identischem Server-Code. Das ist die bekannte Streuung der Tick-Zeit (abhängig davon, welchem Kern und Takt das System den Server-Thread gibt), keine Regression; ein tiefes Profiling unveränderten Codes ist nicht angezeigt. Die vollständige Aufteilung der Frame-Kosten vorher/nachher (alle Szenarien, 8/12/16 Chunks) steht in `docs/Performance.md`.

**Etappe 19 (Aufteilen zu großer Dateien), verschränkt gegen `dev` a5c1657, Stand d44e3b0 im Release-Build, je 3 Runden à 20 s (stand 7 Runden), 8 Chunks, Mittel [Spanne]:** Die Aufteilung berührt heiße Pfade (Mesher, Blocklicht, Physik, Raycast, Weltgenerierung, Frame-Schritt), daher verschränkt gemessen.

| Szenario | Frame Ø ms (Basis → 19) | Frame p99 ms | Tick Ø ms | Speicher MB | Bemerkung |
|----------|-------------------------|--------------|-----------|-------------|-----------|
| stand | 8,33 → 8,35 | 10,29 → 10,29 | 0,639 [0,41–0,78] → 0,704 [0,44–0,80] | 368 → 369 | Tick zweigipflig (≈ 0,4 oder ≈ 0,7 ms je nach Lauf, bei beiden Ständen); Runden 4–7 allein: 0,741 → 0,750 |
| sprint | 8,36 → 8,34 | 12,04 [10,3–15,5] → 10,31 | 0,610 [0,38–0,83] → 0,621 [0,48–0,78] | 446 → 444 | ein Hänger im ersten Basislauf |
| crowd | 8,46 → 8,34 | 15,81 [10,0–27,1] → 10,26 | 0,955 [0,78–1,14] → 0,802 [0,52–0,95] | 374 → 373 | ein Hänger im zweiten Basislauf |

**Bewertung Etappe 19:** Keine Verschlechterung über der Schwelle; Frame-Zeiten und Speicher gleich, Tick-Unterschiede liegen innerhalb der Streuung beider Stände. Einzelmessungen der Agenten (Release, alt gegen neu): Meshing 10,93 → 10,86 s, Blocklicht 134,8 → 135,5 ms, Mischer 272,5 → 273,1 ms, Weltgenerierung 0,87 → 0,58 ms pro Chunk (Ausgabe bit-gleich), Raycast 72 → 70 ms, `stepEntity` 443 → 442 ms. Nachtrag (Etappe 20): Beide Stände dieser Messung enthielten schon die Weltgenerierungs-Politur (a5c1657); der Sprung gegenüber Etappe 16 (GPU ×1,9, Tick ×2, Speicher +23 %) kommt vollständig aus ihr und ist gewollt (Bisektion siehe Etappe 20).

**Etappe 20 (Feinschliff, Multiplayer-Voraussetzungen, Sollzeit-Takt), verschränkt gegen `tickbase` (13ee464: `dev` 25c9fd3 plus Messung der Takt-Verspätung, noch alter periodischer Timer) und `dev` 25c9fd3, Endstand e53827f im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** Systemlast (1-min-Mittel) 1,9–3,9; in 10 der 45 Läufe lief die Aktivitätsanzeige (≈ 25–30 % CPU), in 6 Läufen Safari (bis 100 %, über alle drei Stände verteilt).

| Szenario | Tick Ø ms (tickbase → 20) | Verspätung Ø / p99 ms (tickbase → 20) | GPU Ø ms (dev → 20) | Dreiecke (dev → 20) | Speicher MB (dev → 20) | Frame p99 ms (dev → 20) |
|----------|---------------------------|----------------------------------------|---------------------|---------------------|------------------------|-------------------------|
| stand | 0,792 [0,75–0,83] → 0,736 [0,72–0,76] (−7 %) | 0,37 / 1,22 → 0,14 [0,11–0,16] / 0,60 [0,49–0,75] | 1,56 → 1,56 | 423 000 → 465 000 | 367 → 370 | 10,26 → 10,15 |
| sprint | 0,809 [0,79–0,82] → 0,855 [0,75–0,92] (+6 %) | 0,14 / 0,62 → 0,17 / 0,93 | 1,53 → 1,99 (+30 %) | 383 000 → 566 000 | 445 → 521 (+17 %) | 10,38 → 10,29 |
| crowd | 1,079 [1,04–1,11] → 1,037 [0,97–1,15] (−4 %) | 0,76 / 1,68 → 0,26 / 0,74 | 1,78 → 1,84 (+3 %) | 427 000 → 468 000 | 375 → 375 | 10,25 → 10,13 |
| light | 0,681 [0,67–0,71] → 0,797 [0,72–0,84] (+17 %; Wiederholung +6 %, s. u.) | 0,87 / 2,81 → 0,18 / 0,84 | 1,18 → 1,33 (+13 %) | 417 000 → 461 000 | 379 → 382 | 11,30 → 11,31 |
| liquids | 0,734 [0,70–0,79] → 0,705 [0,62–0,75] (−4 %) | 0,39 / 3,31 → 0,20 / 0,79 | 1,22 → 1,25 (+2 %) | 406 000 → 455 000 | 381 → 385 | 10,95 → 10,61 |

Ausgangslage (verschränkte Messung der Hauptsitzung vor den Korrekturen, gleiche Methode): Tick Ø tickbase → Etappe 20 stand 0,58 → 1,00, sprint 0,62 → 0,92, crowd 0,71 → 1,23, light 0,56 → 1,04, liquids 0,60 → 1,03 ms (+60 bis +85 %); Verspätung Ø 0,14–0,71 → 2,16–2,49 ms, p99 (stand) 0,7 → 3,3 ms; GPU sprint 1,59 → 2,11 ms, Spitzenspeicher sprint 446 → 523 MB.

**Bewertung Etappe 20:**

- **Takt-Verspätung (Messfehler und echte Verspätung):** Ein Versuchsprogramm (Qt 6.8, eigener Thread, 200 Ticks) zeigt, dass die Timer der Ereignisschleife unter macOS 1–3 ms nach der angeforderten Zeit auslösen: periodischer `PreciseTimer` Ø 2,0–2,3 ms, einmaliger Timer auf die aufgerundete Restzeit Ø 2,2 ms, abgerundet Ø 1,1–1,2 ms, `QChronoTimer` mit Nanosekunden Ø 2,1 ms. Der alte periodische Timer war also ebenso spät; seine Messung legte das Raster auf das erste (schon späte) Auslösen und zeigte nur die Schwankung. Korrektur (E-090): Timer 3 ms vor dem Sollzeitpunkt, Schlaf bis 0,2 ms davor, Rest aktiv; Server-Thread mit hoher Priorität. Ergebnis gegen das wahre Raster: Ø 0,14–0,26 ms, p99 0,60–0,93 ms in allen Szenarien (Ziel stand: Ø ≤ 0,3, p99 ≤ 1,5 ms). Die schlechteste Verspätung einzelner Läufe (bis 8–13 ms, einmal 35 ms im Szenario liquids) bleibt Ausreißer (Nachrichtenverarbeitung zwischen zwei Ticks, Systemlast); kein Tick wurde übersprungen.
- **Tick-Kosten:** Phasenmessung im Tick (vorübergehende Zähler, stand): Pflanzenwachstum (E-102) 0,50 ms pro Tick, weil es in jedem Tick die Paletten aller rund 4600 geladenen Abschnitte las; übrige Phasen wie in tickbase (Wesen 0,42–0,50 ms, Clients 0,09 ms, Entladen 0,09 ms). Blockupdates und Flüssigkeiten (mit der Abgrundsuche) liegen je unter 0,005 ms; Ratenbegrenzung und Sichtlinie laufen nur bei eingehenden Nachrichten, nicht im Tick. Korrektur: Abschnitts-Masken für wachsende Pflanzen wie beim Kontaktschaden (`QcSectionMarks`), Wachstum 0,50 → 0,02–0,03 ms. Danach liegt der Tick in stand, crowd und liquids 4–7 % unter, in sprint 6 % über tickbase. light lag in der Reihe 17 % darüber (Spannen ohne Überlappung); eine Wiederholung bei ruhigem System (3 Runden, beide Stände mit Phasenzählern) ergab 0,706 [0,68–0,75] → 0,748 [0,73–0,77] ms (+6 %), in den Phasen ist nur das verbliebene Wachstum neu (0,02–0,03 ms, Zuckerrohr an Gewässern in der Nähe), die Wesen-Logik liegt in der Streuung. Kein Wert über der Schwelle.
- **GPU und Speicher:** Die Zahl der gezeichneten Dreiecke wächst durch die neue Biomverteilung (E-101, häufigere Gebirge) um 10 % (stand, crowd, light, liquids) bzw. 48 % (sprint: der Lauf endet zwischen Bergen, 393 statt 335 sichtbare Sektionen). Eine verschränkte Messung mit abgeschaltetem Himmel (2 Runden) zeigt für den Himmels-Shader (E-095) höchstens 0,08 ms (stand 1,57 → 1,49 ms ohne Himmel, sprint und crowd innerhalb der Streuung); eine Umstellung auf „nur wo leer“ lohnt nicht. Die Mehrkosten in sprint sind Gelände. Der Spitzenspeicher in sprint (+76 MB) entsteht mit derselben Änderung der Weltgenerierung (26e6548 gegen seinen Vorgänger 5ff4423: 442 → 518 MB, Dreiecke 383 000 → 568 000; Code sonst gleich): `vmmap` nach 23 s zeigt Grafiktreiber-Speicher (IOAccelerator, resident) 115 → 214 MB bei fast gleichen Sektionspuffern (102 → 106 MB) und kleinerem Heap (231 → 220 MB); vermutlich hält der Metal-Treiber die Puffer ersetzter Sektionen (mehr und größere Sektionsnetze auf der Bergstrecke) zurück. Gewollt (Gelände), ein späterer Sektionspuffer-Pool könnte das begrenzen.
- **Sprung von Etappe 16 zu `dev` (Bisektion, Release, je 2 Runden stand und sprint verschränkt, Last 2,5–6,6):** Etappe 16 (49840a3), 17 (0416cd2), 18 (fb888e4) liegen gleich (stand GPU 0,77–0,78 ms, Tick 0,115–0,121 ms, Speicher 298–302 MB, 227 000 Dreiecke); der Sprung kommt vollständig mit der Weltgenerierungs-Politur a5c1657: stand GPU 0,77 → 1,48 ms, Dreiecke 227 000 → 423 000 (+86 %), sichtbare Sektionen 272 → 319, Sektionen 972 → 1254, Sektionspuffer 43,6 → 92,1 MB, Speicher 302 → 371 MB, Tick 0,117 → 0,246 ms; sprint GPU 0,90 → 1,41 ms, Speicher 355 → 442 MB, Tick 0,143 → 0,290 ms. Etappe 19 (a5b0217) und 19b (`dev` 25c9fd3) ändern daran nichts. Ursachen und Bewertung: mehr Gelände pro Sektion (Höhlen, Höhleneingänge, Überhänge, abwechslungsreicheres Relief, mehr Vegetation; E-085) erklärt GPU und Speicher; der Tick wächst durch die dauerhaften Tiere (E-086: 10 % der Chunks bekommen eine Gruppe, die geladen nicht ruht), `sample` des Server-Threads in stand: Wesen-Logik und `moveBox` 23 → 59 Proben, der Client sieht 24 statt 14 Wesen. Beides ist gewolltes Verhalten der Politur und wird nicht geändert; Frame-Zeiten bleiben im Budget (p99 9,5–10,3 ms).

- **Nachtrag (nach der Block-Erweiterung): Chat-Ratenbegrenzung verfälschte light.** Die Chat-Grenze (E-091, 2 pro Sekunde, Stoß 20) galt in Etappe 20 auch für den lokalen Spieler und verwarf Befehle der Szenarien: light 19–21 der rund 90 Fackel-Befehle pro Lauf (nach etwa 10 s jeden zweiten), crowd einen der 28 Startbefehle. tickbase hatte keine Grenze; der Vergleich tickbase → Etappe 20 in light maß Etappe 20 also mit kleinerer Last (46–50 statt 78 Lichtjobs, 371 statt 625 neu vernetzte Sektionen). Seit dem Nachtrag zu E-091 ist der lokale Spieler vom Chat-Eimer ausgenommen. Nachmessung (Release, light, 3 Runden à 20 s, verschränkt tickbase 13ee464 → Etappe 20 94064cc → Etappe 20 mit nachgerüsteter Ausnahme (Diff von `src/server` aus f885c22), Last 3,8–5,7, kein Fremdprozess ≥ 80 % CPU, kein Lauf belastet):

| Stand | verworfen | Lichtjobs | Sektionen | Tick Ø ms | Tick max ms | GPU Ø ms | GUI-CPU ms | `change_mesh` Ø / max ms | Lichtjob ms |
|-------|-----------|-----------|-----------|-----------|-------------|----------|------------|---------------------------|-------------|
| tickbase | 0 | 78 | 625 | 0,470 [0,41–0,57] (Median 0,425) | 2,0 | 1,27 | 0,42 | 10,8 / 25,3 | 0,256 |
| Etappe 20, begrenzt | 19–21 | 46 | 371 | 0,487 [0,45–0,51] (+4 %) | 2,4 | 1,29 | 0,39 | 15,1 / 27,6 | 0,243 |
| Etappe 20, volle Last | 0 | 78 | 634 | 0,504 [0,47–0,58] (+7 %; Median 0,468, +10 %) | 2,4 | 1,28 | 0,40 | 14,0 / 25,3 | 0,234 |

  **Bewertung:** Mit voller Last liegt der Tick von Etappe 20 in light im Mittel 7 % über tickbase, im Median 10 % (ohne den Ausreißer der dritten tickbase-Runde, 0,572 ms, +11 %); die Spannen überlappen. Die volle Last kostet in Etappe 20 rund +0,02 ms Tick (+3,5 %), im Stand der Block-Erweiterung +0,05 ms (+11 %, siehe dort). Die Bewertung „kein Wert über der Schwelle“ hält für light damit nur knapp: Etappe 20 liegt an der Schwelle, nicht klar darüber, und der Vorsprung war durch die verworfenen Befehle um etwa 3–4 Prozentpunkte geschönt. GPU, GUI-CPU, Lichtjobs und die schlechteste Zeit bis zum Bild (25,3 gegen 25,3 ms) sind gleich; das höhere Mittel von `change_mesh` (+29 %) liegt in der Streuung dieses Werts (im Szenario glass schwankt er zwischen zwei gleichen Ständen um 30 %, siehe Block-Erweiterung). Steigt der Tick in light in einer späteren Etappe weiter, ist er mit Phasenzählern wie in Etappe 20 einzugrenzen.

**Block-Erweiterung (nach Etappe 20), verschränkt gegen Etappe 20 (94064cc) mit dem Stand 0b6d0b4 (`dev`: Etappe 20 plus Block- und Rezept-Erweiterung, E-200 bis E-217) im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** neues Szenario **glass** (nachts auf geräumtem Holzboden zwischen Wänden aus Scheiben, Glas, Laub und Eis mit acht Lampen; zweimal pro Sekunde erscheinen oder verschwinden Reihen von Lichtquellen und 3 × 3-Flächen durchsichtiger Blöcke) und **glass-full** (dasselbe mit gefärbtem Glas und Scheiben, Eisengitter, Glühwürmchenglas, Laternen, kleinen Fackeln und Netzen; nur der neue Stand). Systemlast (1-min-Mittel) 1,0–5,7; Safari lief zeitweise mit 100 % CPU, in diesen Phasen wurde nicht gemessen (kein Lauf als belastet markiert).

| Szenario | Frame p99 ms | GUI-CPU ms | Render-CPU ms | GPU Ø ms | Dreiecke / Draw-Calls | Tick Ø ms | Speicher MB |
|----------|--------------|------------|---------------|----------|-----------------------|-----------|-------------|
| stand | 10,15 [9,86–10,30] → 9,98 [9,45–10,32] | 0,43 → 0,43 | 1,56 → 1,63 (+4 %) | 1,62 [1,52–1,81] → 1,69 [1,59–1,88] (+4 %) | 465 000 → 480 000 / 336 → 343 | 0,712 [0,61–0,79] → 0,821 [0,61–0,98] (+15 %) | 373 → 380 |
| sprint | 10,09 [9,68–10,30] → 10,05 [9,68–10,25] | 0,41 → 0,46 (+13 %) | 1,73 → 1,73 | 1,97 [1,84–2,22] → 2,08 [1,92–2,33] (+5 %) | 568 000 → 543 000 / 400 → 375 | 0,715 [0,47–0,85] → 0,817 [0,52–0,99] (+14 %) | 523 → 542 (+4 %) |
| crowd | 9,99 [9,40–10,31] → 10,01 [9,66–10,18] | 0,80 → 0,79 | 1,33 → 1,38 | 2,10 → 2,08 | 468 000 → 484 000 / 335 → 341 | 0,907 [0,78–1,07] → 0,927 [0,64–1,11] (+2 %) | 375 → 385 |
| light | 11,14 [10,35–12,70] → 10,18 [9,97–10,32] | 0,50 → 0,55 (+10 %) | 1,63 → 1,67 | 1,22 [1,19–1,27] → 1,39 [1,36–1,45] (+14 %) | 461 000 → 477 000 / 336 → 342 | 0,629 [0,42–0,76] → 0,718 [0,48–0,85] (+14 %) | 382 → 388 |
| liquids | 12,45 [10,64–15,23] → 11,14 [10,24–12,85] | 0,41 → 0,44 (+6 %) | 1,56 → 1,61 | 1,21 [1,19–1,25] → 1,33 [1,31–1,37] (+10 %) | 455 000 → 473 000 / 336 → 342 | 0,586 [0,46–0,69] → 0,721 [0,52–0,83] (+23 %) | 388 → 390 |
| glass | 12,08 [10,30–14,18] → 10,67 [10,21–11,15] | 0,39 → 0,44 (+11 %) | 1,44 → 1,48 | 1,73 [1,59–1,84] → 1,94 [1,75–2,03] (+12 %) | 464 000 → 480 000 / 339 → 345 | 0,547 [0,44–0,72] → 0,600 [0,49–0,79] (+10 %) | 384 → 392 |
| glass-full (nur neu) | 10,58 [9,96–11,50] | 0,44 | 1,47 | 1,94 [1,76–2,06] | 480 000 / 345 | 0,581 [0,44–0,85] | 391 |

Blockänderungen bis zur sichtbaren Sektion (`change_mesh` Ø / max ms), Rückstau und Lichtjobs (base → neu): light 11,5 / 22,4 → 11,6 / 20,4, Rückstau 10 → 11, Lichtjob 0,36 → 0,39 ms; liquids 15,6 / 51,0 → 15,5 / 44,2, Rückstau 54 → 40, Lichtjob 0,041 → 0,051 ms; glass 14,4 / 32,7 → 11,1 / 27,5, Rückstau 27 → 27, Lichtjob 0,112 → 0,100 ms (99 → 96 Jobs); glass-full 10,6 / 22,2, Rückstau 27, Lichtjob 0,105 ms. Die Verspätung des Takts liegt in allen Szenarien bei Ø 0,10–0,20 ms; kein Tick wurde übersprungen.

**Kontrollmessung mit gleichem Gelände:** Die geräumten Szenarien sind entgegen der Erwartung nicht direkt vergleichbar: Mit den Seeds nach Namen (E-214) und den neuen Features (Blumen, Farn, toter Busch, Schlamm am Sumpfufer, Netze, Magma) stehen Bäume anders, und der Spawn verschiebt sich (Seed 42: −23,5 / 70 / −15,5 → −23,5 / 71 / −23,5). Hinter der geräumten Fläche sieht der alte Stand eine Baumkrone nah vor der Kamera, der neue Himmel, Wiese und einen Hang (Screenshots verglichen). Deshalb ein dritter Build `ctrl`: Code und Inhalt von 0b6d0b4, aber Weltgenerierung wie 94064cc (E-214 zurückgenommen, Biome, `sugar_cane`/`tall_grass` und die neuen Features und Erze entfernt). Gelände, Spawn und Dreiecke sind damit gleich wie im alten Stand (Abweichung < 0,02 %). Verschränkt base → ctrl, 3 Runden, light und glass 6, liquids 10 Runden, Last 1,2–5,0 (der Rechner lief in dieser Reihe mit etwa halb so hohen CPU-Zeiten wie in der Hauptreihe, Vergleiche gelten nur innerhalb einer Reihe):

| Szenario | GUI-CPU ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Speicher MB | Frame p99 ms |
|----------|------------|---------------|----------|-----------|-------------|--------------|
| stand | 0,273 → 0,286 (+5 %) | 0,87 → 0,90 (+3 %) | 1,59 → 1,66 (+4 %) | 0,315 → 0,330 (+5 %) | 369 → 379 (+3 %) | 9,98 → 9,97 |
| sprint | 0,268 → 0,284 (+6 %) | 0,94 → 0,99 (+5 %) | 2,12 → 2,02 (−5 %) | 0,362 → 0,370 (+2 %) | 519 → 531 (+2 %) | 9,99 → 9,98 |
| crowd | 0,471 → 0,475 (+1 %) | 0,73 → 0,72 | 2,28 → 2,19 (−4 %) | 0,412 → 0,440 (+7 %) | 377 → 382 (+1 %) | 9,92 → 9,98 |
| light | 0,304 → 0,309 (+2 %) | 0,91 → 0,87 (−4 %) | 1,17 → 1,20 (+3 %) | 0,350 → 0,365 (+4 %) | 381 → 387 (+2 %) | 9,83 → 9,91 |
| liquids | 0,258 [0,227–0,306] → 0,294 [0,275–0,312] (+14 %) | 0,88 → 0,90 (+3 %) | 1,22 → 1,20 (−2 %) | 0,330 → 0,345 (+5 %) | 383 → 393 (+3 %) | 9,81 → 9,96 |
| glass | 0,317 → 0,310 (−2 %) | 0,93 → 0,90 (−3 %) | 1,63 [1,40–1,75] → 1,78 [1,70–1,83] (+9 %) | 0,362 → 0,388 (+7 %) | 385 → 389 (+1 %) | 9,97 → 9,99 |

In der Kontrollreihe: light `change_mesh` 9,2 / 18,6 → 7,9 / 16,3 ms, Lichtjob 0,185 → 0,167 ms; liquids 14,5 / 43,3 → 9,9 / 29,9 ms; glass 8,8 / 18,2 → 8,0 / 16,5 ms, Lichtjob 0,084 → 0,079 ms.

**Bewertung Block-Erweiterung:**

- **Über der Schwelle in der Hauptreihe, durch das Gelände erklärt:** Tick Ø +10 bis +23 % (stand, sprint, light, liquids, glass), GPU +10 bis +14 % (light, liquids, glass), GUI-CPU +10 bis +13 % (sprint, light, glass). Mit gleichem Gelände (ctrl) bleiben davon Tick +2 bis +7 %, GPU −5 bis +9 % (glass +9 %, Spanne des alten Stands 1,40–1,75 ms) und GUI-CPU −2 bis +6 %. Der Rest kommt aus der neuen Weltgenerierung: anderer Spawn, andere Baumstellung vor der Kamera (Überzeichnung durch Laub nah an der Kamera, Himmel), andere Tiergruppen (E-214, neue Features); in sprint endet der Lauf an anderer Stelle (−4 % Dreiecke, −25 Draw-Calls). Gewollt.
- **Verbleibender Code-Anteil:** Der Tick wird in allen Szenarien um 2–7 % teurer (neue Physik in `stepEntity`: `slowsInside` über alle Zellen des Körpers und `surface` unter den Füßen für jedes Wesen in jedem Tick, E-207; Schaden von unten, E-209). Unter der Schwelle, kein Handlungsbedarf. Speicher +5 bis +12 MB (+1 bis +3 %): rund 110 neue Blöcke mit eigenen Texturebenen und Handbuchseiten.
- **liquids GUI-CPU +14 % (+0,036 ms pro Frame, 0,4 % des Frame-Budgets):** in 10 Runden stabil; der alte Stand streut zweigeteilt (0,23–0,25 oder 0,29–0,31 ms), der neue liegt immer bei 0,28–0,31 ms. `sample` des GUI-Threads (2 × 2 Läufe, je 10 s) zeigt keinen neuen oder gewachsenen Hotspot; die Unterschiede verteilen sich in der Streuung auf Entity-Vernetzung, `polishAndSync` und Chat-Zeilen. Begründet in E-218.
- **Transparenz und Licht (Wunsch des Menschen):** Mit gleichem Gelände sind Neuvernetzung nach Blockänderungen (`change_mesh` Ø und max −9 bis −31 %), Rückstau und Lichtjobs (−10 bis +1 %) im neuen Stand gleich oder schneller; Render-CPU (Sortierung der durchscheinenden Flächen) gleich. glass-full mit den neuen Blöcken kostet so viel wie glass: GPU 1,94 gegen 1,94 ms, `change_mesh` 10,6 gegen 11,1 ms, Lichtjob 0,105 gegen 0,100 ms, Tick 0,58 gegen 0,60 ms. Frame p99 liegt im neuen Stand im Mittel bei 9,98–11,14 ms (einzelne Läufe bis 12,9 ms, alter Stand bis 15,2 ms), weit im Budget von 16,7 ms für 60 FPS.
- **Nebenbefund Chat-Ratenbegrenzung (seit Etappe 20, beide Stände gleich):** Chat-Befehle sind auf 2 pro Sekunde (Stoß 20) begrenzt. Das Szenario light schickt 4 Fackel-Befehle pro Sekunde; nach etwa 10 s wird rund jeder zweite verworfen (19 von rund 90 Befehlen pro Lauf, „Rate limit: dropped … chat messages“ im Log), crowd verliert einen der 28 Startbefehle. Für den A/B-Vergleich unschädlich, weil beide Stände gleich betroffen sind; die Last des Szenarios light ist aber seit Etappe 20 kleiner als in den Etappen davor. glass und glass-full bleiben unter der Grenze (keine verworfenen Befehle). Behoben durch den Nachtrag zu E-091 (lokaler Spieler ohne Chat-Grenze), Nachmessung unten.

**Nachtrag: volle Last nach der Ausnahme vom Chat-Limit, verschränkt 54322eb (ohne Ausnahme) → f885c22 (mit Ausnahme), Release, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** dazu tickbase (13ee464) in light. Last (1-min-Mittel) 2,8–5,8, vor jedem Lauf kein Fremdprozess ≥ 80 % CPU, kein Lauf belastet. In den Logs der Läufe mit Ausnahme steht keine Zeile „Rate limit: dropped … chat messages“ mehr (vorher light 19–21 verworfene Befehle pro Lauf, crowd 1).

| Szenario | verworfen | Last-Kennzahlen | Tick Ø ms | Tick max ms | GPU Ø ms | GUI-CPU ms | Render-CPU ms | `change_mesh` Ø / max ms | Lichtjob ms | Frame p99 ms |
|----------|-----------|-----------------|-----------|-------------|----------|------------|---------------|---------------------------|-------------|--------------|
| light | 19–21 → 0 | Lichtjobs 50 → 78, Sektionen 461 → 736, Mesh-Jobs 410 → 652 | 0,487 [0,43–0,52] → 0,541 [0,52–0,58] (+11 %) | 1,25 → 1,74 | 1,46 → 1,45 | 0,41 → 0,40 | 1,29 → 1,26 | 10,2 / 21,9 → 13,1 / 20,4 | 0,255 → 0,257 | 9,91 → 10,32 |
| glass (gleiche Last, Kontrolle) | 0 → 0 | Lichtjobs 96 → 96, Sektionen 194 → 192 | 0,485 [0,44–0,51] → 0,516 [0,50–0,54] (+6 %) | 1,31 → 1,46 | 1,87 → 1,87 | 0,39 → 0,37 | 1,22 → 1,23 | 9,2 / 19,8 → 12,2 / 30,6 | 0,095 → 0,091 | 9,71 → 9,98 |
| crowd | 1 → 0 | Wesen 75 → 84 | 0,622 [0,57–0,69] → 0,654 [0,59–0,77] (+5 %) | 1,55 → 2,03 | 2,07 → 2,05 | 0,59 → 0,68 (+16 %) | 1,04 → 1,16 (+12 %) | – | – | 9,90 → 10,03 |

tickbase in derselben Reihe (light, altes Gelände): Tick 0,392 [0,34–0,43] ms, GPU 1,27 ms, 79 Lichtjobs, 630 Sektionen.

**Bewertung des Nachtrags:** Die volle Last von light kostet im Stand der Block-Erweiterung +0,05 ms Tick (+11 %, Spannen berühren sich) und rund 60 % mehr Neuvernetzung (736 statt 461 Sektionen, so viele wie in tickbase); GPU, GUI- und Render-CPU, Lichtjob und schlechteste Zeit bis zum Bild bleiben gleich. Im Kontrollszenario glass (gleiche Last in beiden Ständen, die Ausnahme ändert dort nichts) steigt der Tick in derselben Reihe um 6 % und `change_mesh` Ø um 33 %, max um 55 %; diese Werte streuen also so stark, und für light ist nur der Teil über glass (+5 % Tick) der Last zuzuschreiben. crowd bekommt seinen fehlenden Startbefehl: 84 statt 75 Wesen in der Messung, GUI-CPU +16 % und Render-CPU +12 % (Entity-Vernetzung und -Zeichnen der zusätzlichen Wesen), Tick +5 %, GPU gleich. Frame p99 bleibt in allen Szenarien bei 9,9–10,3 ms. Gegen tickbase liegt der Tick in light mit voller Last 38 % höher; das enthält das neue Gelände von Etappe 20 und der Block-Erweiterung (E-101, E-214, siehe oben) und ist nicht der Code allein; der direkte Vergleich von Etappe 20 mit tickbase bei gleicher Last steht im Nachtrag zu Etappe 20. Die bisherigen A/B-Vergleiche (Etappe 20 → Block-Erweiterung) bleiben gültig, weil beide Stände gleich betroffen waren; Stände von Etappe 20 bis 54322eb erzeugen in light und crowd weniger Last; für künftige A/B-Vergleiche mit ihnen wird die Ausnahme nachgerüstet (wie hier für 94064cc, Diff von `src/server`).

**Etappe 21 (Showcase-, Flach- und Leere-Welt, Protokoll-Codec nach Themen, Beschriftungen, Weltinfo Format 2), verschränkt gegen `dev` dfe840a mit dem Stand 0a118c5 (Etappe 21 plus die beiden Korrekturen unten) im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** Das Gelände der normalen Welt ist unverändert (Dreiecke, Sektionen und Sektionspuffer gleich bis auf ±3 Dreiecke), die Werte sind also direkt vergleichbar. Last (1-min-Mittel) 1,7–4,8, vor jedem Lauf kein Fremdprozess ≥ 80 % CPU, kein Lauf belastet.

Zwei Befunde vor der Messung:

- **Keine Frames hinter fremden Fenstern:** Alle Läufe endeten mit „Benchmark: no frames rendered“, weil das Fenster des Skriptlaufs (1280 × 720, mittig auf dem eingebauten Display) ganz unter einem anderen Fenster lag; macOS meldet es dann als verdeckt (`qt.qpa.window`: „Occlusion state … changed to occluded“) und zeichnet nicht mehr. Skriptläufe (`--background`) halten ihr Fenster jetzt über den anderen (`Qt::WindowStaysOnTopHint`); für den A/B-Vergleich wurde die Basis mit derselben Zeile gebaut.
- **Halb so viele Tiergruppen (Fehler aus Etappe 21):** Eine erste Messreihe zeigte im neuen Stand weniger Wesen (stand 21 → 16, sprint 22 → 11, light/glass 20 → 16, liquids 22 → 17) und damit niedrigere Kosten. Ursache: Der Charakterständer `qtcraft:mannequin` (E-220, Gewicht 0, ohne Spawn-Blöcke) zählte als Tier, das auf allem stehen darf; die Suche nach dem Boden einer Tiergruppe hielt deshalb schon auf Baumkronen an, wo Schwein und Schaf nicht stehen dürfen, und etwa die Hälfte der Gruppen fiel weg. Jetzt bestimmen nur Arten mit Gewicht > 0 die Böden (Test `animalsSpawnUnderTreeCrowns`, schlägt ohne die Korrektur fehl). Danach sind Wesen und Dreiecke in allen Szenarien gleich wie in der Basis; nur diese Reihe gilt.

| Szenario | Frame p99 ms | GUI-CPU ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Speicher MB | Wesen | Bemerkung |
|----------|--------------|------------|---------------|----------|-----------|-------------|-------------|-------|-----------|
| stand | 10,30 → 10,20 | 0,486 → 0,496 (+2 %) | 1,94 → 1,97 | 1,70 [1,64–1,75] → 1,60 [1,56–1,64] (−6 %) | 0,893 [0,77–0,95] → 0,919 [0,79–1,00] (+3 %) | 3,19 → 2,92 (−8 %) | 384 → 381 | 21 → 21 | |
| sprint | 10,24 → 10,29 | 0,518 → 0,483 (−7 %) | 2,01 → 1,98 | 2,00 → 1,93 (−3 %) | 0,977 [0,88–1,06] → 0,927 [0,89–0,95] (−5 %) | 4,87 [4,36–5,19] → 3,96 [3,61–4,39] (−19 %) | 544 → 547 | 22 → 22 | Upload Ø 0,026 → 0,024 ms, max 1,26 → 1,08 ms; Meshing 1,37 → 1,39 ms pro Sektion |
| crowd | 10,12 → 10,33 | 1,012 → 0,998 | 1,67 → 1,65 | 1,96 → 2,09 (+7 %) | 1,188 [1,13–1,22] → 1,082 [1,04–1,15] (−9 %) | 2,33 [2,11–2,57] → 2,76 [2,29–3,10] (+19 %) | 387 → 387 | 84 → 84 | Entity-Vernetzung 0,71 → 0,69 ms |
| light | 11,11 → 10,75 | 0,599 → 0,596 | 1,94 → 1,92 | 1,44 → 1,51 (+5 %) | 0,832 → 0,866 (+4 %) | 2,66 [2,20–3,43] → 3,04 [2,72–3,34] (+14 %) | 391 → 391 | 20 → 20 | `change_mesh` 14,3 / 32,9 → 12,4 / 30,5 ms, Lichtjob 0,456 → 0,459 ms |
| liquids | 10,53 → 10,65 | 0,473 → 0,494 (+4 %) | 1,82 → 1,85 | 1,37 → 1,38 | 0,800 → 0,794 (−1 %) | 3,22 [2,97–3,39] → 4,07 [2,98–5,67] (+26 %) | 395 → 394 | 22 → 22 | `change_mesh` 15,3 / 41,4 → 16,8 / 45,1 ms |
| glass | 10,38 → 10,70 | 0,549 → 0,546 | 2,01 → 1,98 | 1,73 [1,64–1,84] → 1,86 [1,80–1,91] (+8 %) | 0,858 → 0,828 (−4 %) | 3,35 → 2,92 (−13 %) | 394 → 393 | 20 → 20 | `change_mesh` 12,6 / 24,4 → 11,8 / 28,0 ms, Lichtjob 0,152 → 0,160 ms |

Wiederholung (crowd, light, liquids, 3 Runden, direkt danach; der Rechner lief mit etwa halb so hohen CPU-Zeiten): crowd Tick Ø 0,601 → 0,653 ms (+9 %), Tick max 1,69 → 2,03 ms (+20 %), GUI-CPU 0,546 → 0,541, GPU 2,23 → 2,22 ms; light Tick Ø 0,476 → 0,481 ms (+1 %), Tick max 1,57 → 1,95 ms (+24 %); liquids Tick Ø 0,475 → 0,463 ms (−3 %), Tick max 1,87 → 1,89 ms (+1 %). Frame p99 9,9–10,3 ms bei beiden Ständen.

Codec (eigenes Messprogramm gegen `libqc_common.a` beider Stände, 3 × 5 Durchgänge, bester Wert): Kodieren eines Chunks 0,0162–0,0168 → 0,0157–0,0170 ms, Dekodieren 0,0164–0,0171 → 0,0165–0,0176 ms, `EntityMoves` mit 84 Wesen 7,9–8,0 → 7,9–8,3 µs kodiert, 9,3–9,7 → 9,4–9,9 µs dekodiert. Die Aufteilung nach Themen (6e3c43e) kostet nichts messbares.

**Neue Szenarien (nur Etappe 21, erste Referenzwerte), je 6 Läufe à 20 s, 8 Chunks, Mittel [Spanne]:**

| Szenario | Frame p99 ms | GUI-CPU ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Speicher MB | Wesen | Dreiecke / Draw-Calls | Bemerkung |
|----------|--------------|------------|---------------|----------|-----------|-------------|-------------|-------|-----------------------|-----------|
| showcase | 14,1 [10,2–16,2] | 0,316 [0,24–0,40] | 1,04 [0,79–1,36] | 0,797 [0,78–0,83] | 0,231 [0,20–0,29] | 1,28 | 258 | 6 | 64 000 / 139 | `change_mesh` 15,8 / 27,4 ms (Wasser und Lava der Demos), Lichtjob 0,076 ms |
| flat-crowd | 15,1 [10,3–16,3] | 0,585 [0,45–0,93] | 0,661 [0,49–0,86] | 0,863 [0,84–0,95] | 0,396 [0,29–0,59] | 1,66 | 250 | 80 | 50 000 / 89 | Entity-Vernetzung 0,35 ms |
| flat-liquids | 15,0 [10,4–16,4] | 0,265 [0,20–0,37] | 0,953 [0,73–1,32] | 0,572 [0,56–0,59] | 0,163 [0,12–0,21] | 0,87 | 249 | 0 | 45 000 / 89 | `change_mesh` 10,8 / 36,6 ms, Rückstau 38, 180 Lichtjobs à 0,037 ms |

Beschriftungen in showcase (verschränkt, 3 Runden, `showLabels` an → aus): GUI-CPU 0,301 [0,26–0,33] → 0,271 [0,26–0,28] ms, Render-CPU 0,991 → 0,991 ms, GPU 0,809 → 0,824 ms, Tick 0,222 → 0,239 ms.

**Bewertung Etappe 21:**

- **Keine Mittelwerte über der Schwelle.** Frame p99, GUI- und Render-CPU, GPU, Tick Ø, Upload, Meshing, Lichtjobs und Speicher liegen zwischen −9 % und +8 % (GPU glass +8 %, Spannen fast berührend; GPU crowd +7 % und in der Wiederholung −1 %). Das Chunk-Streaming (sprint) ist gleich oder schneller: Tick Ø −5 %, Tick max −19 %, Upload gleich, Meshing pro Sektion +1 %; der neue Codec ist gleich schnell (Messprogramm oben), und `ChunkLabels` geht in normalen Welten nicht über die Leitung (keine Beschriftungen).
- **Tick max über der Schwelle in crowd (+19 %, Wiederholung +20 %), light (+14 %, +24 %) und liquids (+26 %, Wiederholung +1 %):** einzelne schlechteste Ticks von 1,6–5,7 ms, die Spannen überlappen in allen Fällen; in stand (−8 %), sprint (−19 %) und glass (−13 %) liegt der neue Stand darunter. Pro Tick neu ist nur das Setzen der Spawn-Regel und die Abfrage `daylightCycle` (zwei Zuweisungen); Codec, Mob-Logik, Physik und Chunk-Streaming sind unverändert oder gleich schnell. Die Tick-Mittel liegen bei −9 % bis +9 %. Wie in den Etappen 11–20 beschrieben schwankt der schlechteste Tick stark (ein einzelner Tick pro Lauf, abhängig von Kern und Takt); gewertet nach der Regel „erst ein wiederholbarer Anstieg zählt“ ist nur crowd zweimal gleich gerichtet, bei +0,34–0,43 ms und weit unter dem Tick-Budget von 50 ms. Begründet in E-224, kein tieferes Profiling.
- **showcase:** Die Wesen der Gehege (ein dauerhaftes Wesen jeder Art, im Grundspiel sechs, alle in Sichtweite des Spielers) kosten den Server-Tick kaum: 0,23 ms gegen 0,40 ms in flat-crowd mit 80 Wesen; weit unter 1 ms, eine Ruhe-Regel für Gehege-Wesen ist nicht nötig. Die Beschriftungen (bis 48 Sichtstrahlen, QML-Texte) kosten 0,03 ms GUI-CPU pro Frame (+11 % gegenüber ausgeschaltet), Render-CPU und GPU gleich. GPU 0,80 ms bei 64 000 Dreiecken und 139 Draw-Calls (viel Glas, Teilformen, durchscheinende Flüssigkeitsbecken) liegt unter dem normalen Gelände (1,6–2,1 ms).
- **Frame p99 der neuen Szenarien:** In den ersten Runden 10,2–10,4 ms wie im Gelände; ab der zweiten Runde der ersten Baseline-Reihe lag p99 bei allen Szenarien bei 15,4–16,4 ms (Frame Ø 8,4–8,9 ms statt 8,33 ms, einzelne verpasste Display-Takte), auch in einem Kontroll-Lauf von stand im normalen Gelände (15,4 und 15,7 ms) direkt danach, während der WindowServer mit 45 % CPU lief (Bedienung des Rechners, kein Fremdprozess über der Grenze). Das ist der Zustand des Systems, nicht der Szenarien; p99 dieser Referenzwerte ist deshalb nur verschränkt zu vergleichen.

**Etappe 21b (Debug-Anzeige in Stufen, Phasen-Zähler, Offscreen-Screenshots), verschränkt gegen `dev` ed53f2e mit dem Stand 1bb9726 im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** Je Szenario und Runde drei Läufe: Basis (mit ihrer Vorgabe, der Debug-Zeile), neuer Stand mit `DEBUG_DISPLAY=off` und mit `compact` (Vorgabe im normalen Spiel). Im Benchmark misst der Frame-Monitor in beiden Stufen voll; kompakt unterscheidet sich von aus also nur durch Text und Frame-Abstand der Zeile, der Server-Pfad ist gleich (kompakt abonniert nichts, E-225). Die Phasen-Runden laufen in jedem Tick, auch bei ausgeschalteter Anzeige. Gelände, Wesen und Dreiecke gleich (in einem sprint-Lauf von „aus“ 21 statt 23 Wesen, Spawn-Zufall). Last (1-min-Mittel) 1,9–4,8, vor jedem Lauf kein Fremdprozess ≥ 80 % CPU, kein Lauf belastet. Angaben: Basis → aus / kompakt, Prozent gegen die Basis.

| Szenario | Frame p99 ms | GUI-CPU Ø ms | GUI-CPU p99 ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Speicher MB |
|----------|--------------|--------------|----------------|---------------|----------|-----------|-------------|-------------|
| stand | 15,8 → 15,9 / 15,9 | 0,403 [0,389–0,417] → 0,391 [0,386–0,400] (−3 %) / 0,402 [0,396–0,409] (0 %) | 0,75 → 0,69 (−9 %) / 0,82 (+9 %) | 1,65 → 1,62 / 1,65 | 1,87 → 1,85 / 1,86 | 0,627 [0,545–0,686] → 0,638 [0,576–0,679] (+2 %) / 0,758 [0,673–0,829] (+21 %) | 2,23 → 2,13 (−4 %) / 2,53 (+14 %) | 384 → 386 / 386 |
| sprint | 15,8 → 15,6 / 15,5 | 0,416 [0,415–0,417] → 0,415 [0,407–0,420] (0 %) / 0,416 [0,398–0,436] (0 %) | 1,03 → 0,88 (−14 %) / 0,98 (−5 %) | 1,69 → 1,71 / 1,70 | 2,26 → 2,26 / 2,28 | 0,791 [0,724–0,916] → 0,835 [0,725–0,936] (+6 %) / 0,837 [0,806–0,855] (+6 %) | 3,15 → 3,22 (+2 %) / 3,32 (+5 %) | 545 → 550 / 543 |
| crowd | 14,7 → 13,8 / 14,8 | 0,716 [0,701–0,725] → 0,696 [0,681–0,721] (−3 %) / 0,727 [0,715–0,745] (+2 %) | 1,31 → 1,26 / 1,34 | 1,40 → 1,39 / 1,41 | 2,25 → 2,24 / 2,25 | 0,952 [0,792–1,075] → 0,857 [0,782–1,004] (−10 %) / 0,951 [0,810–1,143] (0 %) | 2,33 → 2,36 (+1 %) / 2,79 (+20 %) | 386 → 388 / 392 |
| light | 14,7 → 16,1 / 15,9 | 0,475 [0,464–0,484] → 0,471 [0,467–0,476] (−1 %) / 0,464 [0,459–0,473] (−2 %) | 2,80 → 2,79 / 2,74 | 1,60 → 1,62 / 1,58 | 1,59 → 1,58 / 1,57 | 0,592 [0,544–0,639] → 0,666 [0,587–0,752] (+12 %) / 0,629 [0,536–0,754] (+6 %) | 3,34 → 3,56 (+7 %) / 2,98 (−11 %) | 391 → 396 / 394 |
| liquids | 16,0 → 16,1 / 16,1 | 0,399 [0,397–0,401] → 0,393 [0,388–0,396] (−2 %) / 0,405 [0,399–0,408] (+1 %) | 2,36 → 2,47 (+5 %) / 2,20 (−7 %) | 1,54 → 1,54 / 1,57 | 1,51 → 1,51 / 1,51 | 0,610 [0,599–0,616] → 0,644 [0,549–0,711] (+6 %) / 0,569 [0,539–0,600] (−7 %) | 2,22 → 2,22 (0 %) / 1,61 (−27 %) | 397 → 397 / 398 |
| glass | 15,9 → 15,9 / 15,9 | 0,440 [0,437–0,444] → 0,427 [0,425–0,428] (−3 %) / 0,440 [0,435–0,443] (0 %) | 1,46 → 1,26 (−14 %) / 1,57 (+8 %) | 1,64 → 1,60 / 1,64 | 2,00 → 1,99 / 1,99 | 0,735 [0,596–0,831] → 0,715 [0,659–0,794] (−3 %) / 0,595 [0,546–0,623] (−19 %) | 2,73 → 2,65 (−3 %) / 2,79 (+2 %) | 392 → 392 / 397 |
| showcase | 16,8 → 16,6 / 16,6 | 0,355 [0,349–0,361] → 0,357 [0,354–0,361] (+1 %) / 0,372 [0,368–0,380] (+5 %) | 0,73 → 0,62 (−15 %) / 0,79 (+9 %) | 1,17 → 1,19 / 1,22 | 0,90 → 0,91 / 0,91 | 0,262 [0,222–0,296] → 0,263 [0,222–0,313] (0 %) / 0,238 [0,222–0,251] (−9 %) | 1,53 → 1,10 (−28 %) / 1,99 (+30 %) | 259 → 256 / 258 |
| flat-crowd | 16,5 → 16,5 / 16,5 | 0,633 [0,627–0,637] → 0,620 [0,608–0,627] (−2 %) / 0,628 [0,617–0,634] (−1 %) | 1,15 → 1,14 / 1,16 | 0,80 → 0,79 / 0,79 | 0,89 → 0,88 / 0,90 | 0,428 [0,395–0,455] → 0,424 [0,375–0,454] (−1 %) / 0,409 [0,369–0,461] (−4 %) | 1,86 → 1,73 (−7 %) / 0,93 (−50 %) | 252 → 252 / 255 |

Wiederholung (stand, light, crowd, 3 Runden, gleiche Methode, Last 2,2–3,3): Tick Ø stand 0,747 → 0,810 (+8 %) / 0,613 (−18 %), light 0,604 → 0,653 (+8 %) / 0,684 (+13 %), crowd 0,899 → 0,851 (−5 %) / 0,961 (+7 %); Tick max stand 2,47 → 3,08 (+25 %) / 2,21 (−11 %), light 2,48 → 2,76 (+12 %) / 3,03 (+22 %), crowd 2,46 → 1,97 (−20 %) / 2,88 (+17 %); GUI-CPU, Render-CPU, GPU, Speicher wie oben (−3 bis +3 %). Dritte Reihe nur light, Basis gegen aus, 4 Runden (Last 2,1–2,8): Tick Ø 0,567 [0,551–0,584] → 0,562 [0,548–0,579] (−1 %), Tick max 2,55 → 3,19 (+25 %). Über alle Reihen (light 10 Runden, stand und crowd 6): Tick Ø stand +5 % / 0 %, crowd −8 % / +3 %, light +6 % / +12 % (kompakt 6 Runden); Tick max stand +11 % / +1 %, crowd −10 % / +18 %, light +15 % / +9 %.

Phasen (neuer Stand, Mittel aller Läufe, ms pro Tick): Wesen 0,41–0,73 in den Szenarien mit Gelände (showcase 0,07, flat-crowd 0,21), Chunk-Streaming 0,08–0,11, Client-Abgleich 0,04–0,07, Wachstum 0,02–0,03, Items 0,004–0,02, alle übrigen Phasen ≤ 0,005; Nachrichten zwischen den Ticks 0,01–0,02. Die Summe der Phasen ist die Tick-Dauer. Der schlechteste Tick liegt in allen 40 ausgewerteten Läufen von stand, light und crowd in der Phase Wesen (1,35–4,16 ms von 1,65–4,35 ms).

**Bewertung Etappe 21b:**

- **Client:** GUI-CPU Ø liegt mit ausgeschalteter Anzeige 0–3 % unter der Basis (deren Debug-Zeile entfällt), kompakt −2 bis +5 % (showcase +5 %, eine Zeile mit Frame-Maximum statt der alten Zeile). GUI-CPU p99 kompakt bis +9 % (stand, showcase; 0,07 ms), aus 9–15 % darunter. Render-CPU, GPU und Frame p99 in der Streuung (−8 bis +9 %; light-p99 +9 %/+8 % kommt aus einem Basislauf mit 12,4 ms, in der Wiederholung −2 %/−3 %). Frame p99 lag in dieser Messung bei allen drei Ständen meist bei 15,5–16,9 ms statt 10,3 ms (verpasste Display-Takte, Frame Ø 8,4–9,2 ms; wie bei den Referenzwerten von Etappe 21 ein Zustand des Systems, gleich für alle Stände). Speicher 0 bis +2 % (+2 bis +6 MB: neue QML-Seiten, Debug-Texte, Phasenfenster).
- **Tick Ø:** Kein Mittel dauerhaft über der Schwelle. Einzelne Reihen liegen darüber (stand kompakt +21 %, in der Wiederholung −18 %; light aus +12 %, dann +8 % und −1 %; light kompakt +6 %, dann +13 %), andere ebenso weit darunter (glass kompakt −19 %, crowd aus −10 %). Aus und kompakt laufen auf dem Server denselben Code und unterscheiden sich in derselben Reihe um bis zu 26 Prozentpunkte (stand in beiden Reihen, glass); das ist die Streuung der Messung, nicht der Code. Neu pro Tick sind 15 Uhrablesungen der Phasen-Runden (21 ns pro Runde, E-225), ein Eintrag ins Phasenfenster (15 Werte), die Phasen-Summen unter dem Mutex der Tick-Statistik und eine Zuweisung pro Wesen (`chasing`): zusammen unter 1 µs, unter 0,2 % eines Ticks von 0,6 ms. Wesen dominieren den Tick (70–80 %), ihre Logik ist bis auf die Zuweisung unverändert, und schwankt allein im neuen Stand zwischen Läufen um ±25 % (stand 0,42–0,70 ms).
- **Tick max über der Schwelle (stand +11 %, light +15 % mit aus; crowd +18 % mit kompakt):** einzelne schlechteste Ticks, jeweils in der Phase Wesen, Spannen überlappen, die jeweils andere Stufe liegt bei +1 % (stand), +9 % (light) und −10 % (crowd). Begründet in E-230 wie schon E-224, kein tieferes Profiling.
- **Phasen in der BENCH-Zeile:** Seit Etappe 21b stehen die Zeitanteile des Server-Ticks in jeder BENCH-Zeile (`tick_<phase>_avg_ms`, `tick_<phase>_max_ms` für `players`, `saving`, `chunkLoading`, `blockUpdates`, `liquids`, `mobs`, `items`, `plants`, `fallingBlocks`, `machines`, `streaming`, `clientSync` und `messages`; E-225). Künftige A/B-Vergleiche grenzen Tick-Abweichungen damit direkt auf eine Phase ein; ältere Stände haben diese Schlüssel nicht, der Vergleich mit ihnen bleibt beim ganzen Tick.

**Etappe 22 (Charakter-Vorschau, Protokoll 21 mit Rüstungsfeldern, Profilprüfung), verschränkt gegen `dev` 78618f4 mit dem Stand 3cea050 (Etappe 22 plus Benchmark-Haken) im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** Skript- und Hintergrundläufe haben ein inaktives Fenster, in dem Vorschauen nur ein Standbild zeichnen (E-231). Für die Messung lässt `QTCRAFT_PREVIEW_ACTIVE=1` (`PREVIEW_ACTIVE=1` in `bench-common.sh`) sie wie im aktiven Fenster animiert laufen (30 Bilder pro Sekunde im Inventar); die BENCH-Zeile nennt dazu die Vernetzung der Vorschauen (`preview_mesh_avg_ms`, `preview_mesh_max_ms`). Im Szenario inventory läuft der neue Stand animiert („neu“) und als Standbild („still“), die Basis hat keine Vorschau. Gelände, Wesen und Dreiecke gleich (±25 Dreiecke im sprint). Last (1-min-Mittel) 2,0–5,1, vor jedem Lauf kein Fremdprozess ≥ 80 % CPU, kein Lauf belastet.

| Szenario | Frame p99 ms | GUI-CPU Ø ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Speicher MB |
|----------|--------------|--------------|---------------|----------|-----------|-------------|-------------|
| stand | 9,88 → 9,84 | 0,427 [0,366–0,468] → 0,409 [0,349–0,452] (−4 %) | 1,44 → 1,39 | 1,83 [1,74–1,94] → 1,74 [1,60–1,90] (−5 %) | 0,510 [0,471–0,552] → 0,507 [0,433–0,570] (−1 %) | 1,52 → 1,78 (+17 %) | 384 → 389 |
| sprint | 9,83 → 9,92 | 0,417 [0,378–0,438] → 0,409 [0,355–0,437] (−2 %) | 1,43 → 1,44 | 2,03 [1,85–2,38] → 2,04 [1,86–2,38] (+1 %) | 0,583 [0,527–0,677] → 0,556 [0,438–0,623] (−5 %) | 2,27 → 2,37 (+4 %) | 545 → 550 |
| crowd | 9,76 → 9,74 | 0,729 [0,693–0,766] → 0,728 [0,683–0,768] (0 %) | 1,25 → 1,22 | 2,41 [2,29–2,57] → 2,35 [2,16–2,56] (−3 %) | 0,655 [0,612–0,711] → 0,676 [0,572–0,794] (+3 %) | 1,34 → 2,00 (+49 %) | 389 → 391 |
| light | 10,19 → 9,91 | 0,481 [0,419–0,514] → 0,499 [0,448–0,538] (+4 %) | 1,42 → 1,35 | 1,41 [1,33–1,51] → 1,43 [1,32–1,48] (+1 %) | 0,443 [0,382–0,514] → 0,500 [0,361–0,625] (+13 %) | 2,03 → 2,67 (+32 %) | 393 → 395 |
| liquids | 9,89 → 9,93 | 0,434 [0,396–0,455] → 0,420 [0,351–0,457] (−3 %) | 1,37 → 1,35 | 1,35 [1,29–1,45] → 1,34 [1,29–1,42] (−1 %) | 0,467 [0,416–0,504] → 0,470 [0,366–0,534] (+1 %) | 2,87 → 1,38 (−52 %) | 398 → 402 |
| glass | 9,81 → 10,55 (+8 %) | 0,450 [0,400–0,480] → 0,447 [0,392–0,478] (−1 %) | 1,41 → 1,42 | 1,93 [1,73–2,13] → 1,85 [1,69–2,04] (−4 %) | 0,529 [0,486–0,571] → 0,491 [0,412–0,590] (−7 %) | 1,97 → 2,15 (+9 %) | 396 → 400 |
| showcase | 12,19 → 12,23 | 0,408 [0,324–0,452] → 0,387 [0,314–0,453] (−5 %) | 1,15 → 1,12 | 0,78 → 0,78 | 0,199 [0,184–0,211] → 0,221 [0,197–0,253] (+11 %) | 0,58 → 1,55 (+168 %) | 255 → 259 |
| flat-crowd | 12,20 → 12,25 | 0,633 [0,549–0,706] → 0,658 [0,551–0,729] (+4 %) | 0,75 → 0,75 | 0,82 → 0,83 (+2 %) | 0,324 [0,310–0,335] → 0,360 [0,294–0,457] (+11 %) | 1,33 → 1,24 (−7 %) | 253 → 254 |
| inventory | 10,27 → 10,39 / still 10,11 | 0,413 [0,339–0,457] → 0,479 [0,421–0,524] (+16 %) / still 0,382 [0,345–0,453] (−8 %) | 1,57 → 1,52 / still 1,56 | 2,02 [2,00–2,04] → 1,93 [1,72–2,07] (−5 %) / still 2,06 [2,03–2,09] (+2 %) | 0,561 [0,458–0,760] → 0,474 [0,368–0,534] (−16 %) / still 0,540 (−4 %) | 2,37 → 1,60 (−33 %) / still 1,75 | 389 → 392 / still 391 |

Wiederholung (light, showcase, flat-crowd, crowd, glass, inventory, 3 Runden direkt danach, Last 2,0–4,8, kein Lauf belastet): Tick Ø light −2 %, showcase −3 %, flat-crowd −3 %, crowd +14 %, glass 0 %, inventory neu +3 % / still +33 %; Tick max light +10 %, showcase +18 %, flat-crowd −28 %, crowd +1 %, glass −10 %; Frame p99 glass 10,21 → 10,43 (+2 %), sonst −11 bis +2 %; GPU, Render-CPU, GUI-CPU außerhalb des Inventars −2 bis +4 %. Inventar: GUI-CPU Ø 0,320 [0,294–0,344] → 0,400 [0,368–0,430] (+25 %) / still 0,328 (+2 %), GUI-CPU p99 0,685 → 0,932 (+36 %) / still 0,694 (+1 %), GPU 2,02 → 2,01 / still 2,00, Render-CPU 1,38 → 1,43 / still 1,41, Speicher 388 → 396 / still 390 MB. Über beide Reihen (6 Läufe je Stand): Tick Ø stand −1 %, crowd +8 %, light +5 %, glass −3 %, showcase +4 %, flat-crowd +5 %, inventory −7 % (still +13 %); Tick max crowd +22 %, light +20 %, showcase +105 % (ein Lauf mit 2,77 ms), glass −2 %, flat-crowd −16 %.

**Kosten der Vorschau (nur neuer Stand):**

- **Inventar, animiert gegen Standbild** (6 Läufe je Stand aus beiden Reihen): GUI-CPU Ø 0,355 → 0,440 ms pro Spiel-Frame (+0,085 ms, +24 %), GUI-CPU p99 0,717 → 0,991 ms (+0,27 ms); davon Vernetzung der Figur `preview_mesh_avg_ms` 0,008 ms pro Spiel-Frame (0,03 ms pro Vorschau-Bild, höchstens 0,11–0,14 ms). Render-CPU, GPU und Frame p99 unverändert (GPU 2,03 → 1,97 ms, Render-CPU 1,49 → 1,47 ms, p99 10,3 → 10,2 ms): Ein Bild von 156 × 256 Punkten mit einigen hundert Eckpunkten und einem Draw-Call fällt im GPU-Mittel nicht auf. Ein einzelner Probelauf vor der Reihe zeigte GPU +0,6 ms (+36 %); in den sechs verschränkten Läufen trat das nicht wieder auf (Taktzustand der GPU). Spitzenspeicher 390 → 394 MB (+4 MB; Reihe 1 +1, Reihe 2 +6 MB). Das Standbild kostet gegen die Basis nichts Messbares (GUI-CPU −3 %, GPU −1 %, Speicher +2 MB).
- **Hauptmenü** (Profilkarte, 15 Bilder pro Sekunde; kein Benchmark-Pfad, das Menü hat keine Welt und keine BENCH-Zeile): CPU-Zeit des ganzen Prozesses (alle Threads, `ps`) über 20 s nach 10 s Anlauf und die GPU-Auslastung des Systems (`ioreg`, „Device Utilization %“, mit WindowServer und anderen Programmen) im Mittel über dieselben 20 s; je 3 Läufe verschränkt (Basis ohne Profilkarte, neuer Stand als Standbild und animiert), Last 2,3–3,2. Basis 0,0 % CPU, 4,7 % GPU, 156 MB; Standbild 0,0 % CPU, 5,3 % GPU, 162 MB; animiert 3,6 % eines Kerns (36 ms CPU pro Sekunde, in allen drei Läufen gleich; das Fenster zeichnet nur für die Vorschau neu, also rund 2,4 ms pro Bild für den ganzen Menü-Frame in GUI- und Render-Thread), 7,3 % GPU (+2–3 Prozentpunkte), 162 MB. Im Menü steht das Fenster sonst still; die Vorschau ist das Einzige, was es bewegt.

**Bewertung Etappe 22:**

- **Ohne Vorschau keine Verschlechterung über der Schwelle:** Frame p99, GUI- und Render-CPU, GPU und Speicher liegen in allen Szenarien ohne offene Vorschau zwischen −5 % und +8 % (glass p99 +8 %, in der Wiederholung +2 %), Speicher +1 bis +5 MB (+1 %: Vorschau-Code, QML-Seiten, Profilprüfung). Server-Code geändert sind nur `EntitySpawn` mit Ausrüstung (nur für andere Spieler, im Einzelspieler nicht im Tick) und die Profilprüfung bei `Hello`/`SetAppearance`.
- **Tick Ø:** Einzelne Reihen über der Schwelle (light +13 %, showcase +11 %, flat-crowd +11 % in der ersten, crowd +14 % in der zweiten Reihe), jeweils in der anderen Reihe −3 bis +3 %; über beide Reihen höchstens +8 %. Die Phasen steigen gleichmäßig (light Reihe 1: Wesen +13 %, Streaming +8 %, Client-Abgleich +17 %, Wachstum +14 %), wie bei einem anderen Takt des Kerns; der Code der Phasen ist unverändert. Derselbe Build im selben Szenario (inventory neu gegen still, Server identisch) unterscheidet sich in der zweiten Reihe um 30 % (Phase Wesen 0,36 gegen 0,47 ms). Streuung, keine Regression (E-233).
- **Tick max über der Schwelle** (crowd +22 %, light +20 %, showcase +105 %, stand +17 % über die Reihen): einzelne schlechteste Ticks, jeweils in der Phase Wesen, Spannen überlappen; showcase hat in der Basis sehr kleine Werte (0,4–0,7 ms), ein einzelner Lauf mit 2,77 ms trägt den Anstieg, die Wiederholung liegt bei +18 %. Wie E-224 und E-230, begründet in E-233.
- **Inventar mit animierter Vorschau über der Schwelle (GUI-CPU Ø +20 bis +25 %, p99 +36 %):** echte, gewollte Kosten des neuen Elements, wiederholbar, nur solange das Inventar im aktiven Fenster offen ist. Absolut +0,07–0,09 ms pro Frame (p99 +0,27 ms) bei einem Budget von 8,33 ms; GPU, Render-CPU und Frame p99 unverändert. Die Vernetzung der Figur erklärt nur ein Zehntel davon; der Rest fällt im GUI-Thread pro Vorschau-Bild an (rund 0,25 ms; vermutlich das zusätzliche Aufwachen der Ereignisschleife durch den eigenen 30-Hz-Timer und `update()` des `QQuickRhiItem`, nicht einzeln gemessen). Ein `sample` des GUI-Threads über 8 s (1-ms-Raster) findet keinen einzelnen Schwerpunkt. Begründet in E-233, keine Änderung.

**Etappe 23 (Rüstung, Rüstungsständer, Umhang), verschränkt gegen `dev` c8b3f27 mit dem Stand 012a718 im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** inventory mit `PREVIEW_ACTIVE=1` (animierte Vorschau in beiden Ständen). Neu pro Frame sind die Anbauteile und der Umhang je Figur (in den alten Szenarien trägt niemand Rüstung oder Umhang; geprüft werden nur die Nachschläge pro Figur), die HUD-Rüstungsleiste (unsichtbar ohne Rüstung) und die Namen der Ständer bei den Beschriftungen; neu pro Tick die Schleife über die Ständer im Client-Abgleich und in der Kampf-Phase (ohne Ständer leer) sowie Minderung und Abnutzung bei Treffern. Gelände, Wesen und Dreiecke gleich, außer showcase (siehe unten). Last (1-min-Mittel) 2,3–4,6, vor jedem Lauf kein Fremdprozess ≥ 80 % CPU, kein Lauf belastet.

| Szenario | Frame p99 ms | GUI-CPU Ø ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Entity-Vernetzung ms | Speicher MB |
|----------|--------------|--------------|---------------|----------|-----------|-------------|----------------------|-------------|
| stand | 9,95 → 9,90 | 0,336 [0,315–0,348] → 0,367 [0,346–0,386] (+9 %) | 1,32 → 1,36 (+3 %) | 1,77 → 1,76 | 0,545 [0,520–0,573] → 0,580 [0,545–0,622] (+6 %) | 1,73 → 1,85 (+7 %) | 0,129 → 0,139 (+8 %) | 386 → 391 |
| sprint | 9,95 → 9,92 | 0,353 [0,341–0,375] → 0,368 [0,348–0,407] (+4 %) | 1,37 → 1,39 | 2,23 → 2,20 | 0,619 [0,604–0,635] → 0,672 [0,566–0,828] (+9 %) | 2,52 → 2,74 (+9 %) | 0,132 → 0,138 (+5 %) | 548 → 553 |
| crowd | 11,72 [9,48–15,48] → 10,58 [9,49–11,92] (−10 %) | 0,609 [0,596–0,617] → 0,646 [0,638–0,651] (+6 %) | 1,17 → 1,17 | 2,20 → 2,17 | 0,660 [0,616–0,721] → 0,729 [0,637–0,846] (+11 %) | 1,64 → 2,29 (+39 %) | 0,363 → 0,381 (+5 %) | 394 → 398 |
| light | 10,52 [9,60–11,01] → 11,77 [10,09–14,03] (+12 %) | 0,426 → 0,416 (−2 %) | 1,32 → 1,33 | 1,40 → 1,43 (+2 %) | 0,509 → 0,527 (+4 %) | 1,97 → 2,16 (+10 %) | 0,153 → 0,152 | 397 → 400 |
| liquids | 10,51 → 10,46 | 0,365 → 0,382 (+5 %) | 1,30 → 1,33 | 1,37 → 1,37 | 0,500 → 0,501 (0 %) | 1,35 → 1,55 (+15 %) | 0,129 → 0,133 (+3 %) | 401 → 405 |
| glass | 10,38 → 10,49 | 0,379 → 0,402 (+6 %; p99 0,93 → 1,05, +13 %) | 1,35 → 1,37 | 1,94 → 1,90 (−2 %) | 0,537 → 0,489 (−9 %) | 2,03 → 2,30 (+14 %) | 0,154 → 0,164 (+6 %) | 398 → 400 |
| showcase | 15,89 → 15,98 | 0,332 [0,327–0,336] → 0,379 [0,377–0,381] (+14 %) | 1,05 → 1,04 | 0,80 → 0,63 (−22 %) | 0,219 → 0,207 (−5 %) | 0,60 → 0,66 (+10 %) | 0,061 → 0,109 (+78 %) | 262 → 266 |
| flat-crowd | 15,96 → 15,93 | 0,548 → 0,552 (+1 %) | 0,69 → 0,67 | 0,83 → 0,83 | 0,368 [0,324–0,428] → 0,339 [0,287–0,377] (−8 %) | 1,41 → 1,31 (−7 %) | 0,293 → 0,300 (+3 %) | 252 → 258 |
| inventory | 10,21 → 10,30 | 0,413 → 0,396 (−4 %; p99 0,965 → 0,865, −10 %) | 1,48 → 1,54 (+4 %) | 1,90 → 1,91 | 0,531 → 0,517 (−3 %) | 1,83 → 1,59 (−13 %) | 0,130 → 0,132 | 393 → 397 |

showcase ist eine andere Szene: Das neue Gebiet „Rüstungssätze“ verschiebt die Gebiete der Showcase-Welt, vom Standpunkt aus sind zwei Wesen mehr (Ständer mit Rüstung, 6 → 8), 16 % mehr Dreiecke (64 200 → 74 500) und 144 statt 128 sichtbare Sektionen im Bild; die Werte sind nicht als A/B zu lesen.

Wiederholung (stand, sprint, crowd, light, glass, 3 Runden direkt danach, Last 2,8–4,2, kein Lauf belastet): Tick Ø stand −7 %, sprint −13 %, crowd −12 %, light −2 %, glass −2 %; Tick max −28 %, −19 %, −37 %, −4 %, −29 %; GUI-CPU Ø +3 %, 0 %, +4 %, +4 %, +3 %; GUI-CPU p99 −1 bis +4 %; Render-CPU, GPU, Speicher −2 bis +4 %; Frame p99 stand +17 % (ein Lauf mit 15,6 ms, währenddessen `deleted` mit 75 % CPU), glass +13 % (ein Lauf mit 15,4 ms), crowd −12 %, light −17 %, sprint −3 %. Über beide Reihen (6 Läufe je Stand): Tick Ø stand −1 %, sprint −3 %, crowd −2 %, light +1 %, glass −5 %; Tick max −14 %, −7 %, −10 %, +2 %, −14 %; GUI-CPU Ø +6 %, +2 %, +5 %, +1 %, +5 %; GUI-CPU p99 +4 %, −2 %, +5 %, +3 %, +6 %.

**Neue Szenarien (nur Etappe 23, erste Referenzwerte), verschränkt mit den Vergleichsszenarien, je 3 Läufe à 20 s, 8 Chunks, Mittel [Spanne], Last 2,3–4,0, kein Lauf belastet:** **armour-crowd** ist flat-crowd (in der flachen Welt) plus 40 Rüstungsständer mit vollem Diamantsatz und grünem Umhang (`/summon armor_stand equip=diamond cape=green_cape arms`, alle auf derselben Zelle 3 m vor dem Spieler; mit Kopf-Füllung unter dem Helm) und 8 Gliederpuppen mit Umhang (`qtcraft:caped_mannequin`, ohne Rüstung): 48 zusätzliche Figuren. **armour-walk** ist walk mit dem eigenen Spieler in vollem Diamantsatz mit Umhang, von hinten gesehen (`--camera behind`, `/equip diamond`, `character/cape`), also mit Figur in dritter Person und schwingendem Umhang.

| Szenario | Frame p99 ms | GUI-CPU Ø ms | GUI-CPU p99 ms | Render-CPU ms | GPU Ø ms | Entity-Vernetzung Ø / p99 ms | Upload MB / 20 s | Tick Ø ms | Client-Abgleich ms | Wesen | Dreiecke | Speicher MB |
|----------|--------------|--------------|----------------|---------------|----------|------------------------------|------------------|-----------|--------------------|-------|----------|-------------|
| flat-crowd | 15,8 [15,7–15,9] | 0,560 [0,553–0,570] | 0,97 | 0,680 | 0,838 | 0,306 / 0,54 | 931 | 0,354 [0,296–0,457] | 0,059 | 80 | 49 900 | 262 |
| armour-crowd | 11,7 [10,3–12,6] | 1,441 [1,429–1,454] | 2,06 | 0,802 | 1,069 | 1,203 / 1,63 | 3926 | 0,300 [0,286–0,309] | 0,055 | 128 | 64 400 | 272 |
| walk | 9,60 | 0,336 [0,318–0,363] | 0,72 | 1,43 | 1,91 | 0,103 / 0,19 | 244 | 0,636 | 0,045 | 7 | 521 900 | 536 |
| armour-walk | 9,70 | 0,370 [0,355–0,394] | 0,76 | 1,52 | 1,93 | 0,130 / 0,25 | 309 | 0,557 | 0,038 | 7 | 543 400 | 535 |

**Kosten pro gerüsteter Figur** (armour-crowd gegen flat-crowd, 48 Figuren, davon 40 Ständer mit Rüstung und Umhang): GUI-CPU +0,88 ms pro Frame, also rund **18 µs pro Figur**, fast vollständig Entity-Vernetzung (+0,90 ms, 19 µs pro Figur); Render-CPU +0,12 ms (2,5 µs pro Figur), GPU +0,23 ms (5 µs pro Figur); rund 300 Dreiecke pro Figur (+14 500); Upload der Entity-Puffer +1,2 MB pro Frame (≈ 25 KB pro Figur, die Figuren werden jeden Frame neu vernetzt und hochgeladen); Spitzenspeicher +10 MB. Zum Vergleich kostet ein Wesen ohne Rüstung in flat-crowd rund 4–5 µs Vernetzung bei rund 60 Dreiecken; die Kosten wachsen also mit der Geometrie, nicht mit einem eigenen Schwerpunkt. Der Server merkt die Ständer nicht: Tick Ø und Client-Abgleich (die Schleife über alle Ständer pro Client und Tick) liegen bei armour-crowd sogar etwas unter flat-crowd (Streuung). Der eigene Spieler in Rüstung mit Umhang in dritter Person (armour-walk gegen walk) kostet +0,034 ms GUI-CPU (+10 %), davon +0,027 ms Vernetzung (eine Figur wie oben), Render-CPU +0,09 ms (+6 %) und 383 statt 372 Draw-Calls mit +21 500 Dreiecken, weil die Kamera hinter dem Spieler mehr Gelände sieht; GPU +1 %, Frame p99 gleich.

**Bewertung Etappe 23:**

- **Keine Verschlechterung über der Schwelle in den Vergleichsszenarien.** Frame p99, Render-CPU, GPU und Speicher liegen zwischen −10 % und +4 % (Frame p99 einzelner Läufe zweigeteilt bei 10 bzw. 15–16 ms, in beiden Ständen), Speicher +3 bis +6 MB (+1 %: Rüstungs-Texturen, Umhang, Ständer-Code). GUI-CPU Ø liegt über beide Reihen 1–6 % über der Basis (+0,01–0,03 ms); davon ist ein Drittel bis die Hälfte die Entity-Vernetzung (+2 bis +8 %, +0,005–0,015 ms: pro Figur und Frame die Nachschläge für Rüstung, Umhang und Rüstungslücke, auch ohne Ausrüstung); einzelne Reihen stehen bei +9 % (stand), GUI-CPU p99 glass +13 % in der ersten Reihe, in der Wiederholung −1 %.
- **Tick Ø:** crowd +11 % in der ersten Reihe (Phase Wesen +11 %), in der Wiederholung −12 %, über beide Reihen −2 %; alle anderen Mittel über beide Reihen −5 bis +1 %. Der Code der Phase Wesen ist bis auf die Rüstungsminderung bei Treffern (nur mit getragener Rüstung) und eine leere Schleife über die Ständer unverändert. **Tick max** einzelner Reihen über der Schwelle (crowd +39 %, liquids +15 %, glass +14 %), in der Wiederholung durchweg darunter (−4 bis −37 %): Streuung wie in E-224, E-230 und E-233.
- **showcase GUI-CPU +14 %, Entity-Vernetzung +78 %:** die neuen Ständer mit Rüstung im Bild (2 Figuren, +0,048 ms, also rund 24 µs pro Figur wie in armour-crowd) und eine andere Szene (+16 % Dreiecke); gewollter Inhalt, keine Regression.
- **Gerüstete Figuren** kosten rund 18 µs GUI-CPU pro Figur (40 Ständer und 8 Gliederpuppen mit Umhang: +0,88 ms bei einem Budget von 8,33 ms) und skalieren mit ihrer Geometrie. Ein Zwischenspeichern der Netze ruhender Ständer (nur der Umhang schwingt) wäre der nächste Schritt, falls Welten mit vielen Ständern später stören. Begründet in E-242, keine Änderung am Code.

**Etappe 24 (Server-Befehle, Operator-Rechte, `/tick`, Spielerzeit für Budgets, Grenzen und Autosave), verschränkt gegen `dev` 2333efd mit dem Stand ffc8fcc im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** Neu pro Tick sind die Spielerzeit (`QcTickScheduler::tickRealNs`, `playerTimeNs`), das pro Nanosekunde aufgefüllte Eingabe- und Ratenbudget und die Prüfung, ob Welt und Zeit an die Clients gehen (`clientSyncDue`); neu pro Chat-Zeile die Befehls-Registry mit Rechteprüfung und einer Log-Zeile pro Befehl (E-243); im Client die Aufteilung von `QcClientSession` (`QcClientMachineScreen`, `QcClientSounds`) und die Tagesgeschwindigkeit aus `WorldTime`. Das Szenario liquids schickt im neuen Stand nach dem großen `/fill` ein `/confirm` (E-245; die Basis antwortet darauf „Unknown command“); beide Stände füllen dieselbe Fläche (gleiche Dreiecke 472 900, 175 Lichtjobs, 1997 neu vernetzte Sektionen). Gelände, Wesen und Dreiecke gleich. Last (1-min-Mittel) 2,4–5,3, vor jedem Lauf kein Fremdprozess ≥ 80 % CPU, kein Lauf belastet.

| Szenario | Frame p99 ms | GUI-CPU Ø ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Phase Nachrichten Ø ms | Speicher MB |
|----------|--------------|--------------|---------------|----------|-----------|-------------|------------------------|-------------|
| stand | 9,99 → 9,97 | 0,281 [0,237–0,305] → 0,303 [0,289–0,315] (+8 %) | 0,87 → 0,91 (+5 %) | 1,70 → 1,68 (−1 %) | 0,451 [0,400–0,497] → 0,424 [0,398–0,453] (−6 %) | 1,48 → 1,47 (0 %) | 0,0059 → 0,0054 | 389 → 389 |
| sprint | 9,75 → 9,99 | 0,268 [0,222–0,313] → 0,294 [0,274–0,314] (+10 %) | 0,89 → 0,92 (+3 %) | 2,15 → 2,14 | 0,505 [0,426–0,582] → 0,498 [0,462–0,563] (−1 %) | 2,86 → 3,22 (+12 %) | 0,0072 → 0,0061 | 555 → 553 |
| crowd | 9,89 → 9,94 | 0,487 [0,449–0,515] → 0,511 [0,497–0,521] (+5 %) | 0,67 → 0,71 (+7 %) | 2,12 → 2,13 | 0,571 [0,519–0,598] → 0,553 [0,535–0,574] (−3 %) | 1,63 → 0,89 (−45 %) | 0,0001 → 0,0001 | 394 → 398 |
| light | 9,86 → 9,98 | 0,342 [0,307–0,367] → 0,361 [0,337–0,374] (+6 %) | 0,91 → 0,92 | 1,37 → 1,41 (+3 %) | 0,426 [0,406–0,440] → 0,415 [0,405–0,435] (−2 %) | 1,93 → 1,24 (−36 %) | 0,0146 → 0,0378 (+158 %) | 400 → 400 |
| liquids | 9,93 → 10,17 | 0,304 [0,254–0,343] → 0,284 [0,223–0,348] (−7 %) | 0,92 → 0,91 | 1,38 → 1,32 (−4 %) | 0,457 [0,407–0,535] → 0,411 [0,329–0,480] (−10 %) | 1,63 → 1,31 (−20 %) | 0,0098 → 0,0187 (+92 %) | 406 → 404 |
| glass | 10,29 → 10,31 | 0,275 [0,234–0,300] → 0,335 [0,329–0,338] (+22 %) | 0,84 → 0,90 (+7 %) | 1,80 → 1,82 (+1 %) | 0,403 [0,338–0,462] → 0,432 [0,405–0,470] (+7 %) | 1,17 → 1,37 (+17 %) | 0,0102 → 0,0164 (+61 %) | 400 → 401 |
| showcase | 9,88 → 10,18 | 0,254 [0,211–0,299] → 0,262 [0,243–0,283] (+3 %) | 0,65 → 0,63 (−2 %) | 0,60 → 0,60 | 0,173 [0,163–0,187] → 0,178 [0,160–0,199] (+3 %) | 0,65 → 0,87 (+33 %) | 0,0071 → 0,0066 | 266 → 263 |
| flat-crowd | 10,06 → 10,18 | 0,382 [0,315–0,442] → 0,394 [0,329–0,438] (+3 %) | 0,35 → 0,36 (+2 %) | 0,83 → 0,83 | 0,243 [0,199–0,275] → 0,249 [0,224–0,273] (+2 %) | 0,44 → 0,72 (+64 %) | 0,0002 → 0,0001 | 258 → 258 |
| armour-crowd | 9,84 → 10,30 (+5 %) | 1,079 [0,842–1,265] → 1,162 [0,933–1,287] (+8 %) | 0,43 → 0,44 (+4 %) | 1,05 → 1,06 | 0,245 [0,199–0,277] → 0,249 [0,192–0,282] (+2 %) | 0,94 → 0,62 (−34 %) | 0,0001 → 0,0002 | 274 → 271 |

Wiederholung (light, glass, stand, sprint, 3 Runden direkt danach, dazu der neue Stand mit `QT_LOGGING_RULES="qtcraft.commands.info=false"` („ohne Befehls-Log“), Last 2,5–4,3, kein Lauf belastet): GUI-CPU Ø light +7 % / ohne Log +2 %, glass +18 % / +21 % (0,283 [0,275–0,289] → 0,335 [0,328–0,343]), stand +4 % / 0 %, sprint +15 % / +12 %; Entity-Vernetzung glass +12 % / +12 %, sprint +10 % / +10 %, sonst −1 bis +3 %; Render-CPU −1 bis +13 %; Frame p99 0 bis +1 %; Tick Ø light −9 % / −12 %, glass −1 % / +12 %, stand −8 % / +3 %, sprint +1 % / −6 %; Tick max sprint +17 % / +13 % (Phase Wesen), sonst −26 bis +11 %. Phase Nachrichten light 0,0164 → 0,0367 (+124 %) / ohne Log 0,0132 (−19 %), glass 0,0118 → 0,0175 (+48 %) / 0,0114 (−3 %).

**Bewertung Etappe 24:**

- **Server-Tick ohne Verschlechterung:** Tick Ø liegt in allen Szenarien zwischen −10 % und +7 % (glass in der Wiederholung −1 %), die Phasen Wesen, Streaming und Client-Abgleich in der Streuung (Client-Abgleich −14 bis +14 %, absolut ±0,005 ms). Spielerzeit, Budget pro Nanosekunde und die Synchronisationsprüfung kosten nichts Messbares (Phase Spieler 0,001 ms wie zuvor).
- **Phase Nachrichten über der Schwelle (+48 bis +158 % in den Szenarien mit Befehlen):** Ursache ist die Log-Zeile, die jeder Befehl seit E-243 schreibt (`qtcraft.commands`, Absender, Stufe, Text): Mit abgeschaltetem Log liegt die Phase gleichauf mit der Basis (light −19 %, glass −3 %), die Befehls-Registry selbst kostet also nichts. Eine Log-Zeile kostet rund 0,05–0,1 ms (Formatieren und Ausgabe über den Qt-Standard-Handler), light schickt 4 Befehle pro Sekunde: +0,02 ms pro Tick im Mittel. Die schlechteste Nachrichtenphase und der Tick bleiben gleich. Gewollt (Protokoll der Befehle), begründet in E-250, keine Änderung.
- **GUI-CPU in glass +18 bis +22 % und sprint +10 bis +15 % (beide Reihen), Entity-Vernetzung +10 bis +16 %:** Der Code im Frame-Pfad ist unverändert (Entity-Vernetzung, Render-Brücke, Frame-Schritt); der Client ändert nur Aufgaben im 20-Hz-Takt (Maschinen-Bildschirm und Klänge in eigenen Klassen, Tageszeit mal `speed`). Ein `sample` des GUI-Threads (glass, je 2 Läufe, 12 s bei 1 ms) zeigt in beiden Ständen dieselben Proben in `QcGameController::onFrame` (145 gegen 136) und `QcRenderBridge::updateEntities` (111 gegen 108). Die GUI-CPU misst die Thread-CPU-Zeit pro Frame und hängt am Kern und seinem Takt; in stand, crowd, light, liquids, showcase und den flachen Szenarien liegt sie bei −7 bis +8 %. Absolut +0,03–0,06 ms bei 8,33 ms Budget; Render-CPU, GPU und Frame p99 unverändert. Begründet in E-250.
- **Tick max einzelner Szenarien über der Schwelle** (sprint +12/+17 % in beiden Reihen, Phase Wesen, deren Code unverändert ist; flat-crowd +64 % und showcase +33 % in einer Reihe, jeweils ein Lauf mit Streaming bzw. Chunk-Laden über 1 ms): einzelne schlechteste Ticks wie in E-224, E-230, E-233 und E-242; dafür crowd −45 %, light −36 %, armour-crowd −34 %.
- **Speicher** −3 bis +4 MB, **Frame p99** 9,8–10,3 ms in beiden Ständen (armour-crowd +5 %: alle drei Läufe des neuen Stands bei 10,3 ms, die Zweiteilung 9,3 gegen 10,3 ms je Lauf).
- **Werkzeugwerte von `/tick`** (Sprint-Durchsatz, Nachrichten an den Client im Sprint, Kosten von Rate 100) stehen in `docs/Performance.md` unter „Tick-Steuerung als Messwerkzeug“. Nebenbefund: Klänge gehen im Sprint ungedrosselt an den Client (rund 400 in 0,3 s im Gedränge), anders als Welt, Entities und Zeit (E-249); für die Leistung unerheblich, aber hörbar, falls der Ton nicht stumm ist.

**Etappe 25 (Multiplayer: TCP-Transport, `QcNetworkServer`, Anmeldung, Protokoll 27 mit `EntityMoves.timeMs`, Interpolationspuffer, Spielerliste, Klartext-Beschriftungen, gebündelte Identitäten), verschränkt gegen `dev` 545237f mit dem Stand 680f7cb im Release-Build, je 3 Runden à 20 s, 8 Chunks, Mittel [Spanne]:** Neu pro Client-Tick (20 Hz) ist der Interpolationspuffer für jede fremde Figur (`QcSnapshotClock`, `QcSnapshotBuffer`, E-271); er ist auch im Einzelspieler eingeschaltet, crowd, flat-crowd und armour-crowd (80–128 Wesen) messen ihn also mit. Neu pro Server-Tick sind der Ablauf offener Anmeldungen, die Prüfung, ob `identities.json` fällig ist (E-273), die Abfrage laufender Sicherungen und die Spielerliste bei Änderungen (alles in der Phase Spieler); `QcNetworkServer` lauscht nur im Mehrspieler und läuft in den Szenarien nicht. Gelände, Wesen und Dreiecke gleich. Last (1-min-Mittel) 2,5–5,9, vor jedem Lauf kein Fremdprozess ≥ 80 % CPU, kein Lauf belastet. Frame p99 lag in dieser Messung bei beiden Ständen meist bei 12–16 ms statt 10 ms (verpasste Display-Takte wie in Etappe 21b, ein Zustand des Systems, gleich für beide Stände).

| Szenario | Frame p99 ms | GUI-CPU Ø ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Phase Nachrichten Ø ms | Speicher MB |
|----------|--------------|--------------|---------------|----------|-----------|-------------|------------------------|-------------|
| stand | 15,71 → 15,56 (−1 %) | 0,388 [0,376–0,395] → 0,376 [0,371–0,380] (−3 %) | 1,53 → 1,50 (−2 %) | 1,82 → 1,81 (0 %) | 0,698 [0,592–0,812] → 0,588 [0,568–0,604] (−16 %) | 2,06 → 2,14 (+4 %) | 0,0113 → 0,0119 | 392 → 392 |
| sprint | 11,68 → 11,76 (+1 %) | 0,395 [0,380–0,415] → 0,390 [0,383–0,401] (−1 %) | 1,58 → 1,56 (−2 %) | 2,26 → 2,27 (0 %) | 0,778 [0,705–0,862] → 0,708 [0,594–0,767] (−9 %) | 3,84 → 4,11 (+7 %) | 0,0132 → 0,0131 | 555 → 556 |
| crowd | 13,82 → 13,76 (0 %) | 0,691 [0,681–0,700] → 0,694 [0,686–0,700] (0 %) | 1,27 → 1,32 (+3 %) | 2,16 → 2,20 (+2 %) | 0,831 [0,695–1,023] → 0,781 [0,672–0,864] (−6 %) | 2,32 → 2,15 (−7 %) | 0,0002 → 0,0003 | 397 → 401 |
| light | 12,87 → 14,02 (+9 %) | 0,469 [0,447–0,504] → 0,457 [0,438–0,488] (−3 %) | 1,43 → 1,45 (+1 %) | 1,40 → 1,44 (+3 %) | 0,567 [0,521–0,653] → 0,677 [0,582–0,798] (+19 %) | 2,20 → 3,19 (+45 %) | 0,0444 → 0,0430 | 401 → 402 |
| liquids | 12,52 → 12,47 (0 %) | 0,387 [0,382–0,395] → 0,406 [0,378–0,441] (+5 %) | 1,44 → 1,42 (−1 %) | 1,42 → 1,39 (−2 %) | 0,579 [0,529–0,627] → 0,666 [0,551–0,739] (+15 %) | 2,31 → 2,35 (+2 %) | 0,0290 → 0,0267 | 404 → 406 |
| glass | 13,64 → 15,61 (+14 %) | 0,418 [0,406–0,430] → 0,422 [0,406–0,432] (+1 %) | 1,52 → 1,52 (0 %) | 1,96 → 1,95 (0 %) | 0,763 [0,658–0,817] → 0,743 [0,576–0,838] (−3 %) | 3,42 → 2,43 (−29 %) | 0,0249 → 0,0244 | 402 → 406 |
| showcase | 16,12 → 16,11 (0 %) | 0,391 [0,379–0,401] → 0,407 [0,396–0,417] (+4 %) | 1,11 → 1,13 (+1 %) | 0,61 → 0,61 (0 %) | 0,301 [0,288–0,317] → 0,282 [0,237–0,338] (−6 %) | 1,83 → 1,75 (−4 %) | 0,0106 → 0,0118 | 268 → 272 |
| flat-crowd | 16,07 → 16,14 (0 %) | 0,603 [0,598–0,608] → 0,609 [0,598–0,625] (+1 %) | 0,75 → 0,73 (−2 %) | 0,83 → 0,82 (−2 %) | 0,428 [0,339–0,569] → 0,402 [0,376–0,421] (−6 %) | 2,12 → 2,52 (+19 %) | 0,0004 → 0,0004 | 258 → 266 |
| armour-crowd | 15,91 → 15,93 (0 %) | 1,593 [1,537–1,649] → 1,569 [1,553–1,597] (−2 %) | 0,87 → 0,88 (+1 %) | 1,08 → 1,05 (−2 %) | 0,402 [0,393–0,413] → 0,380 [0,368–0,399] (−5 %) | 2,73 → 2,52 (−8 %) | 0,0003 → 0,0003 | 274 → 277 |

Wiederholung (light, liquids, glass, 3 Runden direkt danach, Last 2,3–4,9, kein Lauf belastet): light Tick Ø 0,761 [0,713–0,810] → 0,582 [0,571–0,595] (−23 %), Tick max −12 %, Frame p99 −1 %; liquids Tick Ø 0,608 [0,494–0,711] → 0,750 [0,642–0,813] (+23 %), Tick max +17 %, Frame p99 11,95 → 13,53 (+13 %), GUI-CPU Ø −5 %; glass Frame p99 14,97 → 14,44 (−3 %), Tick Ø −8 %, Tick max −24 %. Dritte Reihe liquids (4 Runden, Last 2,3–3,6): Tick Ø 0,665 [0,614–0,757] → 0,619 [0,549–0,697] (−7 %), Tick max −4 %, Frame p99 +2 %, GUI-CPU Ø +1 %.

**Mehrspieler-Last (neu, erste Referenzwerte, kein A/B):** Release-`qtcraft-headless --server` (flache Welt, Seed 42, 8 Chunks, `--bind 127.0.0.1 --port 0`, ohne Mods) und 1 bzw. 4 Bots über TCP (`qtcraft-headless --join 127.0.0.1:<port>`, Sichtweite 8). Jeder Bot sprintet und springt geradeaus (Bot1 und Bot4 nach 0°, Bot2 nach 180°, Bot3 nach 270°, jeweils um 20° pendelnd, rund 200 m), baut alle 4 s den Block vor sich ab (Drops, Blockänderung an alle) und schreibt eine Chatzeile; Bot1 ist Operator und liest alle 4 s die Server-Debugdaten (`debug`, Mittel und Maximum über die letzten 100 Ticks). Ausgewertet die 10 Proben nach dem Beitritt (≈ 45 s), CPU-Zeit und Speicher des Server-Prozesses über den ganzen Lauf (≈ 72 s, `ps` alle 2 s). Je 3 Läufe abwechselnd 1 und 4 Bots, Last 2,3–3,4 (vor einem Lauf mit 1 Bot kurz 7,4), Mittel [Spanne]:

| Bots | Tick Ø ms | Tick max ms | Phase Nachrichten Ø ms | Streaming Ø ms | Client-Abgleich Ø ms | Items Ø ms | Server-CPU (CPU-Zeit/Laufzeit) | Server-Speicher MB | geladene Chunks am Ende |
|-----:|----------:|------------:|-----------------------:|---------------:|---------------------:|-----------:|-------------------------------:|-------------------:|------------------------:|
| 1 | 0,253 [0,223–0,283] | 6,8 [6,2–7,4] | 0,0171 [0,0161–0,0177] | 0,142 | 0,038 | 0,036 | 0,90 s ≈ 1,2 % | 42 | 326 |
| 4 | 0,451 [0,425–0,478] (×1,8) | 9,8 [9,0–11,3] | 0,0415 [0,0398–0,0425] (×2,4) | 0,306 (×2,2) | 0,051 (×1,3) | 0,052 | 1,70 s ≈ 2,4 % | 72 | 979 |

- Der Tick wächst mit vier statt einem Spieler um 0,2 ms (×1,8), vor allem durch das Streaming (+0,16 ms: vier Spieler laufen in neue Gebiete, dreimal so viele Chunks geladen) und die Nachrichten (+0,024 ms, Bewegungseingaben von vier Clients mit 20 Hz, Abbau, Chat); der Client-Abgleich wächst nur um ein Drittel (0,038 → 0,051 ms, also 0,013 ms pro Spieler und Tick). Spieler-Phase 0,003 ms unabhängig von der Spielerzahl (Anmeldungen, Identitäten, Sicherungen, Spielerliste).
- Tick max kommt in allen Läufen aus dem Autosave (Phase Speichern 6,1–11,0 ms, mit den geladenen Chunks wachsend), nicht aus dem Netz; die Nachrichten-Phase bleibt im schlechtesten Tick bei 0,13–0,48 ms (einmal 1,6 ms mit 4 Bots).
- Der Server-Prozess mit 4 Spielern braucht rund 2,4 % eines Kerns (Tick-Thread, Weltgenerierung, TCP); der Tick nutzt unter 1 % des 50-ms-Intervalls.

**Bewertung Etappe 25:**

- **Client und Interpolationspuffer ohne Kosten:** GUI-CPU Ø −3 bis +5 % in allen Szenarien, in den Gedränge-Szenarien mit dem Puffer pro Figur 0 % (crowd, 84 Wesen), +1 % (flat-crowd, 80) und −2 % (armour-crowd, 128 Figuren); Entity-Vernetzung −2 bis +4 %, Render-CPU −2 bis +3 %, GPU −2 bis +3 %, Speicher 0 bis +3 % (bis +8 MB, flat-crowd). Der Puffer arbeitet pro Client-Tick (20 Hz) mit einer kurzen Liste pro Figur, nicht pro Frame.
- **Tick Ø über der Schwelle in einzelnen Reihen, nicht wiederholbar:** light +19 % in der ersten Reihe, −23 % in der Wiederholung; liquids +15 % und +23 %, in der dritten Reihe −7 %. In den Reihen mit Anstieg steigen alle Phasen mit unverändertem Code gemeinsam (liquids: Wesen +20 %, Gegenstände +37 %, Pflanzen +19 %, Streaming +13 %, Client-Abgleich +11 %, Mittel über zwei Reihen); das ist die bekannte Abhängigkeit der Tick-Zeit von Kern und Takt (E-250), keine Regression. In den übrigen sechs Szenarien −16 bis −3 %.
- **Phase Spieler +1 bis +3 µs pro Tick** (0,0008–0,0020 → 0,0023–0,0048 ms, in allen Szenarien und Läufen): neue Arbeit pro Tick (Ablauf offener Anmeldungen über die Clients, Fälligkeit von `identities.json`, Abfrage der Sicherungen mit einer Sperre, Spielerliste nur bei Änderung). Absolut 0,4 % des Ticks, gewollt; keine Änderung.
- **Frame p99 glass +14 % und liquids +13 % (je eine Reihe):** die Zweiteilung des p99 je Lauf (≈ 10–12 ms oder ≈ 15,5–16 ms), in der Wiederholung bzw. dritten Reihe −3 % und +2 %; GUI-, Render-CPU und GPU unverändert.
- **Tick max** light +45 % (Wiederholung −12 %), flat-crowd +19 %, liquids +17 % in der Wiederholung (dritte Reihe −4 %): einzelne schlechteste Ticks wie in E-250; dafür glass −29 %/−24 %.
- **Keine Code-Änderung nötig**, begründet in E-278. Mehrspieler-Referenzwerte auch in `docs/Performance.md`.

## Nachgeholte Messungen: `9075fec`, Etappe 26, Leere-Zone, Etappe 27 (2026-10-12)

Die vier Squash-Merges vom 2026-10-12 wurden ohne Benchmark nach `dev` übernommen (der Mensch arbeitete am Rechner) und hier nachgemessen, jeder Stand verschränkt gegen seinen Vorgänger. Neu dafür ist `scripts/bench-ab.sh`: Es misst beliebig viele Builds verschränkt (pro Runde und Szenario jeder Build einmal, in der angegebenen Reihenfolge), wartet zu Beginn bis zu 3 min auf einen ruhigen Rechner (1-min-Last ≤ 6, kein fremder Prozess ≥ 80 % CPU außer WindowServer) und schreibt pro Lauf Last und belastende Prozesse mit. Alle Stände als Release-Build (`RelWithDebInfo`, nur das Ziel `qtcraft`) aus `git archive` außerhalb des Repos, 8 Chunks, 20 s pro Lauf, Ton stumm, eingebautes Display im Hintergrund, Mittel [Spanne]. Während der Messreihen hielt diese Sitzung die Sperre `~/.cache/qtcraft/bench.lock` (Absprache mit der parallelen Etappe-28-Sitzung). Nachtrag nach dem Review: `bench-ab.sh` legt die Sperre inzwischen selbst an (Verzeichnis mit Besitzerdatei, entfernt am Ende auch bei Abbruch, wartet, solange ein anderer sie hält), rotiert die Reihenfolge der Builds pro Runde (Runde r beginnt mit Build (r − 1) mod n; Position und Reihenfolge stehen in jeder Ergebniszeile), wartet vor jedem Lauf bis zu 3 min auf Ruhe und schreibt Last und fremde Prozesse vor und nach jedem Lauf mit (fremd: ≥ 80 % CPU außer WindowServer und kernel_task, dazu jedes `qtcraft` einer anderen Sitzung); die Reihen 1 bis 5 unten entstanden noch mit fester Reihenfolge.

**Reihen:**

1. Hauptreihe: `d28dd89` → `9075fec` → `48249aa` → `9666133` → `7c7c924` in einer Runde nacheinander, 11 Szenarien (stand, sprint, crowd, light, liquids, glass, showcase, flat-crowd, flat-liquids, void-light, armour-crowd), 3 Runden, 165 Läufe. Last 2,3–5,6; in 10 Läufen lief Safari mit rund 100 % CPU (über alle Stände verteilt).
2. Korrektur der Unterseite (`556e71c`, E-330) gegen `7c7c924` und `48249aa`, 7 Szenarien, 3 Runden. Last 3,0–8,6; Runde 1 teils belastet (Builds und ein Test der anderen Sitzung).
3. Wiederholung der auffälligen Werte: alle fünf Stände plus `fd70a0b` (beide Korrekturen), 7 Szenarien, 3 Runden. Runde 1 stark belastet (Last bis 13,4, Builds der anderen Sitzung); bewertet wurden die Runden 2 und 3 (Last 2,6–4,2).
4. Neue Szenarien: `gravel-collapse` (`48249aa`, `9666133`, `7c7c924`, `fd70a0b`), `farm` und `farm-seeds` (`7c7c924`, `fd70a0b`), je 3 Runden. Last 2,7–4,8.
5. Gegenprobe zur Reihenfolge: `fd70a0b` → `7c7c924` → `48249aa` (umgekehrt), stand, sprint, crowd, glass, 4 Runden. Last 2,6–4,8.

Frame p99 war in allen Reihen wie bekannt zweigeteilt (je Lauf ≈ 9,5–10,5 oder ≈ 14–16 ms, bei allen Ständen); einzelne p99-Unterschiede bis ±39 % in einer Reihe wechseln das Vorzeichen zwischen Paaren und Reihen und werden nicht als Regression gewertet.

### `9075fec` gegen `d28dd89` (Tooltips und Steuerpalette, Sprint-Sichtfeld, dedizierter Server im Bundle)

Reihe 1, Basis → neu:

| Szenario | Frame p99 ms | GUI-CPU Ø ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Speicher MB |
|----------|---:|---:|---:|---:|---:|---:|---:|
| stand | 10,45 → 11,52 (+10 %) | 0,433 → 0,429 (−1 %) | 1,50 → 1,49 (−1 %) | 1,72 → 1,66 (−4 %) | 0,678 → 0,675 (+0 %) | 2,95 → 2,75 (−7 %) | 396 → 397 (+0 %) |
| sprint | 9,97 → 9,80 (−2 %) | 0,417 → 0,399 (−4 %) | 1,58 → 1,46 (−8 %) | 2,12 → 2,04 (−3 %) | 0,658 → 0,644 (−2 %) | 4,00 → 3,54 (−11 %) | 557 → 560 (+0 %) |
| crowd | 9,52 → 10,24 (+8 %) | 0,677 → 0,705 (+4 %) | 1,17 → 1,18 (+1 %) | 2,00 → 2,05 (+3 %) | 0,674 → 0,791 (+17 %) | 1,74 → 1,91 (+10 %) | 401 → 399 (+0 %) |
| light | 11,30 → 11,01 (−3 %) | 0,472 → 0,490 (+4 %) | 1,43 → 1,42 (−1 %) | 1,41 → 1,43 (+1 %) | 0,650 → 0,612 (−6 %) | 4,04 → 2,95 (−27 %) | 404 → 407 (+1 %) |
| liquids | 10,94 → 11,01 (+1 %) | 0,405 → 0,424 (+5 %) | 1,38 → 1,36 (−2 %) | 1,36 → 1,34 (−2 %) | 0,609 → 0,620 (+2 %) | 2,97 → 2,22 (−25 %) | 410 → 409 (+0 %) |
| glass | 11,73 → 10,47 (−11 %) | 0,464 → 0,465 (+0 %) | 1,32 → 1,41 (+7 %) | 1,79 → 1,81 (+1 %) | 0,626 → 0,596 (−5 %) | 3,35 → 3,49 (+4 %) | 409 → 408 (+0 %) |
| showcase | 15,39 → 11,73 (−24 %) | 0,396 → 0,409 (+3 %) | 1,06 → 1,05 (−1 %) | 0,666 → 0,679 (+2 %) | 0,247 → 0,254 (+3 %) | 1,13 → 1,34 (+19 %) | 268 → 270 (+1 %) |
| flat-crowd | 11,87 → 10,06 (−15 %) | 0,581 → 0,586 (+1 %) | 0,637 → 0,624 (−2 %) | 0,860 → 0,856 (+0 %) | 0,386 → 0,359 (−7 %) | 1,80 → 1,08 (−40 %) | 263 → 262 (+0 %) |
| flat-liquids | 13,96 → 11,88 (−15 %) | 0,272 → 0,278 (+2 %) | 0,911 → 0,916 (+0 %) | 0,612 → 0,636 (+4 %) | 0,141 → 0,159 (+13 %) | 0,719 → 0,733 (+2 %) | 264 → 267 (+1 %) |
| void-light | 12,59 → 11,84 (−6 %) | 0,293 → 0,339 (+16 %) | 0,758 → 0,762 (+1 %) | 0,588 → 0,575 (−2 %) | 0,129 → 0,157 (+22 %) | 0,367 → 0,933 (+154 %) | 220 → 222 (+1 %) |
| armour-crowd | 12,10 → 11,86 (−2 %) | 1,45 → 1,59 (+10 %) | 0,776 → 0,804 (+4 %) | 1,08 → 1,14 (+6 %) | 0,361 → 0,343 (−5 %) | 1,67 → 1,76 (+5 %) | 283 → 276 (−3 %) |

Der Server-Code ist unverändert (`git diff d28dd89 9075fec -- src/server src/common` leer). Neu pro Frame ist nur die Sichtfeld-Verbreiterung beim Sprinten (`QcFovEffect::advance`, eine Exponentialfunktion). **Bewertung:** keine Regression. Tick Ø crowd +17 % und flat-liquids +13 % in Reihe 1, crowd in Reihe 3 −4 %, über beide Reihen +5 %. void-light Tick Ø liegt in beiden Reihen höher (+22 %, +16 %; 0,13 → 0,16 ms, gleichmäßig über die Phasen Streaming, Wesen und Pflanzen verteilt) bei identischem Server-Code: Kern- und Taktstreuung wie in E-250. GUI-CPU void-light +16 %/+13 % (+0,04 ms), in den übrigen zehn Szenarien −7 bis +5 % (über beide Reihen); das Sichtfeld kostet nichts Messbares.

### `48249aa` gegen `9075fec` (Etappe 26: Kreativmodus, Flug, Katalog-Inventar, Pick Block)

Reihe 1, Basis → neu:

| Szenario | Frame p99 ms | GUI-CPU Ø ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Speicher MB |
|----------|---:|---:|---:|---:|---:|---:|---:|
| stand | 11,52 → 9,98 (−13 %) | 0,429 → 0,416 (−3 %) | 1,49 → 1,41 (−6 %) | 1,66 → 1,62 (−3 %) | 0,675 → 0,590 (−13 %) | 2,75 → 2,53 (−8 %) | 397 → 417 (+5 %) |
| sprint | 9,80 → 10,07 (+3 %) | 0,399 → 0,400 (+0 %) | 1,46 → 1,49 (+2 %) | 2,04 → 2,05 (+1 %) | 0,644 → 0,755 (+17 %) | 3,54 → 3,19 (−10 %) | 560 → 580 (+4 %) |
| crowd | 10,24 → 10,05 (−2 %) | 0,705 → 0,660 (−6 %) | 1,18 → 1,20 (+1 %) | 2,05 → 1,98 (−3 %) | 0,791 → 0,786 (−1 %) | 1,91 → 2,34 (+22 %) | 399 → 420 (+5 %) |
| light | 11,01 → 11,45 (+4 %) | 0,490 → 0,507 (+4 %) | 1,42 → 1,42 (+0 %) | 1,43 → 1,41 (−1 %) | 0,612 → 0,583 (−5 %) | 2,95 → 3,11 (+6 %) | 407 → 425 (+4 %) |
| liquids | 11,01 → 10,89 (−1 %) | 0,424 → 0,429 (+1 %) | 1,36 → 1,32 (−3 %) | 1,34 → 1,32 (−1 %) | 0,620 → 0,576 (−7 %) | 2,22 → 2,23 (+1 %) | 409 → 431 (+5 %) |
| glass | 10,47 → 11,11 (+6 %) | 0,465 → 0,452 (−3 %) | 1,41 → 1,42 (+1 %) | 1,81 → 1,85 (+2 %) | 0,596 → 0,545 (−9 %) | 3,49 → 2,85 (−18 %) | 408 → 428 (+5 %) |
| showcase | 11,73 → 13,69 (+17 %) | 0,409 → 0,384 (−6 %) | 1,05 → 1,04 (−1 %) | 0,679 → 0,641 (−6 %) | 0,254 → 0,225 (−12 %) | 1,34 → 0,605 (−55 %) | 270 → 289 (+7 %) |
| flat-crowd | 10,06 → 14,03 (+39 %) | 0,586 → 0,648 (+10 %) | 0,624 → 0,669 (+7 %) | 0,856 → 0,822 (−4 %) | 0,359 → 0,408 (+14 %) | 1,08 → 2,58 (+139 %) | 262 → 284 (+8 %) |
| flat-liquids | 11,88 → 15,13 (+27 %) | 0,278 → 0,275 (−1 %) | 0,916 → 0,915 (+0 %) | 0,636 → 0,601 (−6 %) | 0,159 → 0,155 (−3 %) | 0,733 → 1,26 (+71 %) | 267 → 283 (+6 %) |
| void-light | 11,84 → 13,65 (+15 %) | 0,339 → 0,335 (−1 %) | 0,762 → 0,767 (+1 %) | 0,575 → 0,582 (+1 %) | 0,157 → 0,153 (−3 %) | 0,933 → 0,775 (−17 %) | 222 → 239 (+8 %) |
| armour-crowd | 11,86 → 13,93 (+17 %) | 1,59 → 1,48 (−7 %) | 0,804 → 0,738 (−8 %) | 1,14 → 1,07 (−6 %) | 0,343 → 0,325 (−5 %) | 1,76 → 0,841 (−52 %) | 276 → 306 (+11 %) |

**Bewertung:** keine Regression über der Schwelle. **Spitzenspeicher** in allen Szenarien wiederholbar +18 bis +23 MB (über beide Reihen +3 bis +9 %; armour-crowd in Reihe 1 +11 %, über beide Reihen +8 %): fester Mehrbedarf des Katalog-Inventars und der Kreativ-Oberfläche, unabhängig vom Szenario; unter der Schwelle, keine Änderung (falls spätere Etappen den Speicher weiter treiben, zuerst mit `profile.sh alloc` hier ansetzen). Tick Ø sprint +17 % in Reihe 1 und +12 % in Reihe 3, dabei steigen alle Phasen gemeinsam (Reihe 3: Wesen +12 %, Pflanzen +19 %, Streaming und Client-Abgleich ebenso), wie bei einem anderen Takt des Kerns (E-250); in den übrigen Szenarien liegt Tick Ø zwischen −13 und +14 % (flat-crowd +14 % in Reihe 1, −10 % in Reihe 3; stand −13 %/−11 %, crowd −1 %/−8 %). Tick max einzelner Szenarien +22 bis +139 % in einer Reihe (flat-crowd, flat-liquids), in anderen −52 bis −55 %: Ausreißer wie in E-224/E-230.

### `9666133` gegen `48249aa` (Leere-Zone, E-288)

Reihe 1, Basis → neu:

| Szenario | Frame p99 ms | GUI-CPU Ø ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Speicher MB |
|----------|---:|---:|---:|---:|---:|---:|---:|
| stand | 9,98 → 10,46 (+5 %) | 0,416 → 0,458 (+10 %) | 1,41 → 1,60 (+13 %) | 1,62 → 1,71 (+5 %) | 0,590 → 0,625 (+6 %) | 2,53 → 3,15 (+24 %) | 417 → 429 (+3 %) |
| sprint | 10,07 → 10,34 (+3 %) | 0,400 → 0,437 (+9 %) | 1,49 → 1,64 (+10 %) | 2,05 → 2,20 (+7 %) | 0,755 → 0,765 (+1 %) | 3,19 → 3,39 (+6 %) | 580 → 608 (+5 %) |
| crowd | 10,05 → 10,89 (+8 %) | 0,660 → 0,645 (−2 %) | 1,20 → 1,23 (+3 %) | 1,98 → 1,99 (+0 %) | 0,786 → 0,778 (−1 %) | 2,34 → 2,14 (−8 %) | 420 → 433 (+3 %) |
| light | 11,45 → 11,17 (−2 %) | 0,507 → 0,507 (+0 %) | 1,42 → 1,49 (+5 %) | 1,41 → 1,49 (+5 %) | 0,583 → 0,617 (+6 %) | 3,11 → 2,56 (−18 %) | 425 → 435 (+2 %) |
| liquids | 10,89 → 10,88 (+0 %) | 0,429 → 0,432 (+1 %) | 1,32 → 1,46 (+10 %) | 1,32 → 1,43 (+8 %) | 0,576 → 0,597 (+4 %) | 2,23 → 2,42 (+8 %) | 431 → 441 (+2 %) |
| glass | 11,11 → 10,31 (−7 %) | 0,452 → 0,474 (+5 %) | 1,42 → 1,41 (−1 %) | 1,85 → 1,88 (+2 %) | 0,545 → 0,590 (+8 %) | 2,85 → 2,62 (−8 %) | 428 → 437 (+2 %) |
| showcase | 13,69 → 12,08 (−12 %) | 0,384 → 0,392 (+2 %) | 1,04 → 1,20 (+15 %) | 0,641 → 0,747 (+17 %) | 0,225 → 0,252 (+12 %) | 0,605 → 1,01 (+67 %) | 289 → 307 (+6 %) |
| flat-crowd | 14,03 → 10,47 (−25 %) | 0,648 → 0,616 (−5 %) | 0,669 → 0,795 (+19 %) | 0,822 → 0,924 (+12 %) | 0,408 → 0,353 (−14 %) | 2,58 → 1,44 (−44 %) | 284 → 298 (+5 %) |
| flat-liquids | 15,13 → 12,12 (−20 %) | 0,275 → 0,282 (+3 %) | 0,915 → 1,03 (+12 %) | 0,601 → 0,597 (−1 %) | 0,155 → 0,155 (+0 %) | 1,26 → 0,622 (−50 %) | 283 → 297 (+5 %) |
| void-light | 13,65 → 11,74 (−14 %) | 0,335 → 0,342 (+2 %) | 0,767 → 0,740 (−4 %) | 0,582 → 0,573 (−2 %) | 0,153 → 0,139 (−9 %) | 0,775 → 0,944 (+22 %) | 239 → 239 (+0 %) |
| armour-crowd | 13,93 → 9,71 (−30 %) | 1,48 → 1,47 (−1 %) | 0,738 → 0,891 (+21 %) | 1,07 → 1,21 (+13 %) | 0,325 → 0,320 (−1 %) | 0,841 → 1,39 (+65 %) | 306 → 316 (+3 %) |

Wie in E-288 angekündigt zeichnet der Mesher seit der Leere-Zone die Unterseite der untersten Schicht. Dadurch bekam jede Chunk-Säule eine zusätzlich gezeichnete Sektion (die bis dahin vollständig verdeckte Sektion 0 mit einer Fläche je Säule):

| Szenario | Sektionen (Basis → Leere) | sichtbare Sektionen / Draw-Calls | Dreiecke | Sektionspuffer MB |
|----------|---------------------------|----------------------------------|----------|-------------------|
| stand | 1228 → 1328 | 340 → 366 / 343 → 369 | 479 900 → 514 300 (+7 %) | 87,9 → 95,9 |
| sprint | 1483 → 1598 | 391 → 420 / 395 → 424 | 561 400 → 603 900 (+8 %) | 102 → 111 |
| showcase | 354 → 643 | 144 → 221 / 146 → 223 | 74 500 → 113 900 (+53 %) | 9,7 → 17,7 |
| flat-crowd | 289 → 578 | 88 → 159 / 89 → 160 | 49 800 → 86 300 (+73 %) | 8,0 → 16,0 |
| flat-liquids | 289 → 578 | 88 → 159 / 89 → 160 | 45 100 → 81 400 (+81 %) | 8,0 → 16,0 |
| armour-crowd | 289 → 578 | 88 → 159 / 89 → 160 | 64 400 → 100 800 (+56 %) | 8,0 → 16,0 |

Das Frustum verwirft diese Sektionen nicht (sie liegen im Blickfeld unter dem Gelände), und eine Verdeckungsprüfung gibt es nicht. Folge: Render-CPU in der flachen Welt und im Showcase +12 bis +21 %, GPU +12 bis +17 %, in den normalen Welten Render-CPU +5 bis +13 %; das liegt über der Schwelle. **Behoben** in `556e71c` (E-330): Flächen zur Leere hin kommen in eigene Listen des Sektions-Netzes, der Renderer zeichnet sie nur bei einer Kamera unter y = 0 und lässt Sektionen ohne andere Flächen ganz aus. Nachmessung siehe unten. Tick Ø showcase +12 % (Reihe 1) betrifft keine Server-Änderung des Szenarios (Showcase hat keinen Kies und keine Leere-Zone im Tick); in Reihe 2 lag `9666133` nicht vor, die Stände danach liegen auf `48249aa` (−8 %).

### `7c7c924` gegen `9666133` (Etappe 27: Landwirtschaft, Zufallsticks, Items in Strömung, Zucht)

Reihe 1, Basis → neu:

| Szenario | Frame p99 ms | GUI-CPU Ø ms | Render-CPU ms | GPU Ø ms | Tick Ø ms | Tick max ms | Speicher MB |
|----------|---:|---:|---:|---:|---:|---:|---:|
| stand | 10,46 → 10,47 (+0 %) | 0,458 → 0,421 (−8 %) | 1,60 → 1,47 (−8 %) | 1,71 → 1,70 (−1 %) | 0,625 → 0,673 (+8 %) | 3,15 → 2,76 (−13 %) | 429 → 430 (+0 %) |
| sprint | 10,34 → 10,04 (−3 %) | 0,437 → 0,433 (−1 %) | 1,64 → 1,46 (−11 %) | 2,20 → 2,14 (−2 %) | 0,765 → 0,662 (−14 %) | 3,39 → 3,58 (+6 %) | 608 → 607 (+0 %) |
| crowd | 10,89 → 10,64 (−2 %) | 0,645 → 0,616 (−5 %) | 1,23 → 1,22 (+0 %) | 1,99 → 1,93 (−3 %) | 0,778 → 0,766 (−1 %) | 2,14 → 2,12 (−1 %) | 433 → 434 (+0 %) |
| light | 11,17 → 11,72 (+5 %) | 0,507 → 0,478 (−6 %) | 1,49 → 1,42 (−5 %) | 1,49 → 1,40 (−6 %) | 0,617 → 0,602 (−2 %) | 2,56 → 2,54 (−1 %) | 435 → 440 (+1 %) |
| liquids | 10,88 → 11,19 (+3 %) | 0,432 → 0,435 (+1 %) | 1,46 → 1,36 (−7 %) | 1,43 → 1,35 (−6 %) | 0,597 → 0,556 (−7 %) | 2,42 → 2,15 (−11 %) | 441 → 440 (+0 %) |
| glass | 10,31 → 13,58 (+32 %) | 0,474 → 0,446 (−6 %) | 1,41 → 1,38 (−2 %) | 1,88 → 1,85 (−1 %) | 0,590 → 0,630 (+7 %) | 2,62 → 3,01 (+15 %) | 437 → 443 (+1 %) |
| showcase | 12,08 → 10,18 (−16 %) | 0,392 → 0,333 (−15 %) | 1,20 → 1,13 (−6 %) | 0,747 → 0,653 (−13 %) | 0,252 → 0,260 (+3 %) | 1,01 → 1,69 (+67 %) | 307 → 305 (−1 %) |
| flat-crowd | 10,47 → 10,78 (+3 %) | 0,616 → 0,619 (+0 %) | 0,795 → 0,752 (−5 %) | 0,924 → 0,878 (−5 %) | 0,353 → 0,344 (−3 %) | 1,44 → 1,55 (+8 %) | 298 → 298 (+0 %) |
| flat-liquids | 12,12 → 12,26 (+1 %) | 0,282 → 0,277 (−2 %) | 1,03 → 1,01 (−2 %) | 0,597 → 0,613 (+3 %) | 0,155 → 0,152 (−2 %) | 0,622 → 0,770 (+24 %) | 297 → 298 (+0 %) |
| void-light | 11,74 → 13,13 (+12 %) | 0,342 → 0,348 (+2 %) | 0,740 → 0,729 (−1 %) | 0,573 → 0,582 (+2 %) | 0,139 → 0,139 (+0 %) | 0,944 → 0,769 (−19 %) | 239 → 241 (+1 %) |
| armour-crowd | 9,71 → 10,46 (+8 %) | 1,47 → 1,56 (+6 %) | 0,891 → 0,887 (+0 %) | 1,21 → 1,21 (+0 %) | 0,320 → 0,342 (+7 %) | 1,39 → 1,05 (−25 %) | 316 → 318 (+0 %) |

**Bewertung:** keine Regression über der Schwelle im ganzen Tick, in Frame-Werten und Speicher. Wiederholbar ist nur die Phase **Pflanzen** (+13 bis +65 %, absolut +1 bis +5 µs pro Tick, unter 1 % des Ticks): Seit Etappe 27 sortiert `QcPlantGrowth::tick` jeden Tick die Chunks mit wachsenden Sektionen (feste Reihenfolge für reproduzierbare Zufallsticks) und läuft `QcMobBreeding::tick` (in derselben Phase) dreimal über alle Wesen. Die Sortierung wird seit `fd70a0b` zwischengespeichert und nur nach Änderungen der Chunk-Menge neu gebildet (E-331); in Reihe 5 liegt die Phase damit −16 bis +10 % um `7c7c924` (Streuung der µs-Werte ±20 %), ein Gewinn ist nicht messbar, der Aufwand pro Tick aber weg. Die Zuchtschleife bleibt (einige Nanosekunden pro Wesen). glass Frame p99 +32 % in Reihe 1 ist die Zweiteilung (Reihe 3: −4 %, Reihe 5 gegen `fd70a0b`: −4 %).

### Nach den Korrekturen

`556e71c` (Unterseite) gegen `7c7c924`, Reihe 2:

| Szenario | Dreiecke | Draw-Calls | Render-CPU ms | GPU Ø ms | Bemerkung |
|----------|----------|-----------:|---------------|----------|-----------|
| stand | 514 567 → 480 775 (−7 %) | 369 → 347 | 1,51 → 1,59 (+6 %) | 1,63 → 1,67 (+2 %) | Reihe 5: Render-CPU +5 %, GPU +4 % |
| sprint | 604 561 → 563 361 (−7 %) | 424 → 399 | 1,65 → 1,68 (+1 %) | 2,06 → 2,07 | Reihe 5: −8 % / +3 % |
| glass | 514 624 → 480 825 (−7 %) | 371 → 349 | 1,64 → 1,63 (−1 %) | 1,85 → 1,82 (−2 %) | |
| showcase | 113 073 → 73 650 (−35 %) | 219 → 142 | 1,37 → 1,24 (−9 %) | 0,722 → 0,646 (−10 %) | |
| flat-crowd | 86 113 → 49 761 (−42 %) | 160 → 89 | 0,911 → 0,788 (−14 %) | 0,908 → 0,828 (−9 %) | |
| flat-liquids | 81 436 → 45 084 (−45 %) | 160 → 89 | 1,22 → 1,12 (−8 %) | 0,640 → 0,595 (−7 %) | |
| armour-crowd | 100 950 → 64 257 (−36 %) | 161 → 89 | 1,03 → 1,00 (−3 %) | 1,18 → 1,13 (−4 %) | |

**Bedeutung der Zähler seit `556e71c`:** `visible_sections` zählt nur noch Sektionen, die gezeichnet werden: Eine Sektion, die nur Unterseite hat, kommt bei einer Kamera über y = 0 nicht in die Liste, auch wenn ihre Grenzen im Frustum liegen. Bis `7c7c924` zählten solche Sektionen mit (Leere-Zone: eine je Säule). `sections` (alle hochgeladenen Netze) und `section_mb` schließen die Sektionen mit nur Unterseite weiter ein. Vergleiche von `visible_sections` über `556e71c` hinweg mit `9666133` oder `7c7c924` messen also auch diese Umstellung; gegen `48249aa` und ältere Stände ist die Bedeutung wieder dieselbe.

Dreiecke und Draw-Calls liegen damit wieder auf dem Stand vor der Leere-Zone (`48249aa`: flat 49 761 / 89, Showcase 74 484 / 146; normal +0,2 %, die Unterseite in Sektionen mit sichtbarem Gelände kostet keine eigenen Aufrufe mehr). Gegen `48249aa` über die Reihen 2, 3 und 5: Render-CPU −11 bis +10 %, GPU −8 bis +7 %, ohne gleichgerichtete Abweichung. Geblieben ist der Sektionspuffer (normal +8 MB, flach +8 MB): Die Unterseiten liegen weiter auf der GPU, damit der Blick aus der Leere sofort stimmt; Spitzenspeicher +2 bis +3 % gegenüber `48249aa`, unter der Schwelle. Sichtprüfung (Offscreen-Bild aus y = −10 nach oben, flache Welt, Kreativflug): `fd70a0b` und `7c7c924` pixelgleich (größte Abweichung 1/255), die Unterseite wird von unten also weiter gezeichnet.

**Reihenfolge-Effekt (Reihe 3 und 4):** Dort lag `fd70a0b` als letzter Build jeder Runde in allen Werten über `7c7c924`, auch in GUI-CPU, Wesen- und Items-Phase, deren Code die Korrekturen nicht berühren (crowd GUI-CPU +33 %, farm-seeds alle Tick-Phasen +21 bis +48 %). In Reihe 5 mit umgekehrter Reihenfolge liegen beide gleichauf (stand Render-CPU +5 %, sprint −8 %, crowd +6 %, glass −3 %; Tick Ø −8 / +14 / −7 / −4 %). Folgerung für künftige Reihen: Bei mehr als zwei Ständen die Reihenfolge in jeder Runde wechseln oder eine Gegenreihe mit umgekehrter Reihenfolge messen, bevor eine Abweichung des letzten Stands als Regression gilt.

### Neue Szenarien (erste Referenzwerte, Release, 8 Chunks, Mittel [Spanne])

**gravel-collapse** (flache Welt auf drei Schichten Kies, nach 2 s ein Schacht bis in die Leere, E-288), 3 Runden je Stand:

| Stand | Tick Ø ms | Tick max ms | fallende Blöcke Ø / max ms | Block-Updates Ø / max ms | neu vernetzte Sektionen | Rückstau max | Änderung bis Bild Ø / max ms | GUI-CPU ms | Speicher MB |
|-------|----------:|------------:|---------------------------:|-------------------------:|------------------------:|-------------:|-----------------------------:|-----------:|------------:|
| `48249aa` (vor der Leere-Zone: kein Einsturz) | 0,163 | 1,06 | 0,0001 / 0,0007 | 0,0009 / 0,052 | 0 | 0 | – | 0,259 | 256 |
| `9666133` | 1,46 [1,41–1,51] | 5,29 | 0,682 / 3,23 | 0,082 / 2,28 | 5261 | 486 | 15,7 / 73 | 2,29 | 313 |
| `7c7c924` | 1,32 [1,08–1,47] | 5,00 | 0,614 / 2,63 | 0,066 / 1,60 | 5229 | 424 | 16,5 / 160 | 2,38 | 311 |
| `fd70a0b` | 1,35 [1,19–1,48] | 5,34 | 0,625 / 2,37 | 0,068 / 2,16 | 5272 | 438 | 17,2 / 117 | 2,32 | 316 |

Der Einsturz kostet im Mittel rund 1,2 ms pro Tick (davon fallende Blöcke 0,6–0,7 ms) und höchstens gut 5 ms, ein Zehntel des Tick-Budgets; die Grenzen aus E-288 (1024 Updates pro Tick, 2048 fallende Blöcke) halten den schlechtesten Tick klein. Auf dem Client steigt die GUI-CPU auf 2,3 ms (Entity-Vernetzung der fallenden Blöcke), der Rückstau auf über 400 Sektionen, eine Änderung braucht im schlechtesten Fall bis 0,3 s bis zum Bild; Frame p99 bleibt bei 10 ms. Etappe 27 und die Korrekturen ändern daran nichts (Tick −10 %/+2 %, Spannen überlappen).

**farm** (63 × 63 nasser Ackerboden mit Wasserrinnen alle 9 m, junger Weizen, Kürbis- und Melonenstängel, Zufallsticks an, ab 3 s jede Sekunde `/tick sprint 600`) und **farm-seeds** (dasselbe Feld mit reifem Weizen ohne Sprint, Saat-Stapel auf belegten Feldern: das Skript legt 15 × 10 = 150 ab, die BENCH-Zeile zählt in jedem Lauf 140 Items, zehn Stapel vereinigen sich also mit einem anderen oder fehlen; nicht weiter untersucht, für alle Stände gleich; Nachtrag Etappe 30, E-354: nichts fehlt, der Server hat alle 150, der Client bekommt Items nur bis 32 m und `entities` zählt die gesendeten, genau 10 Stapel liegen weiter vom Endpunkt entfernt), je 3 Runden:

| Szenario / Stand | Ticks | Tick Ø ms | Phase Pflanzen Ø / max ms | Phase Items Ø ms | neu vernetzte Sektionen | Änderung bis Bild Ø / max ms | Rückstau max | Frame p99 ms |
|------------------|------:|----------:|--------------------------:|-----------------:|------------------------:|-----------------------------:|-------------:|-------------:|
| farm / `7c7c924` | 11 780 | 0,036 | 0,012 / 0,53 | 0,0003 | 1939 | 20,3 / 57 | 100 | 11,5 |
| farm / `fd70a0b` | 11 782 | 0,038 | 0,012 / 0,42 | 0,0003 | 1950 | 21,2 / 63 | 108 | 11,7 |
| farm-seeds / `7c7c924` | 401 | 0,275 | 0,051 / 0,47 | 0,061 | 102 | 9,2 / 20 | 4 | 9,8 |
| farm-seeds / `fd70a0b` | 401 | 0,334 | 0,066 / 0,58 | 0,078 | 102 | 9,6 / 18 | 4 | 10,5 |

Im Sprint laufen rund 11 400 zusätzliche Ticks in 20 s (600 pro Sekunde in wenigen Millisekunden), die Tick-Werte sind daher Mittel über Sprint-Ticks: Ein Tick mit wachsendem Feld kostet in der Phase Pflanzen 12 µs, der teuerste 0,4–0,5 ms. Der Client vernetzt dabei rund 2000 Sektionen neu, eine Änderung erscheint im Mittel nach 20 ms, im schlechtesten Fall nach 60 ms, bei unverändertem Frame p99. Ruhende Saat (farm-seeds) kostet in der Phase Items rund 0,4–0,6 µs pro Stapel und Tick (Ruheprüfung jeden Tick, Suche nach freiem Boden in neun Zellen einmal pro Sekunde); die Zufallsticks auf dem Feld (63 × 63 = 3969 Zellen, davon 441 Wasserrinne und 3528 Ackerboden unter 3024 Weizen, 252 Stängeln und 252 freien Zellen) kosten 0,05–0,07 ms. Die Unterschiede `7c7c924` → `fd70a0b` in farm-seeds sind der Reihenfolge-Effekt oben (alle Phasen gemeinsam, auch unveränderte). Ein Probelauf mit Saat, die auch in die Wasserrinnen fiel, kam auf 0,17 ms in der Phase Items: Items im Wasser ruhen nie und werden jeden Tick bewegt (E-296); das Szenario legt die Saat deshalb neben die Rinnen.

**Fazit (überarbeitet nach dem Review):** Einzige Regression über der Schwelle war die Unterseite der Welt aus der Leere-Zone (Render-CPU und GPU in flachen Welten und im Showcase +12 bis +21 %); behoben (E-330). Über 10 % lagen in den Reihen mit fester Reihenfolge außerdem void-light (`9075fec`: Tick Ø +22 %/+16 %, GUI-CPU +16 %/+13 %), sprint (`48249aa`: Tick Ø +17 %/+12 %) und farm-seeds (`fd70a0b`: Pflanzen +29 %); diese Werte waren zunächst nur mit Kern- und Taktstreuung bzw. dem Reihenfolge-Effekt begründet, ohne Gegenprobe. Die Nachprüfung mit rotierender Reihenfolge und Tick-Phasen (unten) zeigt sie nicht wieder (+5 %, +4 %, +1 %); sie kamen aus der festen Reihenfolge. Wiederholbar sind: Spitzenspeicher +18 bis +23 MB durch Etappe 26 (unter der Schwelle), Phase Pflanzen +15 % (+2,5 µs) durch Etappe 27, mit `fd70a0b` behoben (E-331), und GPU sprint +15 % durch die Sichtfeld-Verbreiterung von `9075fec` (mehr Sektionen im Frustum, gewollt, E-331). 60 FPS bei 8 Chunks halten alle Stände in allen Szenarien (Frame Ø 8,3–8,8 ms).

### Nachprüfung nach dem Review (2026-10-12, rotierende Reihenfolge)

Alle Reihen mit dem überarbeiteten `bench-ab.sh` (Reihenfolge rotiert pro Runde, Last und fremde Prozesse vor und nach jedem Lauf, Sperre vom Skript gehalten), Release-Builds (`RelWithDebInfo`, Ziel `qtcraft`) aus `git archive`, 8 Chunks, 20 s pro Lauf, Mittel [Spanne]. Last vor und nach den Läufen 2,2–5,1; als belastet markiert (Safari, Teams, Mail oder ein Claude-Fenster ≥ 80 % CPU vor oder nach dem Lauf) sind 1 von 36, 5 von 54 und 2 von 24 Läufen, über die Stände verteilt; sie sind in den Mitteln enthalten, ohne sie ändert sich keine Aussage.

**Reihe 6: `d28dd89`, `9075fec`, `48249aa`, void-light und sprint, 6 Runden (jeder Stand zweimal an jeder Stelle):**

| Szenario | Wert | `d28dd89` | `9075fec` | `48249aa` |
|----------|------|----------:|----------:|----------:|
| void-light | Tick Ø ms | 0,155 [0,119–0,230] | 0,163 [0,133–0,211] (+5 %) | 0,178 [0,136–0,213] (+9 %) |
| void-light | GUI-CPU ms | 0,380 | 0,360 (−5 %) | 0,350 (−3 %) |
| void-light | Phasen Streaming / Nachrichten / Wesen ms | 0,091 / 0,051 / 0,0084 | 0,098 / 0,044 / 0,0085 | 0,112 / 0,049 / 0,0100 |
| sprint | Tick Ø ms | 0,536 [0,474–0,657] | 0,584 [0,461–0,700] (+9 %) | 0,610 [0,497–0,791] (+4 %) |
| sprint | Phasen Wesen / Streaming / Client-Abgleich / Pflanzen ms | 0,386 / 0,079 / 0,042 / 0,016 | 0,430 / 0,083 / 0,039 / 0,017 | 0,444 / 0,086 / 0,045 / 0,020 |
| sprint | GPU Ø ms | 2,00 | 2,31 (+15 %) | 2,29 (−1 %) |
| sprint | sichtbare Sektionen / Dreiecke | 372 / 541 500 | 391 / 562 800 | 391 / 563 500 |
| sprint | Render-CPU / GUI-CPU ms | 1,32 / 0,419 | 1,37 / 0,382 | 1,37 / 0,411 |
| beide | Frame p99 ms | 10,4 / 10,2 | 10,5 / 10,0 | 11,1 / 10,1 |

Prozente gegen die Spalte links. Die Abstände der Reihen 1 und 3 (void-light Tick +22/+16 %, GUI-CPU +16/+13 %; sprint Tick +17/+12 %) wiederholen sich nicht: void-light Tick +5 % bei völlig überlappenden Spannen (Server-Code identisch), GUI-CPU −5 %; sprint Tick `9075fec` → `48249aa` +4 %, alle Phasen innerhalb von 9 µs. Tick-Phasen statt `profile.sh time`: Ohne wiederholbare Abweichung gibt es nichts einzugrenzen (Arbeitsauftrag §3: ohne Verschlechterung kein tiefes Profiling); die Phasen zeigen keinen Teil, der allein wächst. Neu sichtbar ist sprint GPU +15 % von `d28dd89` zu `9075fec` mit 19 sichtbaren Sektionen und 4 % Dreiecken mehr in jeder Runde: die Sichtfeld-Verbreiterung beim Sprinten (`QcFovEffect`), gewollt; Begründung in E-331. In Reihe 1 lag `d28dd89` vorn und `9075fec` an zweiter Stelle, dort war der GPU-Abstand −3 %; die Sektionen wurden damals nicht verglichen.

**Reihe 7: `9666133`, `7c7c924`, `fd70a0b`, farm-seeds, farm und stand, 6 Runden, `fd70a0b` in Runde 1 vorn:**

| Szenario | Wert | `9666133` | `7c7c924` | `fd70a0b` |
|----------|------|----------:|----------:|----------:|
| farm-seeds | Tick Ø ms | – (kein Ackerbau) | 0,255 [0,195–0,391] | 0,257 [0,175–0,378] (+1 %) |
| farm-seeds | Pflanzen / Items ms | – | 0,0477 / 0,0567 | 0,0481 / 0,0572 (+1 % / +1 %) |
| farm | Tick Ø ms | – | 0,0327 | 0,0332 (+2 %) |
| farm | Pflanzen Ø ms | – | 0,0108 | 0,0110 (+2 %) |
| stand | Tick Ø ms | 0,516 [0,375–0,764] | 0,476 [0,386–0,599] | 0,481 [0,369–0,542] |
| stand | Pflanzen Ø ms | 0,0165 [0,0130–0,0231] | 0,0190 [0,0149–0,0254] (+15 %) | 0,0167 [0,0138–0,0188] (−12 %) |

Die Abweichung `7c7c924` → `fd70a0b` in farm-seeds aus Reihe 4 (Pflanzen +29 %, Tick +21 %, `fd70a0b` immer zuletzt) war der Reihenfolge-Effekt: rotiert +1 %. Der Anstieg der Phase Pflanzen mit Etappe 27, auf dem E-331 aufbaut, ist dagegen echt: `7c7c924` liegt in stand in 5 von 6 Runden über `9666133` (+15 %, +2,5 µs); `fd70a0b` bringt die Phase auf den Stand vor Etappe 27 zurück (+1 % gegenüber `9666133`). `9666133` kennt Ackerboden und Weizen noch nicht; seine farm-Läufe sind keine Vergleichswerte.

**Reihe 8: zusammengeführter Stand gegen `dev` (Etappe 28, `91e78b6`), stand, flat-crowd, showcase, 4 Runden:** prüft, ob E-330 auf dem Renderer von Etappe 28 (Texturpakete, Mip-Stufen) hält. Der Zweig unterscheidet sich von `dev` nur in Renderer (Unterseiten-Puffer), Mesher (Listen) und `QcSectionMarks`/`QcPlantGrowth`.

| Szenario | Dreiecke | Draw-Calls | sichtbare Sektionen | Render-CPU ms | GPU Ø ms | GUI-CPU ms | Tick Ø ms | Frame p99 ms | Speicher MB |
|----------|---------:|-----------:|--------------------:|--------------:|---------:|-----------:|----------:|-------------:|------------:|
| stand | 514 575 → 480 783 (−7 %) | 369 → 347 | 366 → 344 | 1,38 → 1,37 (−1 %) | 1,79 → 1,74 (−3 %) | 0,424 → 0,441 (+4 %) | 0,556 → 0,501 (−10 %) | 9,87 → 9,87 | 436 → 434 |
| flat-crowd | 86 240 → 49 888 (−42 %) | 160 → 89 | 159 → 88 | 0,838 → 0,772 (−8 %) | 0,844 → 0,810 (−4 %) | 0,659 → 0,706 (+7 %) | 0,358 → 0,407 (+14 %) | 9,84 → 9,84 | 305 → 301 |
| showcase | 113 075 → 73 651 (−35 %) | 219 → 142 | 216 → 139 | 1,18 → 1,12 (−5 %) | 0,658 → 0,587 (−11 %) | 0,407 → 0,450 (+11 %) | 0,215 → 0,209 (−3 %) | 10,12 → 9,87 | 310 → 307 |

Dreiecke und Draw-Calls fallen auf `dev` genau wie vor dem Zusammenführen (flat 49 761 / 89, Showcase 73 650 / 142, stand −7 %); Render-CPU −1 bis −8 %, GPU −3 bis −11 %. `sections` (578, 641, 1328) und `section_mb` (16,0, 17,7, 95,9) sind gleich: Die Unterseiten liegen weiter auf der GPU (E-330). Über 10 % auf der ungünstigen Seite liegen nur Werte, die der Zweig nicht berührt: GUI-CPU showcase +11 % (der Haupt-Thread hat keine Änderung; ohne den niedrigsten `dev`-Lauf, 0,335 ms, +3 %) und Tick flat-crowd +14 % (ein Lauf mit 0,604 ms, die übrigen drei 0,339–0,344 gegen 0,318–0,441 ms). Keine Regression.

## Etappe 28: Texturpakete

### Atlas allein (Etappe 28, vor dem Benchmark)

Bei der Etappe kein Benchmark (Anweisung des Menschen 2026-10-12); gemessen nur der Atlas mit `qtcraft-headless --atlas-stats` im Release-Build (Basis ohne Mods, 346 Texturen + Platzhalter; Bestwert aus drei Läufen, Kacheln parallel auf dem Entwicklungsrechner):

| Pakete | Kachel | Atlas (Stufe 0) | Mip-Stufen | Speicher alle Stufen | Pakete laden | Atlas bauen |
|--------|-------:|-----------------|-----------:|---------------------:|-------------:|------------:|
| keine (16×) | 16 | 380 × 380 | 2 | 0,7 MiB | 0 ms | 0–2 ms |
| QtCraft HD 32× | 32 | 760 × 760 | 3 | 2,9 MiB | 0 ms | 2 ms |
| QtCraft HD 64× | 64 | 1520 × 1520 | 4 | 11,7 MiB | 0 ms | 8 ms |
| QtCraft HD 128× | 128 | 3040 × 3040 | 5 | 47,0 MiB | 13 ms | 52 ms |
| Storybook 32× (62 PNG) | 32 | 760 × 760 | 3 | 2,9 MiB | 1 ms | 1 ms |

Vorher (Etappe 27): 16× ohne Mip-Stufen in einem quadratischen Zweierpotenz-Raster (32 × 32 Kacheln, 512 × 512 Pixel, 1,0 MiB). Ohne Parallelisierung dauerte 128× 470 ms. Auf der GPU kommt dieselbe Menge hinzu (plus die kleinen Stufen bis 1 × 1). Erwartung für den Benchmark: Frame-Zeiten unverändert bis leicht besser in der Ferne (Mip-Stufen statt Flimmern, weniger Texturbandbreite), Speicher +2 MiB (32×) bis +46 MiB (128×) CPU und GPU, Ladezeit des Atlas beim Weltstart wie oben. Messplan für die Hauptsitzung: siehe Bericht der Etappe (Szenarien `stand`, `sprint`, `showcase`, `crowd` mit `TEXTURE_PACKS=none`, `builtin:qtcraft_hd_32`, `builtin:qtcraft_hd_64`, `builtin:qtcraft_hd_128`).

### Benchmark (2026-10-13, Branch `feature/bench-28`)

Alle Reihen mit `scripts/bench-ab.sh` (Reihenfolge rotiert pro Runde), Release-Builds (`RelWithDebInfo`, Ziel `qtcraft`) aus `git archive`, echtes Fenster auf dem eingebauten Display im Hintergrund, Ton stumm, 20 s pro Lauf, Mittel [Spanne]. Last vor und nach den Läufen 2,2–4,7; als belastet markiert (ein Claude- oder Browser-Prozess ≥ 80 % CPU vor oder nach dem Lauf) sind 1 von 32, 0 von 8, 6 von 64, 2 von 16 Läufen (Reihen A8, A16, B8, B16), über die Stände verteilt; ohne sie ändert sich keine Aussage. Für die Reihen B (gleicher Build, verschiedene Pakete) kann `bench-ab.sh` jetzt Variablen je Name setzen (`x64=<App>:TEXTURE_PACKS=builtin:qtcraft_hd_64`). FPS = 1000 / Frame Ø (VSync, 120-Hz-Display).

**Frame p99 ist in diesen Reihen zweigeteilt:** Ein Lauf liegt entweder bei 9,1–10,9 ms oder bei 14,7–16,6 ms (einzelne verpasste VSyncs über die ganze Laufzeit), je nach Zeitfenster und für alle Stände gleich; die Mittel hängen davon ab, wie viele Läufe eines Stands in die zweite Gruppe fallen. Ebenso wechseln GUI- und Render-CPU zwischen zwei Lagen (etwa 0,22–0,30 und 0,38–0,50 ms GUI-CPU im Showcase), Kern- und Taktwechsel des Systems; Abstände bis rund 20 % in diesen Werten sind daher nur mit mehreren Runden und Gegenprobe zu bewerten.

#### Reihe A: Etappe 28 bei 16× gegen Etappe 27 (`7c7c924` → `91e78b6`, 4 Runden)

16× hat seit Etappe 28 Mip-Stufen und einen Atlas von 380² Pixeln mit Rand (NPOT) statt 512² ohne Mip-Stufen.

| Szenario | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | GUI-CPU ms | Spitzenspeicher MB |
|----------|----:|-------------:|---------:|--------------:|-----------:|-------------------:|
| stand (8) | 118,2 → 118,6 | 13,27 → 13,07 (−1 %) | 1,796 → 1,799 (+0 %) | 1,351 → 1,343 (−1 %) | 0,364 → 0,360 (−1 %) | 430 → 437 (+2 %) |
| sprint (8) | 118,3 → 118,8 | 14,42 → 12,76 (−11 %) | 2,216 → 2,225 (+0 %) | 1,519 → 1,444 (−5 %) | 0,388 → 0,367 (−5 %) | 604 → 608 (+1 %) |
| showcase (8) | 117,1 → 117,1 | 16,12 → 16,14 (+0 %) | 0,742 → 0,746 (+1 %) | 1,187 → 1,202 (+1 %) | 0,365 → 0,366 (+0 %) | 307 → 315 (+2 %) |
| crowd (8) | 118,5 → 118,4 | 12,98 → 14,20 (+9 %) | 2,141 → 2,136 (−0 %) | 1,103 → 1,169 (+6 %) | 0,623 → 0,658 (+6 %) | 435 → 442 (+2 %) |
| sprint (16) | 119,6 → 119,8 | 9,61 → 9,60 (−0 %) | 3,659 → 3,657 (−0 %) | 2,076 → 2,164 (+4 %) | 0,265 → 0,277 (+5 %) | 1423 → 1423 (+0 %) |

Dreiecke, Draw-Calls und sichtbare Sektionen sind in jedem Szenario gleich. **Bewertung:** keine Regression. GPU-Zeit unverändert (die Mip-Stufen sparen auf diesem Rechner keine messbare Bandbreite, kosten aber auch nichts), Speicher +4 bis +8 MB (+1 bis +2 %: Mip-Stufen, Pakete-Controller und Einstellungsseite), crowd p99 +9 % und Render-/GUI-CPU +6 % in den Spannen der Zweiteilung (p99 je Runde: 10,2/10,0, 15,9/16,0, 16,2/15,8, 9,6/15,2). 60 FPS weit gehalten.

#### Reihe B: Auflösungen auf `dev` d0c905c (4 Runden, `TEXTURE_PACKS` je Name)

| Szenario | Wert | keine (16×) | HD 32× | HD 64× | HD 128× |
|----------|------|------------:|-------:|-------:|--------:|
| stand (8) | FPS / p99 ms | 119,0 / 11,46 | 119,2 / 11,58 | 119,1 / 11,50 | 118,9 / 13,23 |
| stand (8) | GPU / Render-CPU / GUI-CPU ms | 1,649 / 0,875 / 0,234 | 1,687 / 0,885 / 0,235 | 1,683 / 0,884 / 0,235 | 1,681 / 0,881 / 0,238 |
| stand (8) | Spitzenspeicher MB | 433 | 447 (+14) | 456 (+23) | 501 (+68) |
| sprint (8) | FPS / p99 ms | 119,4 / 10,30 | 119,2 / 11,42 | 119,1 / 12,04 | 119,2 / 10,82 |
| sprint (8) | GPU / Render-CPU / GUI-CPU ms | 2,133 / 1,004 / 0,263 | 2,182 / 0,985 / 0,249 | 2,188 / 0,991 / 0,248 | 2,170 / 0,937 / 0,231 |
| sprint (8) | Spitzenspeicher MB | 598 | 608 (+10) | 619 (+21) | 664 (+66) |
| showcase (8) | FPS / p99 ms | 118,0 / 15,00 | 118,1 / 13,25 | 117,6 / 13,35 | 117,8 / 13,35 |
| showcase (8) | GPU / Render-CPU / GUI-CPU ms | 0,608 / 0,732 / 0,253 | 0,599 / 0,696 / 0,231 | 0,610 / 0,743 / 0,263 | 0,611 / 0,723 / 0,260 |
| showcase (8) | Spitzenspeicher MB | 307 | 321 (+14) | 329 (+21) | 377 (+70) |
| crowd (8) | FPS / p99 ms | 119,3 / 10,25 | 119,4 / 11,66 | 119,4 / 10,20 | 119,1 / 11,72 |
| crowd (8) | GPU / Render-CPU / GUI-CPU ms | 2,050 / 0,768 / 0,453 | 2,109 / 0,846 / 0,494 | 2,064 / 0,824 / 0,440 | 1,876 / 0,773 / 0,333 * |
| crowd (8) | Spitzenspeicher MB | 441 | 452 (+11) | 460 (+19) | 500 (+59) |
| sprint (16) | FPS / p99 ms | 119,8 / 9,73 | 119,9 / 9,84 | 120,0 / 9,60 | 119,8 / 9,52 |
| sprint (16) | GPU / Render-CPU / GUI-CPU ms | 3,532 / 2,122 / 0,282 | 3,600 / 2,154 / 0,288 | 3,497 / 2,139 / 0,278 | 3,574 / 2,109 / 0,283 |
| sprint (16) | Spitzenspeicher MB | 1379 | 1387 (+8) | 1400 (+21) | 1432 (+53) |

\* crowd mit 128×: am Ende 60 statt 84 Wesen und Items in 3 von 4 Läufen (einmal 76; mit 64× in Reihe C ebenfalls 76): weniger zu zeichnen, daher weniger GPU- und GUI-Zeit. Die Werte von crowd mit 128× sind nicht vergleichbar; Ursache siehe „Zahl der Wesen in crowd“ unten.

Füllfarbe der durchsichtigen Texel (E-332) beim Atlasbau, `qtcraft-headless --atlas-stats` mit und ohne Füllung im selben Build: 128× 91–92 → 99–100 ms, ein prozedurales Testpaket mit 256× (6080² Pixel, 6 Stufen, 188 MiB) 362–367 → 393–394 ms (+8 %).

Atlas beim Betreten der Welt (Logzeile `Texture atlas:`, 16 Läufe je Paket, Laden der Pakete und Bauen zusammen, im GUI-Thread): 16× 380² Pixel, 2 Stufen, 0,7 MiB, 1–3 ms; 32× 760², 3 Stufen, 2,9 MiB, 5–6 ms; 64× 1520², 4 Stufen, 11,7 MiB, 18 ms; 128× 3040², 5 Stufen, 47,0 MiB, 95–99 ms (einmal 188 ms). Mit `qtcraft-headless --atlas-stats` (Release, ohne Spiel daneben) 0–2 / 3 / 15 / 90 ms; das sind mehr als in der Tabelle oben (52 ms für 128×), die Kacheln laufen auf dem gleichen Rechner zu anderer Zeit parallel (Last, Takt).

**Bewertung:** Die höheren Auflösungen kosten keine messbare Frame-Zeit: rund 118 FPS bei 8 Chunks mit 64× (117,6–119,4; Ziel 60 weit gehalten, auch mit 128× und bei 16 Chunks), GPU Ø −1 bis +3 % gegen 16×, Render- und GUI-CPU innerhalb der Zweiteilung. p99-Abstände über 10 % (sprint 32×/64× +11/+17 %, crowd 32×/128× +14 %, stand 128× +15 %) kommen je aus einem oder zwei Läufen in der 16-ms-Gruppe und wiederholen sich nicht in Reihe C. Erwartet und wiederholbar ist der Speicher: +8 bis +14 MB mit 32×, +19 bis +23 MB mit 64×, +53 bis +70 MB mit 128× (Atlas mit Stufen auf CPU und GPU, geladene Paketbilder; bei 8 Chunks +16 bis +23 % mit 128×). Einziger spürbarer Preis von 128× ist der Atlasbau beim Betreten der Welt (rund 0,1 s im GUI-Thread, einmal).

#### Paketwechsel zur Laufzeit

Neuer Messhaken `QTCRAFT_BENCHMARK_PACK_SWITCH=<ids>` (E-332, in den Skripten `PACK_SWITCH=<ids>`): Im Benchmark-Lauf werden die Pakete nach 10 s eingeschaltet und nach 20 s wieder aus; gemessen wird ab dem Wechsel bis 1 s nach dem neuen Atlas. „Atlas fertig nach“ (`atlas_ms`) reicht bis nach dem Slot `applyAtlas` im GUI-Thread, enthält also dessen Zeit (eigene Spalte). Gemessen wurde mit der ersten Fassung des Hakens, die jedes Paket einzeln mit `setActive` schaltete (bei einem Paket je Wechsel dasselbe wie die heutige Fassung, die die ganze Auswahl auf einmal setzt und nichts speichert). Release-Build von `feature/bench-28`, je Paket stand und sprint, 4 Runden à 30 s, manuelle Reihe mit Sperre.

| Wechsel | Atlas fertig nach ms | GUI-Thread in `applyAtlas` ms (davon Item-Icons) | längster Frame ms | Frames ≥ 50 ms | GUI-CPU max ms | Render-CPU max ms |
|---------|---------------------:|-------------------------------------------------:|------------------:|---------------:|---------------:|------------------:|
| keine → 128× | 130–137 | 5,3–5,8 (5,3–5,6) | 18,8–24,1 | 0 | 11,1–16,1 | 3,4–4,0 |
| 128× → keine | 25–30 | 2,9–4,4 | 17,9–25,2 | 0 | 9,7–16,0 | 1,8–2,6 |
| keine → 64× | 52–64 | 6,6–7,3 (6,5–7,3) | 18,4–23,4 | 0 | 13,1–14,6 | 2,1–4,2 |
| 64× → keine | 26–29 | 3,9–4,3 | 19,4–28,1 | 0 | 12,8–14,3 | 1,8–2,6 |

Zum Vergleich der längste Frame ganzer 20-s-Läufe ohne Wechsel (Reihe B, stand und sprint): 12,4–25,4 ms. **Bewertung:** Ein Wechsel kostet zwei bis drei verpasste VSyncs (längster Frame 18–28 ms bei 8,3 ms pro VSync) (GUI-Thread bis 16 ms im Frame der Übergabe: `applyAtlas` mit Icons 3–7 ms, dazu die Aktualisierung der Einstellungsdaten), keinen Hänger; der Atlas wird im Hintergrund gebaut, die Welt zeichnet so lange mit dem alten. Der Upload von 47 MiB auf dem Render-Thread fällt nicht auf (Render-CPU max 4 ms).

#### Sichtprüfung der Alpha-Abdeckung (E-300) und Korrektur (E-332)

Offscreen-Bilder derselben Szene (flache Welt, von einem Pfeiler in 16 m Höhe nach Norden: Reihen aus Eichenlaub, Fackeln, Mohn, Löwenzahn und hohem Gras in 12–112 m) und eines Waldrands (Seed 42, Spawn), `docs/screenshots/28-bench-*.png`:

- `28-bench-far-27-16.png` (`7c7c924`, ohne Mip-Stufen): Ferne in Ordnung, Gras und Laub körnig.
- `28-bench-far-28-16.png` (`d0c905c`): **Fehler.** Am Horizont bunte Streifen (Magenta, Gelb, Cyan), entferntes Laub und Pflanzen mit Magenta- und schwarzen Sprenkeln, hohes Gras ab etwa 20 m mit schwarzen Rändern; im Waldbild (`28-bench-forest-28-16.png`) rosa Pünktchen im fernen Gras. Dasselbe zeigten die Etappe-28-Bilder `28-landscape-*.png` (bunter Horizont).
- `28-bench-far-fix-16.png`, `28-bench-far-fix-64.png`, `28-bench-forest-fix-16.png`, `28-bench-forest-fix-64.png` (`feature/bench-28`): Horizont ruhig, keine Sprenkel, keine dunklen Ränder. Laub wirkt in der Ferne nicht zu dicht (geschlossene Flächen wie ohne Mip-Stufen), Fackeln, Blumen und hohes Gras verschwinden auch in 112 m nicht; Blumen und Gras werden ab etwa 30 m etwas voller (Abdeckungserhalt hebt Alpha nur an, E-300), gewollt.

Ursache und Korrektur siehe E-332: Die Block-Pipeline schrieb das Teil-Alpha der Mip-Stufen ins Bild, durchsichtige Texel waren schwarz. Jetzt schreiben undurchsichtige Blöcke und Wesen Alpha 1 (eigene Shader-Variante), durchsichtige Texel tragen die Farbe des nächsten gezeichneten. Ob das Fenster selbst die Streifen zeigte (macOS stellt die Ebene vermutlich undurchsichtig dar), ist nicht geprüft; Screenshots und durchsichtige Ziele zeigten sie sicher, die dunklen Ränder auch das Fenster. `28-*.png` sind mit dem korrigierten Stand neu erzeugt (`scripts/texturepack-shots.sh docs/screenshots build/release`).

#### Reihe C: Korrektur gegen `dev` (4 Runden, 16× und 64×)

Erster Ansatz (Farbschreibmaske ohne Alpha, Commit `fc5d1dc`) gegen `d0c905c`: GPU Ø showcase 0,579 → 0,788 ms (+36 %, in jeder Runde, mit 64× ebenso), stand +16 bis +22 % – eine Regression, daher ersetzt durch die Shader-Variante (`5c5a4d4`). Mit ihr:

| Szenario | Wert | `d0c905c` 16× | Korrektur 16× | `d0c905c` 64× | Korrektur 64× |
|----------|------|-------------:|--------------:|--------------:|--------------:|
| stand | FPS / p99 ms | 119,9 / 9,81 | 119,8 / 9,99 | 119,9 / 10,10 | 120,0 / 10,06 |
| stand | GPU Ø ms | 1,421 | 1,520 (+7 %) | 1,352 | 1,477 (+9 %) |
| sprint | GPU Ø ms | 1,774 | 1,781 (+0 %) | 2,000 | 1,779 (−11 %) |
| showcase | GPU Ø ms | 0,569 | 0,569 (+0 %) | 0,574 | 0,575 (+0 %) |
| crowd | GPU Ø ms | 1,970 | 1,959 (−1 %) | 1,544 | 1,788 (+16 %) * |
| alle | Spitzenspeicher MB | 437 / 600 / 308 / 442 | 431 / 596 / 308 / 439 | 457 / 619 / 334 / 458 | 453 / 622 / 330 / 460 |

\* crowd 64×: in allen acht Läufen 76 Wesen statt 84 (wie oben), GPU-Spannen 1,35–1,84 und 1,72–1,85 ms; die Korrektur lag in allen vier Runden darüber. Gegenprobe (Reihe D, 6 Runden, nur 64×, crowd und stand, `d0c905c` gegen `5c5a4d4` frisch aus `git archive`): crowd GPU Ø 1,870 → 1,830 ms (−2 %), GPU p99 2,41 → 2,41, Wesen am Ende in beiden Ständen 75–84 (die GPU-Zeit folgt der Zahl); stand GPU Ø 1,583 → 1,564 ms (−1 %), Render-CPU +3 %, GUI-CPU ±0. Keine Regression; die Zahl der Wesen am Ende von crowd schwankt auch mit 64× und auf beiden Ständen. GPU-Zeiten in stand und sprint streuen in dieser Reihe stark (stand 1,21–1,73 ms je Stand); die Abstände wechseln das Vorzeichen zwischen den Szenarien. Render- und GUI-CPU im Showcase lagen in Reihe C und der Reihe mit Schreibmaske für die Korrektur 13–25 % höher, in allen Thread-Werten gemeinsam (auch Weltrenderer und Uploads, deren Code unverändert ist). Gegenprobe mit 6 Runden (`d0c905c`, Build der Korrektur aus dem Worktree, derselbe Stand frisch aus `git archive`), showcase und stand: GUI-CPU +3 % / +9 % und +4 % / +6 %, Render-CPU +1 % / +3 % und +6 % / +7 %, GPU +0 bis +3 %, Runden mit wechselndem Vorzeichen: die zwei Lagen der CPU-Werte, keine Regression.

**Fazit Etappe 28:** Gegen Etappe 27 keine Regression in Frame-Zeit, GPU, CPU-Zeiten oder Speicher über der Schwelle (Speicher +1 bis +2 %). Höhere Auflösungen kosten nur Speicher (64× +19 bis +23 MB, 128× +53 bis +70 MB) und beim Betreten der Welt Zeit für den Atlas (128× rund 0,1 s); 60 FPS bei 8 Chunks halten alle Auflösungen, 64× ohne Einschränkung. Ein Paketwechsel kostet zwei bis drei verpasste VSyncs, keinen Hänger. Der eigentliche Befund war optisch: Mip-Stufen mit Teil-Alpha und schwarzen durchsichtigen Texeln erzeugten Streifen am Horizont und dunkle Ränder an Pflanzen; behoben (E-332), die Korrektur kostet keine Frame-Zeit und beim Atlasbau 1–8 ms bis 128× (256× rund 30 ms). Offen: welche Wesen in crowd verschwinden, hängt vom Startzeitpunkt ab (unten); ob die Streifen auch im Fenster sichtbar waren.

#### Zahl der Wesen in crowd

Die BENCH-Zeile meldet seit diesem Branch zusätzlich `server_mobs` und `server_items` (Kreaturen und Item-Stapel auf dem Server am Ende, im Server-Thread gezählt) und `inventory_items` (Items im Inventar des Spielers). Gegenprobe (crowd, je 4 Läufe 16× und 128×, Release): Der Server hat am Ende immer alle 24 Item-Stapel, das Inventar ist leer; die Vermutung des Reviews (ein Zombie stößt den Spieler in die Items, er sammelt alle 24 ein, 84 − 24 = 60) trifft nicht zu. Es fehlen Kreaturen: in den 84er-Läufen hat der Server 127 Kreaturen (mit den Tieren der Umgebung), in den 60er-Läufen 103, also 24 weniger. Eine einmalige Aufschlüsselung nach Art (Diagnose, nicht eingecheckt) zeigt, dass je nach Lauf alle 16 Zombies oder alle 8 Schleime fehlen, dazu bis 32 Schweine: 16× 75 Schweine, 36 Schafe, 16 Zombies, 0 Schleime (84 sichtbar); 128× 43 bzw. 59 Schweine, 36 Schafe, 0 Zombies, 8 Schleime (45 bzw. 60 sichtbar). Welche Kreaturen bis zum Ende des Laufs sterben oder verschwinden (Zombies in der Sonne, Kämpfe, Entladen), hängt vom Zeitpunkt des Starts ab, den der Atlasbau verschiebt; das Szenario bleibt wegen der Vergleichbarkeit mit der Historie unverändert. Werte von crowd sind nur zwischen Läufen mit gleicher Zahl `entities` und `server_mobs` vergleichbar.

**Nachtrag Etappe 30 (Ursache, E-351):** Die Wesen verschwanden nicht, sie wurden nie gerufen. `/summon` zählte für seine Grenze von 64 alle wachen Wesen, auch die Tiere der geladenen Chunks (E-086); je nach Ladestand beim Rufen lehnte der Server `zombie 16`, `slime 8` oder eine oder beide Schweinegruppen ab (alle Zählungen oben passen dazu). Seit Etappe 30 zählen nur gerufene Wesen, in `crowd` gelingen immer alle 56 Rufe. `crowd`-Werte ab Etappe 30 sind daher mit früheren nicht vergleichbar (mehr Wesen als in den meisten früheren Läufen); A/B-Vergleiche mit Ständen vor Etappe 30 laufen über `flat-crowd` (flache Welt ohne eigene Tiere, dort gelangen alle Rufe schon vorher). Das Szenario selbst ist unverändert.

## Etappe 29a: Signal Dust (nur headless, keine GUI-Messung)

GUI-Benchmarks sind auf Anweisung des Menschen ausgesetzt. Gemessen wurde nur die Signal-Phase im Server-Tick, headless mit `test_signal_measure` im Release-Build (`QTCRAFT_SIGNAL_MEASURE=full build/release/tests/test_signal_measure`, Apple M4 Max, sonst ruhiges System, je Szenario nach dem Aufbau, bis alle Bau-Änderungen abgearbeitet waren). Ganze Server-Ticks (alle Phasen, ein Spieler fern der Schaltungen), Arbeit = `QcServerCircuits::lastTickWork` (Budget 8192, E-335).

| Szenario | Größe | Ticks | Tick Ø ms | Tick max ms | Arbeit Ø | Arbeit max |
|----------|-------|------:|----------:|------------:|---------:|-----------:|
| Staublinie, Hebel jeden Tick umgeschaltet | 4096 Staub | 40 | 0,058 | 0,154 | 176 | 176 |
| Staubfeld, Hebel jeden Tick umgeschaltet | 64 × 64 | 40 | 0,361 | 0,458 | 895 | 895 |
| Repeater-Takte (Fackel + Repeater Verzögerung 4, gleichphasig) | 1000 | 100 | 1,915 | 3,310 | 6822 | 10963 |
| Klebkolben mit Block, alle 5 Ticks gemeinsam geschaltet | 500 | 40 | 1,061 | 2,840 | 3604 | 10000 |

Die Linie kostet unabhängig von ihrer Länge dasselbe (Region von 15 Verbindungen, E-335); das Feld kostet die Fläche, die ein Signal erreicht. Takte und Kolben überschreiten im Spitzentick das Budget um höchstens einen Schritt (geteilte Schritte, E-335 nach dem Review); alle Werte bleiben weit unter dem Tick von 50 ms. Die GUI-Messung (Szenario `showcase` an der Station) steht aus.

## Etappe 29.2: Signalmaschinen (nur headless, keine GUI-Messung)

GUI-Benchmarks bleiben ausgesetzt. Gemessen wie in Etappe 29a: `QTCRAFT_SIGNAL_MEASURE=full build/release/tests/test_signal_measure` (Release, Apple M4 Max, ganze Server-Ticks mit allen Phasen; die Trichter laufen am Ende der Phase `signal`, E-344). Arbeit = `QcServerCircuits::lastTickWork` (Signal-Budget 8192); Trichter haben ein eigenes Budget (4096 handelnde je Tick) und stehen in der zweiten Tabelle.

| Szenario | Größe | Ticks | Tick Ø ms | Tick max ms | Arbeit Ø | Arbeit max |
|----------|-------|------:|----------:|------------:|---------:|-----------:|
| Staublinie, Hebel jeden Tick umgeschaltet | 4096 Staub | 40 | 0,058 | 0,123 | 176 | 176 |
| Staubfeld, Hebel jeden Tick umgeschaltet | 64 × 64 | 40 | 0,359 | 0,504 | 895 | 895 |
| Repeater-Takte | 1000 | 100 | 2,061 | 3,468 | 6822 | 10963 |
| Klebkolben, alle 5 Ticks gemeinsam | 500 | 40 | 1,222 | 3,402 | 3604 | 10000 |
| Trichter in Zehnerketten, leer | 1000 | 100 | 0,007 | 0,018 | 0 | 0 |
| Trichter in Zehnerketten, voller Block darüber | 1000 | 100 | 0,006 | 0,018 | 0 | 0 |
| Trichter in Zehnerketten, 10 Items fallen je Tick hinein | 1000 | 200 | 0,141 | 0,218 | 95 | 116 |
| Sortierer (4 Einheiten), 200 gemischte Items | 200 Items | 2200 | 0,014 | 0,057 | 20 | 87 |
| Beobachter-Takte (je zwei gegenüber) | 500 | 100 | 0,894 | 1,927 | 2798 | 5596 |

| Trichter-Szenario | handelnd Ø/Tick | suchend Ø/Tick | geweckt Ø/Tick |
|-------------------|----------------:|---------------:|---------------:|
| leer | 0 | 0 | 0 |
| voller Block darüber | 0 | 0 | 0 |
| Items fallen hinein | 104 | 104 | 246 |
| Sortierer | 2,1 | 1,3 | 4,3 |

Untätige Trichter kosten nichts (sie schlafen, E-344): 1000 leere oder abgedeckte Trichter liegen im Rauschen des leeren Ticks. Unter vollen Blöcken wird nie gesucht. Fallende Items wecken nur die Trichter darunter; die Suche nutzt ein Zellenraster statt aller Items. Die vier Szenarien aus 29a liegen im Rahmen der damaligen Messung (Kolben Ø +0,16 ms bei gleicher Arbeit, Einzellauf ohne Wiederholung). Die GUI-Messung (Szenario `showcase` an den Stationen) steht aus.

### Nach dem Review (Etappe 29.2, B2 und S7)

Je drei Läufe `QTCRAFT_SIGNAL_MEASURE=full build/release/tests/test_signal_measure`, vorher = Stand vor dem Review (`674de90`), nachher = mit den Korrekturen. Tick Ø = Mittel der drei Läufe, max = größter Einzeltick aller Läufe; Arbeit = Signal-Arbeit je Tick (Mittel).

| Szenario (1000 Trichter) | Tick Ø ms vorher | nachher | Tick max ms vorher | nachher | Signal-Arbeit Ø vorher | nachher |
|---|---:|---:|---:|---:|---:|---:|
| beschäftigt, kein Komparator | 0,129 | 0,127 | 1,047 | 1,064 | 116 | 0 |
| beschäftigt, jeder von einem Komparator gelesen | 0,175 | 0,194 | 1,056 | 1,245 | 304 | 287 |
| 10 Items je Tick fallen hinein | 0,132 | 0,130 | 0,184 | 0,182 | 95 | 0 |
| voll, darüber bewegte Items (Wecken jeden Tick, Suche ohne Platz) | 0,313 | 0,278 | 0,613 | 0,590 | 251 | 0 |
| 4096 Items in der Welt (512 wach), fallende Items | 0,229 | 0,211 | 0,552 | 0,539 | 95 | 0 |
| untätig / voller Block darüber | 0,006 / 0,005 | 0,006 / 0,006 | 0,013 / 0,014 | 0,011 / 0,013 | 0 | 0 |
| Sortierer (4 Einheiten ohne Fackel), 200 Items | 0,013 | 0,014 | 0,065 | 0,069 | 24 | 22 |

Ohne lesenden Komparator kostet bewegter Inhalt die Signal-Phase nichts mehr; das Raster für die Suche nur über die Zellen suchender Trichter spart 7–11 % der Tickzeit, wenn viele Items in der Welt liegen. Die Szenarien aus 29a liegen im Rauschen (Leitung 0,057, Feld 0,308, Takte 2,00, Kolben 1,12, Beobachter 0,85 ms).

## Etappe 30: Wesen aus Blöcken schieben (nur headless)

Seit Etappe 30 prüft jeder Tick für jedes wache laufende Wesen, ob sein Körper Blöcke überlappt (E-352). Gemessen mit `QTCRAFT_MOB_MEASURE=1 build/release/tests/test_mobs measureWalkingCreatures` (Release, Apple M4 Max, sonst ruhiges System): 1000 Schweine laufen auf der Testwelt umher, ein Spieler in der Mitte, Mittel über 400 Ticks nach 100 Ticks Anlauf, je 3 Läufe.

| Stand | ms pro Tick (Phase Wesen) |
|-------|--------------------------:|
| ohne Prüfung | 0,825 / 0,875 / 0,819 |
| Prüfung mit `QcPhysics::collisionBoxesAt` (Liste je Zelle, wie zuerst umgesetzt) | 0,935 / 0,931 / 0,942 |
| Prüfung ohne Liste, Boxen direkt aus dem Zustand (Stand nach dem Review) | 0,844 / 0,844 / 0,819 |

Die Prüfung ohne Liste kostet im Rauschen der Messung (höchstens rund 20 ns je Wesen und Tick); mit Liste waren es rund +0,1 ms (+13 %) je 1000 Wesen. Die mittlere Zeile enthält schon die zusätzliche Zellebene darunter (Zäune), ist also etwas teurer als die erste Fassung.

## Etappe 31.1: Block-Entity-Register (nur headless)

Gemessen mit `QTCRAFT_BLOCK_ENTITY_MEASURE=full build/release/tests/test_block_entity_measure` (Release, Apple M4 Max): 441 Chunks mit je 5 gefüllten Truhen, 3 brennenden Öfen, 3 Trichtern unter Truhen und 3 Setzlingen (2205 Truhen, 1323 Öfen, 1323 Trichter, 1323 Setzlinge), 200 Ticks, danach erstes Speichern (alle Regionen), ein Tick, zweites Speichern, Öffnen der Welt in einem neuen Server. Vorher = `eef0382` (Code vor dem Register, Testprogramm um die Phasen-Ausgabe ergänzt, unter `$TMPDIR` gebaut), nachher = Stand nach E-357 bis E-361; verschränkt, je drei Läufe abwechselnd (E-358).

| Wert (Mittel aus 3 Läufen) | vorher | nachher | Änderung |
|---|---:|---:|---:|
| Tick Ø | 0,390 ms | 0,405 ms | +4 % |
| Phase Signal (Trichter) | 0,261 ms | 0,257 ms | −2 % |
| Phase Maschinen | 0,086 ms | 0,084 ms | −2 % |
| Phase Pflanzen (Setzlinge) | 0,032 ms | 0,052 ms | +20 µs |
| erstes Speichern | 21,6 ms | 22,7 ms | +5 % |
| zweites Speichern | 9,8 ms | 10,8 ms | +10 % |
| Öffnen | 32,0 ms | 35,0 ms | +9 % |

Eine erste Fassung mit einem `QHash` je Chunk und Art kostete Tick 0,58 ms (Maschinen 0,145, Signal 0,33 ms): Qt-Hashes haben Spans zu 128 Einträgen, das Durchlaufen vieler kleiner Hashes prüft je Chunk 128 Plätze; die Einträge liegen deshalb in einer Tabelle je Art mit einem Index je Chunk. Öffnen kostete zuerst 39 ms; das Anlegen der Container ankommender Chunks vergleicht jetzt Palettenindizes statt Blöcke. Mehrkosten bleiben bei den Setzlingen (Zeitgeber in einer nach Pflanzreihenfolge geordneten Tabelle statt einer Liste, rund 15 ns je Setzling und Tick) und beim Speichern (Datensätze je Chunk sortiert und je Region verglichen); beides unter der Schwelle.

Nach dem Review (Etappe 31.1, je drei Läufe verschränkt gegen `eef0382`, Mittel): Tick 0,389 → 0,390 ms, Phase Pflanzen 0,031 → 0,030 ms (Setzlinge über einen Gang durch die nach Pflanzreihenfolge geordnete Tabelle statt Liste und Suche je Setzling), Signal 0,261 → 0,262 ms, Maschinen 0,085 → 0,085 ms, erstes Speichern 22,0 → 22,8 ms (+4 %), zweites Speichern 9,9 → 10,7 ms (+8 %; Datensätze direkt aus dem Index statt über Kopien je Chunk; in diesem Szenario ändern sich mit den brennenden Öfen alle Regionen in jedem Speichern, dazu kommen Sortieren und das Umbenennen der Staging-Dateien), Öffnen 32,6 → 35,0 ms (+7 %).


## Etappe 31.1: gerenderte Messung (2026-10-14)

Ausnahme von „Keine GUI-Benchmarks“ für diese Etappe (Mensch abwesend). Release-Builds (`RelWithDebInfo`, Ziele `qtcraft` und `qtcraft-headless`) aus `git archive` unter `$TMPDIR`: `fe0a8c9` (`dev` mit Etappe 30) gegen `8aa4346` (31.1 mit Review-Fixes). `scripts/bench-ab.sh` mit rotierender Reihenfolge, echtes Fenster auf dem eingebauten Display im Hintergrund, Ton stumm, 20 s je Lauf, 8 Chunks, Mittel [Spanne]. Last vor und nach den Läufen 2,0–5,1; belastet markiert ist 1 von 128 Läufen der Hauptreihe (`systemstats` 87 % nach einem Lauf), ohne ihn ändert sich nichts. Zweimal kam während der Reihe Eingabe am Rechner an (23:48 und 00:47, HID-Leerlauf unter 60 s): Die Reihe wurde jeweils abgebrochen (die angebrochene erste Runde verworfen) und erst nach mehr als 10 min ohne Eingabe neu gestartet. Neue Szenarien in `scripts/bench-common.sh`: `showcase-<demo>` (Aussichtspunkt eines Demo-Bauwerks aus der Showcase-Tour des jeweiligen Builds), `liquid-fill`, `flight`, `showcase-flight`, `signal-stress`; `STATS=<ms>` schreibt die Debug-Daten als Zeitreihe ins Log.

### 31.1 gegen `fe0a8c9`

| Szenario (Runden) | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | GUI-CPU ms | Tick Ø ms | Tick max ms | Spitzenspeicher MB | Phasen Ø ms |
|---|---|---|---|---|---|---|---|---|---|
| stand (6) | 120 / 120 | 9,77 → 9,99 | 1,55 → 1,48 | 1,31 → 1,27 | 0,415 → 0,397 | 0,464 → 0,501 (+8 %) | 1,89 → 1,80 | 443 → 444 | Wesen 0,336 → 0,367 |
| sprint (6) | 120 / 120 | 10,0 → 9,83 | 1,84 → 1,91 | 1,34 → 1,32 | 0,403 → 0,396 | 0,427 → 0,484 (+13 %) | 2,00 → 1,96 | 607 → 606 | Wesen 0,307 → 0,350, Streaming 0,065 → 0,070 |
| sprint, 16 Chunks (4) | 120 / 120 | 9,31 → 9,24 | 3,39 → 3,38 | 2,03 → 2,08 | 0,292 → 0,284 | 0,927 → 0,789 (−15 %) | 2,90 → 2,49 | 1384 → 1385 | Chunk-Laden max 1,07 → 0,92 |
| crowd (4) | 120 / 120 | 10,1 → 9,92 | 1,81 → 1,99 (+10 %) | 1,03 → 1,03 | 0,658 → 0,673 | 0,629 → 0,594 | 2,54 → 2,09 | 447 → 452 | 94–95 Wesen sichtbar, 135 auf dem Server |
| flat-crowd (4) | 120 / 120 | 10,1 → 9,68 | 0,775 → 0,791 | 0,759 → 0,695 | 0,666 → 0,588 | 0,352 → 0,352 | 1,15 → 2,29 | 313 → 314 | |
| showcase (6) | 120 / 120 | 9,71 → 10,0 | 0,593 → 0,602 | 1,04 → 1,10 | 0,375 → 0,427 (+14 %) | 0,249 → 0,271 (+9 %) | 1,03 → 0,99 | 322 → 321 | Signal 0,095 → 0,107 |
| showcase-signal_components (8) | 120 / 120 | 9,98 → 10,1 | 0,599 → 0,599 | 1,09 → 1,12 | 0,362 → 0,392 | 0,222 → 0,248 (+12 %) | 0,51 → 1,28 | 327 → 325 | Signal 0,085 → 0,098 (+15 %), Maschinen 0,004 → 0,006 |
| showcase-signal_machines (8) | 120 / 120 | 9,97 → 10,1 | 0,526 → 0,528 | 0,969 → 0,976 | 0,584 → 0,609 | 0,237 → 0,231 | 0,80 → 0,76 | 322 → 324 | Signal 0,083 → 0,082, Maschinen 0,005 → 0,006 |
| showcase-signal_farm (8) | 120 / 120 | 10,1 → 9,97 | 0,514 → 0,515 | 1,07 → 1,08 | 0,375 → 0,375 | 0,240 → 0,301 (+26 %) | 1,06 → 1,51 | 323 → 321 | Signal 0,075 → 0,102 (+35 %), Maschinen 0,005 → 0,008 |
| liquids (6) | 120 / 120 | 9,93 → 10,2 | 1,24 → 1,22 | 1,22 → 1,27 | 0,388 → 0,424 (+9 %) | 0,454 → 0,449 | 1,89 → 1,64 | 458 → 456 | Flüssigkeiten 0,009 → 0,006 |
| liquid-fill (4) | 120 / 120 | 10,4 → 10,4 | 0,645 → 0,636 | 1,05 → 0,974 | 0,413 → 0,399 | 0,567 → 0,592 (+4 %) | 2,67 → 4,30 | 329 → 329 | Flüssigkeiten 0,339 → 0,345 (max 2,20 → 3,04), Blockupdates 0,103 → 0,107 |
| farm (4) | 120 / 120 | 10,4 → 10,3 | 0,760 → 0,762 | 1,06 → 1,00 | 0,381 → 0,345 | 0,036 → 0,034 | 2,11 → 1,69 | 326 → 326 | Pflanzen 0,012 → 0,012 |
| gravel-collapse (6) | 120 / 120 | 10,1 → 10,3 | 0,592 → 0,588 | 0,663 → 0,674 | 2,05 → 2,09 | 1,14 → 1,23 (+8 %) | 3,56 → 3,78 | 328 → 327 | fallende Blöcke 0,525 → 0,565 (+8 %) |

`server_block_entities` (nur 31.1): 99 in allen Showcase-Szenarien, 0 sonst; `block_entity_problems` = 0 in allen 107 Läufen mit diesem Feld (31.1 und die verworfene Speicher-Variante unten, alle Reihen einschließlich Flügen und großer Welt). Keine „Too many chests/machines“: Die Meldung gibt es nur beim Öffnen eines Bildschirms über der Grenze; die höchste Zahl in allen Läufen war 864 Einträge (Grenzen 16384 Container, 4096 Maschinen), und alle 3456 `openMachine` beim Bau der vier großen Welten gelangen.

**Bewertung der Hauptreihe:** Frame-Zeiten, GPU, Render-CPU und Speicher liegen in allen Szenarien innerhalb der Schwelle; 120 FPS überall (31.1 ändert keinen Code des Renderers). Über 10 % auf der ungünstigen Seite:

- **Tick sprint +13 %, stand +8 %** (je 6 Runden): zweigipflig (sprint 0,39–0,46 ms oder 0,58–0,63 ms je Lauf; zwei Läufe des neuen Stands in der oberen Gruppe), gleichmäßig über alle Phasen verteilt, am stärksten in der Phase Wesen, deren Code 31.1 nicht ändert; mit 16 Chunks −15 %. Gegenprobe headless (Weltgenerator Gelände, Seed 42, Spawn, 8 Chunks, je 4 Läufe verschränkt, 600 Ticks): Tick 0,723 → 0,675 ms, Wesen 0,536 → 0,494 ms. Keine Regression.
- **gravel-collapse Tick +8 %, fallende Blöcke +8 %** (Spannen fast ohne Überlappung): headless dieselbe Szene (je 3 Läufe, 400 Ticks): fallende Blöcke 0,945 / 1,191 / 0,936 gegen 0,962 / 0,940 / 0,978 ms, gleiche Zahl fallender Blöcke (2048 an der Grenze). Keine Regression.
- **crowd GPU +10 %**: 31.1 ändert nichts am Zeichnen; `fe0a8c9` selbst lag zwei Stunden später in der Reihe der nachgeholten Messungen bei 1,98 ms (unten). Streuung.
- **GUI-CPU showcase +14 %, signal_components +8 %**: zwei Lagen der GUI-CPU (0,25–0,29 oder 0,42–0,44 ms), der alte Stand hatte zwei Läufe in der unteren Lage; Mediane 0,424 → 0,425 ms.
- **Signal-Phase an den Stationen (`showcase-signal_components` +15 %, `showcase-signal_farm` +35 %)**: in einer zweiten Reihe (4 weitere Runden nur dieser Szenarien) bestätigt, also wiederholbar; absolut +0,013 bzw. +0,027 ms je Tick. Eingegrenzt und begründet in **E-362**: headless dieselben Szenen +5 % bzw. ohne gerichteten Unterschied, im `sample`-Profil kein heißer Pfad aus 31.1; ein kleiner, gewollter Teil ist das Durchlaufen der ungenutzten generierten Öfen (Maschinen-Phase +2–3 µs, E-357).
- **Tick max liquid-fill +61 %, signal_components ×2,5**: Einzelwerte (2,8–7,2 ms gegen 1,8–4,0 ms bzw. 0,5–2,8 ms), weit unter dem Tick von 50 ms, keine übersprungenen Ticks, Verspätung Ø gleich.

### Signal-Stress (Referenz und A/B)

`signal-stress`: flache Welt, 48 × 48 Staub-Feld, von 16 Beobachter-Takten alle 2 Ticks umgeschaltet; 200 Repeater-Takte (Fackel an einem Block, Staub über einen Repeater mit Verzögerung 4 zurück in den Block: 20 Ticks je Periode, 3 Wechsel in 60 Ticks, die Fackeln brennen nie durch) mit je zwei Lampen (je 10 Ticks an und aus, länger als die Ausschaltverzögerung 4); 32 weitere Repeater-Takte mit je zwei Klebkolben, die Stein heben und senken. Spieler auf einem Block 10 m hoch, Blick nach Norden, nächste Bauteile 15–21 m entfernt. Aufbau über rund 1950 Chat-Befehle beim Laden; Beobachter-Takte statt Fackel-Takte für das Feld, weil nur sie schneller als alle 4 Ticks takten, ohne dass etwas durchbrennt.

Der Lauf landet in einer von zwei Lagen, bei beiden Ständen (Ursache nicht untersucht; die Befehle kommen in allen Läufen vollständig an, vermutlich der Zeitpunkt, zu dem die Takte relativ zueinander starten): „hoch“ mit rund 31 500 Licht-Aufträgen und 33 000 neu vernetzten Sektionen in 20 s, „niedrig“ mit 14 300 bzw. 26 500. Verglichen wird je Lage (6 Runden aus zwei Reihen, dazu 2 Runden mit STATS):

| Wert | hoch: `fe0a8c9` (5) → 31.1 (2) | niedrig: `fe0a8c9` (3) → 31.1 (6) |
|---|---|---|
| FPS / Frame p99 ms | 118,4 / 14,6 → 117,8 / 15,9 | 120,0 / 9,67 → 120,0 / 9,87 |
| GPU / Render-CPU / GUI-CPU ms | 0,685 / 1,06 / 0,445 → 0,683 / 1,05 / 0,433 | 0,648 / 0,993 / 0,484 → 0,644 / 0,995 / 0,504 |
| Uploads Ø ms / MB in 20 s | 0,369 / 2025 → 0,362 / 1990 | 0,312 / 1398 → 0,312 / 1397 |
| neu vernetzte Sektionen / Mesh-Aufträge | 33 331 / 27 632 → 33 250 / 27 402 | 26 474 / 15 884 → 26 465 / 15 914 |
| Änderung bis Bild Ø ms | 22,3 → 26,3 | 12,5 → 12,4 |
| Licht-Aufträge / Ø ms / max ms | 31 474 / 0,027 / 0,55 → 31 482 / 0,028 / 0,45 | 14 281 / 0,024 / 0,39 → 14 278 / 0,024 / 0,54 |
| Tick Ø / max ms | 7,28 / 11,2 → 7,35 / 10,7 | 5,86 / 13,2 → 6,05 / 13,9 |
| Phase Signal Ø / max ms | 7,07 / 11,0 → 7,12 / 10,4 (+1 %) | 5,63 / 12,9 → 5,82 / 13,4 (+3 %) |
| Phase Blockupdates Ø ms | 0,151 → 0,166 | 0,151 → 0,148 |
| Spitzenspeicher MB | 584 → 580 | 457 → 460 |

Keine Regression (Signal +1 bis +3 % innerhalb der Lage). Headless (Release, 6 Chunks) läuft dieselbe Szene mit Signal Ø 8,8 ms in beiden Ständen gleich. **Referenz für spätere Etappen:** 117–120 FPS, p99 9,7–16 ms je nach Lage (verpasste VSyncs in der hohen Lage), Tick 6–7,4 ms, davon Signal 5,6–7,1 ms, Licht-Auftrag Ø 0,024–0,028 ms. `entities` am Ende schwankt mit der Phase der Kolben (bewegte Blöcke zählen als Wesen, 0–61) und ist nicht vergleichbar.

### Lange Flüge

`flight` (120 s, 3 Runden): Kreativ-Modus, Teleport alle 250 ms um 5 m nach Osten auf y 200 (20 m/s, 2,4 km neues Gelände); `showcase-flight` (80 s = eine Runde, und 240 s = drei Runden, je 2 Runden): dieselbe Geschwindigkeit auf y 110 um ein Quadrat von 400 m mit der ganzen Showcase-Welt, jeder Chunk verlässt die Sichtweite einmal je Runde und kommt zurück. STATS jede Sekunde. Bei Verzögerungen über 2 s laufen die Teleports über grobe Qt-Timer und kommen teils gebündelt an; für beide Stände gleich.

| Wert | flight | showcase-flight 1 Runde | showcase-flight 3 Runden |
|---|---|---|---|
| FPS / p99 ms | 120 / 10,0 → 120 / 10,0 | 120 / 9,87 → 120 / 9,38 | 120 / 9,87 → 120 / 10,3 |
| GPU / Render-CPU / GUI-CPU ms | 0,530 / 0,592 / 0,332 → 0,535 / 0,650 / 0,346 | 0,619 / 0,797 / 0,376 → 0,619 / 0,745 / 0,358 | 0,619 / 0,790 / 0,372 → 0,615 / 0,668 / 0,326 |
| Tick Ø / max ms | 0,646 / 2,90 → 0,681 / 3,86 | 0,259 / 2,44 → 0,254 / 1,47 | 0,382 / 3,61 → 0,357 / 2,80 |
| Verspätung Ø / p99 / max ms | 0,136 / 0,84 / 3,2 → 0,112 / 0,71 / 3,2 | 0,075 / – / 1,19 → 0,088 / – / 0,64 | 0,137 / – / 3,6 → 0,243 / – / 8,6 (ein Lauf 15,0, der andere 2,1) |
| Chunk-Laden Ø / max ms | 0,025 / 1,39 → 0,027 / 1,57 | 0,006 / 0,47 → 0,006 / 0,33 | – / 1,59 → – / 1,21 |
| Streaming Ø / max ms | 0,132 / 2,05 → 0,144 / 2,25 | 0,096 → 0,097 | – |
| Spitzenspeicher MB | 555 → 552 | 347 → 345 | 358 → 361 |
| Block-Entities am Ende (31.1) | 0 | 53 | 53 |

Über die Zeit (STATS): Beim Flug durch neues Gelände bleibt der Prozessspeicher bei 473–549 MB (Wellen mit dem Gelände, beide Stände gleich), geladen sind immer 380 Chunks beim Client und beim Server; kein Tick wird übersprungen. Im Showcase-Flug wächst der Speicher bei beiden Ständen gleich um rund 5 MB je Runde (331 → 346 bzw. 334 → 344 MB nach drei Runden), die Block-Entities bleiben nach einer wie nach drei Runden bei 53 (am Start mitten in der Showcase 99): leere, ruhende Container unveränderter Chunks gehen beim Entladen mit (E-358), **begrenzt**. Kosten des Ankommens von Chunks (Phase Chunk-Laden) und Verspätung liegen in der Streuung beider Stände; der eine Lauf mit 15 ms Verspätung hat keinen zweiten.

### Große gespeicherte Welt: Öffnen und Autosave

Welten je Stand mit dessen `qtcraft-headless` über die Steuerschnittstelle gebaut (flache Welt, Seed 42, Kreativ-Modus): auf einem Raster von 12 × 12 Stellen im Abstand von 6 Chunks (9 Regionen) je 4 Truhen mit 3 Stapeln und 2 Öfen mit 64 Eisenerz, in der Variante „brennend“ mit 16 Kohle (brennen länger als 10 min), in „kalt“ ohne Brennstoff: 576 gefüllte Truhen, 288 Öfen, 144 geänderte Chunks, 864 Block-Entities; Speicherung am Ende am Ursprung (rund 0,4 MB auf der Platte). Gemessen mit einem nur für die Messung in temporäre Kopien beider Stände eingebauten Haken (`QTCRAFT_BENCH_OPEN_WORLD=<Ordner>`: öffnet die gespeicherte Welt wie „Spielen“ im Menü und schreibt die Zeit bis zum Spielbeginn und bis zum ersten danach getauschten Bild; nicht im Repository), danach 130 s stehen mit zwei Autosaves (alle 60 s), STATS alle 200 ms (Phase „saving“ und Verspätung je Fenster). Eine frische Kopie der Welt je Lauf.

| Welt (Läufe je Stand) | bis Spielbeginn ms | bis erstes Bild ms | 1. Autosave ms | 2. Autosave ms | Verspätung max in Speicher-Fenstern ms | Tick Ø ms |
|---|---|---|---|---|---|---|
| brennend, Reihe 1 (3) | 151 → 139 | 155 → 143 | 6,0 [3,7–9,2] → 9,8 [8,2–10,7] | 5,3 [4,5–6,1] → 8,4 [6,7–9,3] | 0,8 → 5,0 | 0,31 → 0,27 |
| brennend, Gegenprobe (6, rotiert) | 132 → 149 | 151 → 154 | 7,9 [5,5–10,3] → 7,7 [5,8–11,3] | 7,6 [5,6–10,2] → 7,0 [5,9–7,9] | 1,9 → 1,0 | 0,32 → 0,28 |
| kalt (3) | 150 → 132 | 153 → 153 | 5,9 [5,7–6,3] → 5,2 [4,1–6,5] | 7,5 [4,9–9,4] → 6,6 [3,3–9,3] | 1,3 → 1,0 | 0,33 → 0,31 |

Das Speichern läuft im Tick (die Speicher-Phase ist der Tick-Stillstand: Tick max = Speichern + 0,1–0,3 ms). Die erste Reihe zeigte für die brennende Welt +65 %; die Gegenprobe mit 6 rotierten Runden wiederholt das nicht (−3 % / −8 %), headless 48 Speicherungen derselben Welt je Stand 8,79 → 8,95 ms (+2 %). Welche Dateien ein Speichern schreibt (headless, brennend): `fe0a8c9` 7 Dateien (`containers.qce` und `machines.qce` der ganzen Welt, Wesen, Spieler, `world.json` u. a.), 31.1 9 Dateien (die 4 Regionen mit brennenden Öfen in Reichweite über `.next`-Zwischennamen, ohne die beiden Ganzwelt-Dateien); kalt: 7 → 5 Dateien (ein Speichern ohne Änderung an Block-Entities schreibt keine Block-Entity-Datei). Eine Variante, die die `.next`-Dateien ohne den eigenen Zwischenschritt von `QSaveFile` schreibt (ein Umbenennen weniger je Region), brachte nichts Messbares (Fenster 9,0 / 7,6 ms, headless 8,86 ms) und wurde verworfen. **Keine Regression**; Stillstand je Autosave 3–11 ms bei beiden Ständen, keine übersprungenen Ticks. Öffnen bis zum ersten Bild 128–168 ms bei beiden Ständen; das erste Speichern einer frisch gebauten Welt (headless, `worldSave` nach dem Bau, alle Regionen neu) 8,6–13,1 ms gegen 10,2–10,5 ms.

### Flüssigkeiten (`placed()` mit schnellem Weg)

`liquid-fill` (Tabelle oben): Phase Flüssigkeiten 0,339 → 0,345 ms (+2 %) bei voller Schrittgrenze, Blockupdates 0,103 → 0,107 ms (+4 %). Die zusätzlichen Nachschlagen beim Setzen kosten nichts Messbares; die höheren Maxima (Flüssigkeiten max 2,20 → 3,04 ms) sind Einzelticks ohne Wirkung auf Verspätung oder Frames.

### Fazit Etappe 31.1

Im Fenster keine Regression über der Schwelle in Frame-Zeit, GPU, Render-/GUI-CPU, Speicher, Speichern oder Öffnen; Block-Entities beim Hin- und Herfliegen begrenzt, keine verletzten Regeln. Wiederholbar über 10 % ist nur die Signal-Phase an zwei Showcase-Stationen (+0,013 und +0,027 ms je Tick), headless nicht in dieser Größe nachweisbar und ohne Ursache im Profil; begründet in E-362 und zur Beobachtung in 31.2/31.3 vorgemerkt. Alle Tick-Werte unter 1,3 ms außerhalb der Stress-Szenen.

## Etappe 29.1: gerenderte Messung (nachgeholt 2026-10-14)

`234529c` (Benchmark von Etappe 28, Vorgänger in `dev`) gegen `393c1e7` (29.1), in einer Reihe mit 29.2 und 30 (vier Stände, 4 rotierte Runden, Bedingungen wie oben). Die Showcase-Welt bekommt mit 29.1 die Signal-Stationen; `showcase` sieht dadurch eine andere Szene, `showcase-signal_components` gibt es erst ab 29.1 (Referenz).

| Szenario | Wert | `234529c` | `393c1e7` (29.1) |
|---|---|---:|---:|
| showcase | FPS / p99 ms | 120 / 9,76 | 120 / 10,1 |
| showcase | GPU / Render-CPU / GUI-CPU ms | 0,572 / 0,940 / 0,370 | 0,598 / 1,09 / 0,456 |
| showcase | neu vernetzte Sektionen / Licht-Aufträge in 20 s | 81 / 15 | 276 / 47 |
| showcase | Tick Ø / max ms, Signal ms | 0,170 / 0,63, – | 0,212 / 1,18, 0,022 |
| showcase | Spitzenspeicher MB | 309 | 320 |
| showcase-signal_components | FPS / p99 ms | – | 120 / 9,78 |
| showcase-signal_components | GPU / Render-CPU / GUI-CPU ms | – | 0,573 / 1,03 / 0,365 |
| showcase-signal_components | neu vernetzte Sektionen / Mesh-Aufträge / Licht-Aufträge | – | 276 / 269 / 47 |
| showcase-signal_components | Änderung bis Bild Ø ms | – | 13,5 |
| showcase-signal_components | Tick Ø / max ms, Signal ms | – | 0,235 / 1,01, 0,024 |

**Bewertung:** Die Takte der Signal-Station (Staub, Lampen) lösen im Showcase rund 14 Neu-Vernetzungen je Sekunde aus; 120 FPS, GPU +5 % (mehr Bauwerke im Bild), GUI-CPU in den Spannen der zwei Lagen (0,30–0,44 gegen 0,45–0,46 ms), Speicher +11 MB (neue Bauwerke). Die Signal-Phase kostet an der Station 0,024 ms je Tick. Kein Befund.

## Etappe 29.2: gerenderte Messung (nachgeholt 2026-10-14)

`393c1e7` (29.1) gegen `3f29c94` (29.2), dieselbe Reihe. Die Maschinen-Station (Trichter, Sortierer) und die Wasser-Farm gibt es erst ab 29.2 (Referenz).

| Szenario | Wert | `393c1e7` | `3f29c94` (29.2) |
|---|---|---:|---:|
| showcase | FPS / p99 ms | 120 / 10,1 | 120 / 10,0 |
| showcase | GPU / Render-CPU / GUI-CPU ms | 0,598 / 1,09 / 0,456 | 0,594 / 1,01 / 0,366 |
| showcase | neu vernetzte Sektionen / Mesh-Aufträge / Licht-Aufträge | 276 / 241 / 47 | 6534 / 3723 / 515 |
| showcase | Tick Ø ms, Signal ms | 0,212, 0,022 | 0,286, 0,109 |
| showcase-signal_components | neu vernetzte Sektionen / Licht-Aufträge | 276 / 47 | 5930 / 515 |
| showcase-signal_components | Tick Ø ms, Signal ms | 0,235, 0,024 | 0,295, 0,126 |
| showcase-signal_machines | FPS / p99 ms, GPU / Render / GUI ms | – | 120 / 9,91, 0,524 / 0,959 / 0,550 |
| showcase-signal_machines | Tick Ø / max ms, Signal ms | – | 0,285 / 1,42, 0,102 |
| showcase-signal_farm | FPS / p99 ms, GPU / Render / GUI ms | – | 120 / 10,1, 0,517 / 1,09 / 0,382 |
| showcase-signal_farm | neu vernetzte Sektionen / Änderung bis Bild Ø ms | – | 1065 / 6,1 |
| showcase-signal_farm | Tick Ø / max ms, Signal ms | – | 0,243 / 1,11, 0,077 |

**Bewertung:** Mit 29.2 laufen in der Showcase dauerhaft Takte (Wasser-Farm alle 40 Ticks, Beobachter, Trichter), die vom Spawn aus im Bild sind: rund 330 Neu-Vernetzungen je Sekunde (vorher 14) und 26 Licht-Aufträge je Sekunde, Tick +0,07 ms durch die Signal-Phase (0,02 → 0,11 ms). Frame-Werte unverändert (120 FPS, GPU −1 %, GUI-CPU in den zwei Lagen), Uploads 132 → 185 MB in 20 s. Gewollt (neue Inhalte), kein Befund.

## Etappe 30: gerenderte Messung (nachgeholt 2026-10-14)

`3f29c94` (29.2) gegen `fe0a8c9` (30), dieselbe Reihe. Vergleich mit älteren Ständen über `flat-crowd` (E-354); `crowd` mit den neuen Zahlen als Referenz.

| Szenario | Wert | `234529c` | `393c1e7` | `3f29c94` | `fe0a8c9` (30) |
|---|---|---:|---:|---:|---:|
| flat-crowd | FPS / p99 ms | 120 / 9,98 | 120 / 10,3 | 120 / 10,3 | 120 / 10,3 |
| flat-crowd | GPU / Render-CPU / GUI-CPU ms | 0,780 / 0,691 / 0,605 | 0,785 / 0,674 / 0,600 | 0,774 / 0,691 / 0,630 | 0,776 / 0,732 / 0,653 |
| flat-crowd | Tick Ø / max ms, Wesen ms | 0,366 / 2,49, 0,169 | 0,361 / 2,01, 0,167 | 0,363 / 1,87, 0,170 | 0,317 / 1,09, 0,153 |
| flat-crowd | Spitzenspeicher MB, Wesen sichtbar / Server | 301, 80 / 56 | 311, 80 / 56 | 313, 80 / 56 | 310, 80 / 56 |
| crowd | Wesen sichtbar / Server | 76 / 119 | 72–79 / 112–120 | 72 / 112 | 93–95 / 135 |
| crowd | FPS / p99 ms | 120 / 10,3 | 120 / 10,3 | 120 / 10,3 | 120 / 10,3 |
| crowd | GPU / Render-CPU / GUI-CPU ms | 1,81 / 1,10 / 0,572 | 1,85 / 1,08 / 0,625 | 1,60 / 1,09 / 0,546 | 1,98 / 1,05 / 0,712 |
| crowd | Tick Ø ms, Wesen ms | 0,654, 0,463 | 0,733, 0,526 | 0,481, 0,343 | 0,727, 0,537 |
| crowd | Spitzenspeicher MB | 437 | 445 | 445 | 450 |

**Bewertung:** In `flat-crowd` (alle Stände rufen alle 56 Wesen) keine Verschlechterung durch Etappe 30: Tick −13 %, Wesen-Phase −10 % (das Prüfen auf Überlappung mit Blöcken, E-352, kostet hier nichts Messbares), Frame-Werte gleich (Render-/GUI-CPU +6 / +4 %). In `crowd` gelingen seit Etappe 30 alle 56 Rufe (Server 135 Kreaturen mit den Tieren der Umgebung, sichtbar 93–95 statt 72–79): GPU 1,98 ms, GUI-CPU 0,712 ms, Tick 0,73 ms als neue Referenz; die Abstände zu den älteren Ständen sind die zusätzlichen Wesen (E-351), kein Befund. 120 FPS in allen Ständen.

## Etappe 31.2: headless Messung (2026-10-14)

`d6568ba` (31.1) gegen `feature/31-2-entities`, Release, verschränkt. Keine gerenderte Messung (Vorgabe des Menschen für 31.2); was gerendert nachzuholen ist, steht in der Bewertung.

| Messung | Wert | `d6568ba` (31.1) | 31.2 | Differenz |
|---|---|---:|---:|---:|
| `test_entity_measure` (1323 Entities, 5 Läufe) | Tick ms | 0,600 | 0,602 | +0,3 % |
| | Phase Wesen / Items / Client-Sync ms | 0,478 / 0,094 / 0,021 | 0,471 / 0,092 / 0,020 | −1 / −2 / −5 % |
| | erstes / zweites Speichern ms | 9,9 / 1,95 | 10,6 / 2,62 | +7 / +35 % |
| | Öffnen ms | 14,0 | 14,8 | +6 % |
| headless `showcase-signal_farm` (3 Läufe) | Tick / Signal / Maschinen ms | 0,164 / 0,0435 / 0,0029 | 0,166 / 0,0389 / 0,0023 | +1 / −11 / −21 % |
| headless `showcase-signal_components` (3 Läufe) | Tick / Signal / Maschinen ms | 0,187 / 0,0439 / 0,0085 | 0,181 / 0,0404 / 0,0071 | −3 / −8 / −16 % |

**Bewertung:** Tick, Wesen, Items und Synchronisierung unverändert; das Speichern je Chunk kostet beim zweiten Speichern +0,7 ms (vier Regionsdateien statt dreier Weltdateien), als Voraussetzung für 31.3 hingenommen (E-369). Signal-Phase headless ohne den in E-362 gesehenen Abstand; untätige Maschinen werden übersprungen. Gerendert nachzuholen: `crowd`/`flat-crowd` (Entity-Synchronisierung und Darstellung mit der Art im Protokoll), `showcase` (Ständer und Items in nicht geladenen Chunks werden nicht mehr gesendet), `showcase-signal_*`, `gravel-collapse` (fallende Blöcke über das Register).

**Nach der Prüfung von 31.2** (Reihenfolge-Schlüssel ist die Entity-Id statt des Tabellenindex, Entity-Dateiversion 2, E-366): `test_entity_measure` verschränkt gegen `5c0fba7` (31.2 vor den Korrekturen), je 7 Läufe, Mediane: zweites Speichern 2,66 → 2,66 ms, erstes Speichern 10,97 → 11,34 ms, Öffnen 14,89 → 14,78 ms, Tick 0,617 → 0,610 ms. Das zweite Speichern bleibt gleich: In diesem Szenario bewegen sich die Tiere in jedem Tick, alle Regionen ändern sich ohnehin. Der Schlüssel wirkt beim Entfernen einer Entity (nur ihre Region wird neu geschrieben, `test_entity_model::removingAnEntityRewritesOnlyItsRegion`), nicht bei diesem Lauf.

## Etappe 31.2: gerenderte Messung (2026-10-14)

Nachgeholt mit Freigabe des Menschen („31.2 jetzt messen“, Mensch abwesend). Release-Builds (`RelWithDebInfo`, Ziele `qtcraft` und `qtcraft-headless`) in zwei temporären Worktrees: `d6568ba` (31.1) gegen `789e2d9` (31.2); die späteren `dev`-Commits ändern nur Web-Handbuch, Item-Icons und Pläne, keinen gemessenen Pfad. `scripts/bench-ab.sh` mit rotierender Reihenfolge, echtes Fenster auf dem eingebauten Display im Hintergrund, Ton stumm, 20 s je Lauf, 8 Chunks, Mittel [Spanne]. Hauptreihe 6 Runden (crowd, flat-crowd, showcase, showcase-signal_farm, showcase-signal_components, gravel-collapse), Wiederholung 6 Runden für die drei Szenarien über der Schwelle, dritte Reihe 6 Runden nur flat-crowd. Last vor und nach den Läufen 2,3–6,4; belastet markiert sind 6 von 120 Läufen (Safari oder `contactsd` um 100 % nach dem Lauf, einmal Last 6,4), ohne sie ändert sich keine Aussage. Keine Eingabe am Rechner während der Reihen (HID-Leerlauf alle 15 s geprüft).

### 31.2 gegen `d6568ba`

| Szenario (Runden) | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | GUI-CPU ms | Tick Ø ms | Tick max ms | Spitzenspeicher MB | Phasen Ø ms |
|---|---|---|---|---|---|---|---|---|---|
| crowd (6) | 120 / 120 | 12,2 → 11,1 | 2,05 → 2,01 | 1,06 → 1,13 | 0,672 → 0,711 | 0,787 → 0,649 (−17 %) | 2,64 → 2,25 | 448 → 450 | Wesen 0,594 → 0,459, Chunk-Laden 0,001 → 0,018; 93–95 Wesen sichtbar, 135 auf dem Server (beide) |
| flat-crowd (12) | 120 / 120 | 10,6 → 13,1 | 0,796 → 0,800 | 0,752 → 0,716 | 0,694 → 0,649 | 0,335 → 0,392 (+17 %) | 0,96 → 1,94 | 313 → 314 | Wesen 0,167 → 0,192, Client-Sync 0,054 → 0,063, Chunk-Laden 0,001 → 0,010; 80 / 56 Wesen (beide) |
| flat-crowd (18, mit 3. Reihe) | 120 / 120 | 12,1 → 13,8 | | | | 0,426 → 0,434 (+2 %) | 1,57 → 1,74 | | Wesen 0,218 → 0,217 |
| showcase (6) | 120 / 120 | 12,4 → 10,3 | 0,596 → 0,590 | 1,06 → 1,08 | 0,400 → 0,413 | 0,400 → 0,363 (−9 %) | 2,70 → 1,74 | 325 → 321 | Signal 0,159 → 0,136, Chunk-Laden 0,001 → 0,012 |
| showcase-signal_farm (12) | 120 / 120 | 12,1 → 11,8 | 0,503 → 0,503 | 1,06 → 1,10 | 0,375 → 0,369 | 0,344 → 0,398 (+16 %) | 1,70 → 2,48 | 323 → 322 | Signal 0,109 → 0,124 (+14 %), Maschinen 0,010 → 0,009, Chunk-Laden 0,001 → 0,015 |
| showcase-signal_components (12) | 120 / 120 | 11,8 → 10,7 | 0,590 → 0,592 | 1,09 → 1,15 | 0,347 → 0,388 (+12 %) | 0,326 → 0,396 (+22 %) | 1,61 → 2,11 | 325 → 324 | Signal 0,127 → 0,156 (+23 %), Maschinen 0,008 → 0,008, Chunk-Laden 0,001 → 0,014 |
| gravel-collapse (6) | 120 / 120 | 10,4 → 10,4 | 0,587 → 0,588 | 0,672 → 0,675 | 2,16 → 2,16 | 1,14 → 1,19 (+5 %) | 4,49 → 4,09 | 333 → 330 | fallende Blöcke 0,524 → 0,516, Client-Sync 0,496 → 0,520, Chunk-Laden 0,001 → 0,029; 1480–1710 Entities beim Client (beide) |

`entity_problems` (nur 31.2) und `block_entity_problems` = 0 in allen Läufen; `server_block_entities` 99 in allen Showcase-Szenarien bei beiden Ständen, Wesen und Items auf dem Server (crowd 135 / 24, flat-crowd 56 / 24, Showcase 10 / 3) gleich, also sind crowd und flat-crowd vergleichbar. Keine übersprungenen Ticks.

**Bewertung:** Frame-Zeiten, GPU, Render-CPU und Speicher liegen in allen Szenarien innerhalb der Schwelle, 120 FPS überall; Entity-Darstellung (Vernetzung der Wesen 0,40 → 0,41 ms in crowd, 1,80 → 1,79 ms in gravel-collapse) unverändert. Über 10 % auf der ungünstigen Seite:

- **Tick in flat-crowd (+15 / +18 %), showcase-signal_farm (+13 / +18 %), showcase-signal_components (+18 / +25 %)**, in Haupt- und Wiederholungsreihe gleich gerichtet, also wiederholbar in diesen beiden Reihen. Der Zuwachs verteilt sich gleichmäßig über alle Phasen, auch über solche, deren Code 31.2 nicht ändert (Signal, Blockupdates, Streaming je +10 bis +23 %), während crowd (−17 %) und showcase (−9 %) in derselben Reihe schneller sind. Gegenproben: (1) dritte Reihe flat-crowd (6 Runden, eine Stunde später): Tick 0,607 → 0,520 ms (−14 %), über alle 18 Runden +2 %; (2) headless dieselben Stationen (Showcase-Welt, Spawn an der Station, 8 Chunks, je 5 Läufe rotiert, 900 Ticks, Mittel über 8 × 100 Ticks aus `debug`): signal_components Tick 0,448 → 0,399 ms (−11 %), Signal 0,128 → 0,115 ms; signal_farm Tick 0,376 → 0,324 ms (−14 %), Signal 0,065 → 0,055 ms; (3) Time Profiler (`scripts/profile.sh time`, je 2 Läufe signal_components): Tick 0,490 / 0,450 gegen 0,371 / 0,476 ms, kein Server-Pfad von 31.2 unter den heißen Funktionen; bei 0,07 ms je Tick (rund 30 Proben in 20 s) ist das Profil dafür zu grob. Die Lage des Server-Threads (Kern und Takt bei rund 1 % Last) verschiebt alle Phasen gemeinsam; dieselbe Zweigipfligkeit wie bei 31.1 (E-362, sprint +13 %). **Keine Regression.**
- **Phase Chunk-Laden +0,010 bis +0,029 ms je Tick** in allen Szenarien, auch headless (+0,011 bis +0,013 ms): echt und gewollt. `QcEntities::settle` (E-363) setzt jeden Tick für jede Entity aller Arten `dormant` über eine Chunk-Abfrage; vorher prüfte jede Art in ihrer eigenen Phase (fallende Blöcke dort 0,0003 → 0,0001 ms). Am höchsten in gravel-collapse (bis 2048 fallende Blöcke, 0,029 ms = 2,4 % des Ticks). **Folgeaufgabe (31.3):** `settle` nur laufen lassen, wenn sich die Menge der geladenen Chunks oder die Chunk-Zugehörigkeit einer Entity ändert (passt zu Nehmen/Zurücklegen je Chunk), oder die Chunk-Abfrage je Chunk statt je Entity zwischenspeichern.
- **Frame p99 flat-crowd +24 % (12 Runden)**: p99 ist zweigipflig (9,2–10,5 ms oder 15,3–16,8 ms = verpasste VSyncs). In den ersten beiden Reihen lagen 6 von 12 Läufen von 31.2 oben gegen 1 von 12 bei 31.1, in der dritten Reihe 5 von 6 bei beiden; über alle Szenarien der Haupt- und Wiederholungsreihe 12 von 54 (31.1) gegen 11 von 54 (31.2). Die Frame-Kosten-p99 (GUI, Render, GPU) bleiben in jedem dieser Läufe bei 1,0–1,4 ms; die langen Abstände kommen nicht aus dem Spiel. Streuung.
- **GUI-CPU showcase-signal_components +12 %** (0,347 → 0,388 ms): die zwei bekannten Lagen der GUI-CPU (0,23–0,30 oder 0,40–0,44 ms), 31.1 hatte mehr Läufe in der unteren; in der Hauptreihe +1 %, signal_farm −2 %.
- **Tick max** in den drei Szenarien +31 bis +101 %: Einzelticks 2–4 ms, weit unter 50 ms, keine übersprungenen Ticks, Verspätung Ø gleich (±4 %, signal_components +13 % = 0,015 ms).

**Absolute Ziele (`docs/Performance.md`, 8 Chunks):** Bildrate 120 FPS in allen Läufen (Ziel ≥ 60) erfüllt; Frame-Kosten im Mittel höchstens 2,16 ms (GUI-CPU gravel-collapse) bzw. 2,05 ms (GPU crowd), Ziel ≤ 4 ms erfüllt; Spitzenspeicher höchstens 454 MB (crowd), Grenze 600 MB erfüllt. p99 ≤ 12,5 ms nur im Median der Szenarien (10,3–10,4 ms) erfüllt, nicht in jedem Lauf: 21 % der Läufe der ersten beiden Reihen (23 von 108) und 10 von 12 der dritten lagen bei 15–17 ms, bei beiden Ständen gleich und bei Frame-Kosten-p99 ≤ 1,4 ms; das ist der bekannte Vorbehalt „p99 nur auf ruhigem Desktop“ (Erfüllungstabelle), kein neuer Befund des Spiels. Der offene Befund „Spitzenspeicher sprint, 16 Chunks“ war nicht Teil dieser Reihe (nach 43 und 31.3).

**Nicht gerendert gemessen:** Autosave mit vielen Entities. Es gibt kein Fenster-Szenario dafür (die Messung der großen Welt bei 31.1 lief über einen nicht eingecheckten Haken); das Speichern läuft im Server-Tick und ist headless gemessen (zweites Speichern +0,7 ms, oben), die Prüfung des Speicherns über alle Regionsdateien steht in 31.3.

### Fazit Etappe 31.2

Im Fenster keine Regression in Frame-Zeit, GPU, Render-/GUI-CPU, Speicher oder Entity-Darstellung; crowd und flat-crowd mit gleichen Wesen- und Item-Zahlen, keine Entity-Probleme. Die wiederholten Tick-Abstände (+13 bis +25 % in drei Szenarien) sind eine Lage des Server-Threads, die alle Phasen gleichmäßig trifft: Eine dritte Reihe und headless dieselben Szenen zeigen 31.2 schneller (−11 bis −14 %). Einzige echte Mehrkosten ist `settle` in der Phase Chunk-Laden (0,01–0,03 ms je Tick), als Folgeaufgabe für 31.3 vorgemerkt.

## Etappe 43: Speicher der Übersetzungen (headless, 2026-10-14)

Keine gerenderte Messung (Vorgabe „Keine GUI-Benchmarks“). `0c11c73` (`dev` `eac19ad` plus Messtest) gegen `feature/43-translation-memory` (Grenze der Übersetzungen, E-371), Release (`RelWithDebInfo`), der alte Stand aus `git archive` unter `$TMPDIR`, Apple M4 Max, verschränkt (je Runde erst alt, dann neu), Last 2–5.

**Methode (zum Wiederholen):**
- `QTCRAFT_TRANSLATION_MEASURE=full build/release/tests/test_translation_measure <Fall>`, jeder Fall in einem eigenen Prozess (der Spitzenspeicher wächst nur). `baseGame`: Einträge je Sprache, Zahl der Teile, Ladezeit des Grundinhalts (`QcBaseContent::load`), Heap des Inhalts und einer tiefen Kopie aller Übersetzungstabellen (`malloc_zone_statistics`, Linux `mallinfo2`). `languageFiles`: Lesen und Parsen der Sprachdateien (Mittel über 20 Durchläufe). `modAtTheLimits`: ein Mod mit 34 Sprachen × 60 000 kürzesten Einträgen (16 Bytes je Eintrag in der Synchronisierung, knapp unter 32 MiB), `modBeyondTheLimits`: zwei Mods mit je 8 Dateien × 380 000 Einträgen (je knapp 32 MiB, zusammen fast die 64 MiB, die Mods lesen dürfen); jeweils Ladezeit (`QcModLoader::load`), Heap danach, Spitzenspeicher (`getrusage`) und `/usr/bin/time -l`. `largeLanguageFile`: Parsen einer Datei mit 380 000 Einträgen allein.
- Headless Lauf: `/usr/bin/time -l qtcraft-headless --script run.jsonl --no-mods --seed 42 --view-distance 8 --data-dir <tmp> --time-limit 300` mit `waitUntilReady`, `wait` 200 Ticks, `move` vorwärts sprintend und springend 400 Ticks, `wait` 100 Ticks (Client und Server in einem Prozess, Gelände-Welt, frisches Datenverzeichnis je Lauf); dasselbe mit `--mods-dir` auf den Mod von `modAtTheLimits` (als Dateien unter `$TMPDIR`).

| Messung | Wert | `0c11c73` (vorher) | Etappe 43 | Differenz |
|---|---|---:|---:|---:|
| Grundspiel (5 Läufe) | Einträge je Sprache (en, de) / davon Teile | 1306 / 50 | 1306 / 50 | 0 |
| | Übersetzungstabellen Heap MB (Median) | 0,707 | 0,707 | 0 % |
| | Grundinhalt laden ms (Median, ohne 1. Lauf) / Heap MB | 12,83 / 3,61 | 12,89 / 3,61 | +0,5 % / 0 % |
| | Sprachdateien lesen und parsen ms (2512 Einträge) | 0,95 | – | – |
| Mod an den Grenzen vor E-371 (derselbe Mod in beiden Ständen; nachher laden 4 seiner 34 Dateien, nahe der neuen Obergrenze 262 144; 3 Läufe) | Einträge nach dem Laden | 2 042 612 | 242 612 (6 Sprachen, Rest übersprungen) | −88 % |
| | Heap danach MB | 202,9 | 27,1 | −87 % |
| | Spitzenspeicher MB / Laden ms | 466 / 971 | 91 / 442 | −80 % / −54 % |
| Zwei Mods über den Grenzen (3 Läufe, beide abgelehnt bzw. ohne Sprachdateien) | Spitzenspeicher MB / Laden ms | 489 / 1861 | 371 / 443 | −24 % / −76 % |
| Eine Datei mit 380 000 Einträgen parsen | Spitzenspeicher +MB / ms | – | +38 / 26 | – |
| headless Lauf ohne Mods (3 Läufe) | Spitzenspeicher MB | 67,8 / 68,0 / 67,8 | 68,8 / 69,0 / 67,8 | 0 % |
| headless Lauf mit dem Mod (3 Läufe) | Spitzenspeicher MB | 549 / 546 / 553 | 135 / 135 / 143 | −75 % |

**Bewertung:** Das Grundspiel ist unverändert (0,7 MB Übersetzungen, 68 MB headless); der Posten „700–870 MB“ war der Grenzfall eines Mods und ist jetzt begrenzt (E-371). Die zwei abgelehnten Mods heben die Spitze weiterhin um rund 300 MB: Die Dateien werden geparst, bevor ihre Größe geprüft werden kann, der Heap ist danach wieder frei (bekanntes Restrisiko in `docs/Mod-Sicherheit.md`). Die Laufzeit beim Laden des Grundspiels ändert sich nicht messbar (+0,1 ms für die Prüfung der neuen Schlüssel).

**Absolute Ziele (`docs/Performance.md`):** Die Speicherziele gelten für das Spiel mit Fenster bei 8/12/16 Chunks und sind headless nicht prüfbar. Die Übersetzungen erklären den Befund „1 384 MB im Szenario sprint, 16 Chunks“ nicht (Grundspiel: 0,7 MB je Kopie); der Befund bleibt offen für 31.3 und eine gerenderte Messung.

## Etappe 43: gerenderte Messung (2026-10-14)

Etappen-Ende von Etappe 43 (Grenze der Übersetzungen, E-371), GUI-Benchmarks wieder erlaubt. Release-Builds (`RelWithDebInfo`, Ziel `qtcraft`): `dev` `eac19ad` in einem temporären Worktree gegen `feature/43-translation-memory` (`25b14b4`). Geändert ist nur das Laden des Inhalts (Prüfungen der Grenzen in Parser, Paket-Lader, Mod-Lader, Serialisierer); erwartet ist keine Änderung zur Laufzeit. `scripts/bench-ab.sh` mit rotierender Reihenfolge, echtes Fenster auf dem eingebauten Display im Hintergrund, Ton stumm, 20 s je Lauf, Mittel [Spanne]. Hauptreihe 6 Runden bei 8 Chunks (stand, sprint, showcase, crowd), Wiederholung 6 Runden für die drei Szenarien mit Werten über der Schwelle (stand, showcase, crowd), dazu sprint bei 16 Chunks 3 Runden für den offenen Speicher-Befund. Last vor und nach den Läufen 2,1–9,3; belastet markiert sind 3 von 90 Läufen (Safari 84–100 % nach zwei Läufen von 43, einmal Last 9,3 nach einem Lauf von `dev`, alle in der Hauptreihe), ohne sie ändert sich keine Aussage.

**Ladezeit:** Die BENCH-Zeile und das Log tragen keine Lade- oder Startzeit (nur Atlas: 2–3 ms Bau, 5,7–6,3 ms angewandt, bei beiden gleich). Die Ladezeit des Inhalts ist headless gemessen (oben: Grundinhalt 12,83 → 12,89 ms, +0,5 %).

### 43 gegen `eac19ad`

| Szenario (Runden) | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | GUI-CPU ms | Tick Ø ms | Tick max ms | Spitzenspeicher MB | Phasen Ø ms |
|---|---|---|---|---|---|---|---|---|---|
| stand (12) | 119 / 119 | 11,5 → 11,1 | 1,75 → 1,74 | 1,72 → 1,74 | 0,462 → 0,461 | 0,734 → 0,735 (0 %) | 3,02 → 2,64 | 445 → 444 | Wesen 0,518 → 0,517, Streaming 0,091 → 0,090; 92 Wesen auf dem Server (beide) |
| sprint (6) | 119 / 119 | 12,8 → 10,9 | 2,12 → 2,11 | 1,83 → 1,82 | 0,449 → 0,457 | 0,766 → 0,726 (−5 %) | 2,84 → 2,94 | 610 → 605 | Meshing je Sektion 1,30 → 1,34, Wesen 0,543 → 0,512 |
| showcase (12) | 119 / 119 | 13,3 → 12,3 | 0,671 → 0,679 | 1,21 → 1,24 | 0,372 → 0,380 | 0,359 → 0,371 (+3 %) | 1,51 → 1,30 | 324 → 322 | Signal 0,132 → 0,137, Client-Sync 0,020 → 0,022 |
| crowd (12) | 119 / 119 | 10,9 → 11,6 | 2,18 → 2,17 | 1,49 → 1,48 | 0,841 → 0,823 | 1,00 → 0,99 (−1 %) | 2,69 → 2,73 | 448 → 450 | Wesen 0,736 → 0,725; 93 Wesen sichtbar, 135 / 24 Wesen / Items auf dem Server (beide) |
| sprint, 16 Chunks (3) | 119 / 119 | 10,1 → 10,1 | 3,01 → 2,84 | 2,44 → 2,48 | 0,287 → 0,296 | 1,04 → 1,10 (+6 %) | 3,29 → 3,21 | 1370 [1328–1391] → 1386 [1386–1388] | Chunk-Laden 0,032 → 0,037, 1430 Draw-Calls, 5356 Sektionen, 349 MB Sektionspuffer (beide) |

`entity_problems` und `block_entity_problems` = 0 in allen Läufen, keine übersprungenen Ticks; Wesen- und Item-Zahlen bei beiden Ständen gleich (crowd vergleichbar).

**Bewertung:** Frame-Zeit, GPU, Render- und GUI-CPU, Tick Ø und Speicher liegen in allen Szenarien innerhalb von ±6 %, wie erwartet. Über 10 % lagen in der Hauptreihe:

- **Tick-Verspätung Ø crowd +18 % und stand +14 %, Tick max crowd +19 %:** in der Wiederholung crowd −3 % / −16 % und stand −10 % / −12 %, über 12 Runden crowd +7 % / +1 %, stand +3 % / −13 %. Nicht wiederholbar, Streuung der Einzelticks (2–5 ms, weit unter 50 ms).
- **Phase Client-Sync showcase +12 %** (0,020 → 0,022 ms, 2 µs): in der Wiederholung +6 %, über 12 Runden +9 %; Signal (±13 %) und Chunk-Laden bei 16 Chunks (+13 %, 5 µs) ebenso Mikrosekunden mit wechselndem Vorzeichen. 43 ändert keinen Tick-Pfad.
- **Frame p99** sprint −15 %, showcase −10 %, Wiederholung crowd +11 %: zweigipflig (10–12 ms oder 14–16 ms = verpasste VSyncs), 9 von 42 Läufen (`dev`) gegen 4 von 42 (43) oben, Frame-Kosten-p99 ≤ 3,3 ms in jedem Lauf. Streuung.

**Keine Regression.**

**Absolute Ziele (`docs/Performance.md`):** Bei 8 Chunks Bildrate 119 FPS in allen Läufen (Ziel ≥ 60) erfüllt; Frame-Kosten im Mittel höchstens 2,23 ms (GPU crowd), Ziel ≤ 4 ms erfüllt; p99 ≤ 12,5 ms im Median aller Läufe (11,0 ms) erfüllt, 13 von 84 Läufen bei 14–16 ms (bekannter Vorbehalt „nur auf ruhigem Desktop“). **Spitzenspeicher sprint bei 8 Chunks 604–612 MB** (beide Stände) über der Grenze 600 MB (Perf-Check, 20-s-Lauf; stand, showcase und crowd 318–454 MB): neuer Befund, nicht durch 43 (−5 MB), schon in Etappe 28 lag sprint bei 598 MB (16×-Atlas). Bei 16 Chunks: Bildrate 119 FPS, p99 10,1 ms (≤ 16,7), Frame-Kosten höchstens 3,0 ms (≤ 8,3) erfüllt; **Spitzenspeicher sprint 1386 MB** (`dev` 1370, ohne den ersten Lauf 1391) gegen ≤ 800 MB, unverändert gegen 31.1 (1 384 / 1 385 MB). Die Übersetzungen erklären ihn nicht (oben, headless); 349 MB davon sind Sektionspuffer der 5356 Sektionen. Beide Befunde bleiben offen für 31.3.

### Fazit Etappe 43

Im Fenster keine Änderung durch die Grenze der Übersetzungen: alle Kennzahlen innerhalb der Schwelle, die Ausreißer der Hauptreihe wiederholen sich nicht. Der Speicher-Befund bei 16 Chunks ist unverändert (1386 MB gegen ≤ 800 MB); neu festgehalten ist, dass sprint bei 8 Chunks mit 20-s-Läufen knapp über 600 MB liegt (604–612 MB, bei beiden Ständen).

## Etappe 31.3: headless Messung (2026-10-15)

`da33b56` (dev `d530a7f` plus E-372, also vor dem Entladen) gegen `feature/31-3-chunk-unloading`, Release, `qtcraft-headless` und `test_world_open_measure` als feste Binärdateien.

**Langer Erkundungsflug** (`scripts/explore-headless.sh <app> 300 8 10000`: Bot im Creative-Modus fliegt mit 20 m/s auf y 200 nach Osten, setzt jede Sekunde eine 3 × 3-Glasfläche auf y 70, also wird jeder überflogene Chunk geändert; Sichtweite 8, Server und Bot in einem Prozess; je ein Lauf von rund 510 s Wanduhr, Werte alle 10 s):

| | Server-Chunks am Anfang / nach 300 s / am Ende | Prozessspeicher am Anfang / am Ende (MB) | Tick Ø / max (ms) |
|---|---:|---:|---:|
| vorher | 306 / 518 / 669 (wächst um 7 je 10 s) | 74 / 96 | 0,801 / 4,99 |
| nachher | 306 / 306 / 306 | 72 / 80 | 0,712 / 5,53 |

Vorher blieb jeder geänderte Chunk bis zum Schließen der Welt geladen (und alle Wesen und Items im Register), die Zahl wuchs mit der Flugstrecke; nachher bleibt sie bei der Sicht (17 × 17 plus Rand), der Rest geht im Hintergrund in den Speicher. Der Speicher wächst nachher noch um 8 MB in 500 s (Allokator, Puffer der unverschlüsselt zu schreibenden Chunks bis zum Autosave, Client-Seite des Bots); vorher um 22 MB. Wesen (wache Kreaturen) gleich (kurzer Lauf von 100 s: Verlauf 71 → 25 → 120 gegen 71 → 19 → 106).

**Große Welt öffnen** (`QTCRAFT_WORLD_OPEN_MEASURE=full`: 41 × 41 = 1681 geänderte Chunks mit je zwei Tieren und einer gefüllten Truhe; verschränkt je 3 Läufe):

| | vorher | nachher |
|---|---:|---:|
| Öffnen | 89,4 / 90,3 / 93,9 ms (1681 Chunks geladen) | 2,9 / 3,0 / 3,0 ms (0 Chunks geladen, nur die Indizes) |
| Öffnen bis der Spieler in geladenen Chunks steht | 6,9 / 7,1 / 7,2 ms | 8,0 / 8,1 / 8,1 ms (ein Tick, 3 × 3 Chunks gelesen) |
| erstes Speichern der 1681 Chunks (wartend) | 45,8 / 46,5 / 48,2 ms | 73,5 / 73,6 / 73,9 ms |

**Bewertung:** Ziel der Etappe erreicht: Server-Chunks und -Speicher beim Erkunden begrenzt, Öffnen großer Welten unabhängig von ihrer Größe (30-mal schneller bei 1681 Chunks). Das wartende Speichern aller Chunks wird um 58 % langsamer (Komprimieren der Datenblöcke, Index, Journal); das Autosave wartet aber nicht mehr, der Tick sammelt nur die Datensätze, Verschlüsseln und Schreiben laufen auf dem Thread von `QcChunkIo` (E-373). Tick-Zeit im Flug −11 % (keine Ruhe-Prüfung aller Entities je Tick mehr, E-372, kleineres Register). Gerendert nachzuholen (Hauptsitzung): Szenario `explore` (neu, `STATS=5000`, lang) A/B, `sprint` bei 8 und 16 Chunks gegen die Ziele 600/800 MB (Befund in `docs/Performance.md`), `showcase`, `showcase-flight` und die Signal-Szenarien (Gruppen, Tickets).

## Etappe 31.3: gerenderte Messung (2026-10-15)

Pflicht-Benchmark am Etappen-Ende (Mensch zuerst abwesend, GUI-Messungen erlaubt). Release-Builds (`RelWithDebInfo`, Ziele `qtcraft`, `qtcraft-headless`, `test_world_open_measure`): `dev` `d530a7f` in einem temporären Worktree (dazu nur der Messtest aus `1d5afde`) gegen `feature/31-3-chunk-unloading` (`9ffa0c4`). `scripts/bench-ab.sh` mit rotierender Reihenfolge, echtes Fenster auf dem eingebauten Display im Hintergrund, Ton stumm, 8 Chunks, Mittel [Spanne]. Reihen: Hauptreihe 6 Runden à 20 s (stand, sprint, showcase, showcase-signal_farm, showcase-signal_components, crowd, 06:43–07:14), `explore` 6 Runden à 300 s mit `STATS=5000` (07:15–08:16), `showcase-flight` 6 Runden à 80 s (eine Runde um die Showcase-Welt) mit `STATS=1000`, sprint bei 16 Chunks 3 Runden, Wiederholung 6 Runden der Szenarien über der Schwelle (signal_farm, signal_components, showcase, sprint; 08:38–08:58). Last vor und nach den Läufen 2,0–7,2; belastet markiert 13 von 150 Läufen (Safari, Teams oder `suggestd` über 80 % nach dem Lauf, Last bis 7,2), ohne sie ändert sich keine Aussage. HID-Leerlauf alle 15 s protokolliert.

**Externer Monitor und Mensch am Rechner:** Um 07:13:25 kam Eingabe an (der Mensch schloss nach eigener Angabe gegen 07:00–07:10 einen externen Monitor an; das Fenster blieb auf dem eingebauten Display). Genau in diesem Lauf (Hauptreihe, Runde 6, `showcase-signal_components` von `dev`, 07:13:26–07:13:46) fallen Render-CPU (0,73 statt 1,3 ms), GUI-CPU und Tick (0,22 statt 0,36–0,50 ms) aus der Reihe; er ist unten herausgenommen. Danach liegt bei beiden Ständen gelegentlich eine zweite, niedrigere Lage der Render-CPU (0,7 statt 1,3 ms) vor, frame Ø und GPU bleiben gleich. Ab 08:07 wurde der Rechner laufend benutzt: die sechste Runde von `explore`, die Reihen `showcase-flight` und sprint 16 Chunks und die ganze Wiederholung liefen mit Mensch am Rechner. Weil jede Runde beide Stände im Abstand von Sekunden misst, trifft das beide gleich; keine Aussage unten hängt allein an diesen Läufen (die Hauptreihe und fünf der sechs Erkundungsflüge liefen ohne Eingabe; Ausnahme: eine Eingabe um 07:53 in der vierten Runde von `explore`, ohne Auffälligkeit).

### 31.3 gegen `d530a7f`

| Szenario (Runden) | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | GUI-CPU ms | Tick Ø ms | Tick max ms | Spitzenspeicher MB | Phasen Ø ms |
|---|---|---|---|---|---|---|---|---|---|
| stand (6) | 119 / 119 | 12,5 → 12,0 | 1,75 → 1,76 | 1,80 → 1,83 | 0,472 → 0,489 | 0,800 → 0,780 (−2 %) | 2,87 → 2,40 | 444 → 445 | Chunk-Laden 0,031 → 0,010, Streaming 0,095 → 0,102, Speichern 0,0005 → 0,0020; 92 / 91 Wesen auf dem Server |
| sprint (12) | 119 / 119 | 13,1 → 13,6 | 2,13 → 2,13 | 1,80 → 1,79 | 0,453 → 0,443 | 0,776 → 0,790 (+2 %) | 2,91 → 2,86 | 609 → 610 [606–620] | Chunk-Laden 0,029 → 0,013, Streaming 0,102 → 0,108 |
| showcase (12) | 119 / 119 | 14,4 → 14,6 | 0,687 → 0,688 | 1,21 → 1,22 | 0,376 → 0,377 | 0,369 → 0,376 (+2 %) | 1,40 → 1,39 | 324 → 324 | Signal 0,134 → 0,135, Chunk-Laden 0,011 → 0,008, Streaming 0,089 → 0,096; Block-Entities 99 → 85 (unten) |
| showcase-signal_farm (12) | 118 / 118 | 15,6 → 15,8 | 0,643 → 0,643 | 1,24 → 1,23 | 0,355 → 0,351 | 0,348 → 0,398 (+15 %; Haupt +24, Wdh. +4) | 1,68 → 1,68 | 323 → 323 | Signal 0,102 → 0,117, Streaming 0,095 → 0,112, Client-Sync 0,023 → 0,025 |
| showcase-signal_components (11 → 12) | 118 / 118 | 15,0 → 15,1 | 0,698 → 0,708 | 1,26 → 1,28 | 0,344 → 0,349 | 0,362 → 0,385 (+6 %; Haupt +5, Wdh. +6) | 1,35 → 1,70 | 325 → 327 | Signal 0,135 → 0,136, Streaming 0,086 → 0,100, Client-Sync 0,015 → 0,018 |
| crowd (6) | 119 / 119 | 13,4 → 13,7 | 2,17 → 2,15 | 1,53 → 1,55 | 0,872 → 0,884 | 1,08 → 1,07 (−1 %) | 2,60 → 3,48 | 448 → 451 | Wesen 0,799 → 0,803, Chunk-Laden 0,035 → 0,013; 135 / 24 Wesen / Items auf dem Server (beide) |
| explore, 300 s (6) | 118 / 118 | 15,3 → 15,8 | 0,533 → 0,530 | 0,720 → 0,731 | 0,357 → 0,359 | 1,19 → 0,764 (−36 %) | 6,2 → 19,0 (ein Lauf 85,6) | 615 [605–627] → 601 [580–607] | Wesen 0,789 → 0,439, Chunk-Laden 0,094 → 0,047, Streaming 0,184 → 0,183, Speichern max 0,07 → 0,33 |
| showcase-flight, 80 s (6) | 118 / 118 | 15,3 → 14,7 | 0,681 → 0,675 | 0,649 → 0,572 | 0,293 → 0,272 | 0,302 → 0,245 (−19 %) | 2,00 → 1,80 | 346 → 346 | Signal 0,097 → 0,042, Chunk-Laden 0,014 → 0,016, Client-Sync 0,016 → 0,019; am Ende Wesen 10 → 0, Block-Entities 53 → 4 (unten) |
| sprint, 16 Chunks (3) | 118 / 118 | 13,9 → 14,6 | 2,89 → 2,86 | 2,22 → 2,21 | 0,271 → 0,268 | 0,934 → 0,911 (−2 %) | 3,25 → 3,05 | 1389 → 1390 | Chunk-Laden 0,029 → 0,012; 5356 Sektionen, 349 MB Sektionspuffer (beide) |

sprint, showcase und die Signal-Stationen über Haupt- und Wiederholungsreihe (12 Runden). `entity_problems` und `block_entity_problems` = 0 in allen 150 Läufen, keine übersprungenen Ticks.

**Langer Erkundungsflug (`explore`, 300 s, Mittel über 6 Läufe je Stand, Werte alle 30 s):**

| s | 0 | 30 | 60 | 90 | 120 | 150 | 180 | 210 | 240 | 270 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Server-Chunks `dev` | 380 | 409 | 447 | 484 | 522 | 560 | 598 | 634 | 672 | 710 |
| Server-Chunks 31.3 | 380 | 380 | 380 | 380 | 380 | 380 | 380 | 380 | 380 | 380 |
| Prozess MB `dev` | 461 | 488 | 505 | 552 | 503 | 536 | 536 | 531 | 563 | 572 |
| Prozess MB 31.3 | 462 | 490 | 505 | 545 | 494 | 526 | 530 | 528 | 551 | 559 |
| Tick Ø ms `dev` | 0,62 | 0,81 | 1,07 | 1,02 | 1,18 | 1,40 | 1,56 | 1,20 | 1,34 | 1,49 |
| Tick Ø ms 31.3 | 0,74 | 0,82 | 1,09 | 0,76 | 0,86 | 0,89 | 0,89 | 0,46 | 0,63 | 0,53 |

Der Server von `dev` hält jeden geänderten Chunk bis zum Schließen (am Ende 741–742, in jedem Lauf gleich, und 1238 Wesen im Register), 31.3 bleibt bei der Sicht (380, 17 × 17 plus Rand beim Client und Server gleich); Tick Ø wächst bei `dev` mit der Flugstrecke (Wesen der gehaltenen Chunks), bei 31.3 nicht. Der Prozessspeicher wellt sich bei beiden mit dem Gelände und endet bei 31.3 10–13 MB tiefer (Spitze 615 → 601 MB, −2 %): Der Server-Anteil ist klein, den Großteil hält der Client (Sektionen, Atlas, Puffer), der in beiden Ständen gleich ist. Wache Wesen (STATS) zu Beginn gleich (83 / 83), danach bei 31.3 weniger: `dev` zählt die Wesen der gehaltenen Chunks hinter dem Spieler mit; am Ende (Tundra, ohne Tiere) 82 gegen 0. Speichern im Tick höchstens 0,53 ms (Datensätze sammeln, E-373), Chunk-Laden Ø halbiert.

**Weltöffnung einer großen Welt** (headless, `QTCRAFT_WORLD_OPEN_MEASURE=full build/release/tests/test_world_open_measure`, 41 × 41 = 1681 geänderte Chunks mit je zwei Tieren und einer gefüllten Truhe, je 3 Läufe verschränkt, Sperre von Hand gehalten):

| | `d530a7f` | 31.3 |
|---|---:|---:|
| Öffnen | 78,5 / 78,2 / 81,9 ms (1681 Chunks geladen, +1,2 MB) | 2,3 / 2,6 / 2,8 ms (0 Chunks geladen, nur die Indizes, +0,1 MB) |
| Öffnen bis der Spieler in geladenen Chunks steht | 6,2 / 6,4 / 6,2 ms | 7,2 / 7,7 / 7,9 ms (ein Tick, 3 × 3 Chunks gelesen) |
| Beitreten eines Spielers während eines Hintergrund-Speicherns aller Chunks | – | 10,4 / 11,5 / 10,5 ms |

Das Speichern ist hier nicht vergleichbar (der Messtest von 31.3 speichert erst nach einem Hintergrund-Speichern derselben Chunks); für das erste Speichern aller 1681 Chunks gilt die headless Messung oben (45,8 → 73,5 ms, wartend; das Autosave wartet nicht). **Öffnen 30-mal schneller**, unabhängig von der Größe der Welt.

**Bewertung:** Ziel der Etappe gerendert bestätigt: Server-Chunks beim Erkunden begrenzt, Tick im Flug −36 %, im Showcase-Flug −19 %, Öffnen großer Welten in 3 statt 80 ms. Frame-Zeit, GPU, Render- und GUI-CPU und Speicher in allen Szenarien innerhalb der Schwelle, 118–119 FPS überall. Über 10 % auf der ungünstigen Seite:

- **Tick showcase-signal_farm +24 % in der Hauptreihe** (alle Phasen +20 bis +37 %, auch solche, die 31.3 nicht ändert: das Muster der Lage des Server-Threads wie in 31.1/31.2), in der Wiederholung +4 %, über 12 Runden +15 %; signal_components in der Hauptreihe +13,5 % allein durch den einen Lauf beim Anschließen des Monitors, ohne ihn +5 %, Wiederholung +6 %. **Gegenprobe headless** (Showcase-Welt, Spawn an der Station, 8 Chunks, je 5 Läufe rotiert, 1000 Ticks, Mittel der STATS-Fenster 3–10): signal_farm Tick 0,290 → 0,321 ms (+11 %, in allen 5 Runden höher), davon Signal 0,050 → 0,059, Streaming 0,088 → 0,101, Client-Sync 0,033 → 0,040, Speichern 0 → 0,002, Chunk-Laden 0,010 → 0,007 ms; signal_components 0,343 → 0,362 ms (+6 %), Signal +0,004, Streaming +0,006, Client-Sync +0,009 ms. Also **echte Mehrkosten von 0,02–0,03 ms je Tick** (rund 0,05 % des Tick-Budgets von 50 ms), keine Lage. Profil (`sample`, headless signal_farm, je 270 s, Server-Thread): neu sind `QcChunkLoading::unloadUnneeded` mit `neededChunks`/`addPlayerChunks` (190 Proben, vorher `QcChunkStreaming::unloadUnneeded` 143: die Menge der benötigten Chunks wird jeden Tick neu aufgebaut), `QcCircuitState::takeReaders` 37 → 84 und `QcServerCircuits::tick` 74 → 135 (Signal-Phase; Prüfung der Gruppen bzw. Tickets der Leser), dazu kleine Posten im Speichern (`QcChunkIo::takeSaves`, `QcServerPersistence::takeNewFailure`, `reportDamagedChunks`, je Tick abgefragt); `QcEntities::settle` 154 → 48 (Folgeaufgabe aus 31.2 erledigt). **Folgeaufgaben:** benötigte Chunks nur bei Wechsel des Spieler-Chunks, der Tickets oder der erzwungenen Chunks neu berechnen; die Prüfung je Signal-Leser zwischenspeichern; Client-Sync +0,007–0,009 ms headless ist im Profil nicht zuzuordnen.
- **Streaming +5 bis +19 %** in allen Szenarien (0,006–0,016 ms je Tick), gleich gerichtet in allen Reihen: dieselbe Ursache (benötigte Chunks je Tick), Folgeaufgabe oben.
- **Phase Speichern 0,0003 → 0,002 ms je Tick** in allen Szenarien: das Abfragen der Hintergrund-Ein-/Ausgabe je Tick (E-373), gewollt und vernachlässigbar.
- **Tick max explore 6,2 → 19,0 ms**: ein einziger Tick von 85,6 ms (Chunk-Laden 81,6, Nachrichten 115, Wesen 25 ms im selben 5-s-Fenster) im dritten Flug von 31.3 (07:43, keine Eingabe am Rechner), zugleich ein Bild von 505 ms: ein Stillstand des ganzen Prozesses, nicht der Phase allein; keine übersprungenen Ticks. Bilder über 100 ms kamen in beiden Ständen vor (`dev` 102 und 150 ms in zwei Flügen; 31.3 100 ms und im sechsten Flug mit Mensch am Rechner bis 1010 ms). In den übrigen fünf Flügen von 31.3 Tick max 4,1–6,1 ms. Nicht wiederholt (keine weiteren GUI-Läufe nach Anweisung des Menschen). **Folgeaufgabe:** headless langer Flug mit STATS je Sekunde, um einen Stillstand im Chunk-Laden (z. B. synchrones Lesen, das auf die Schreib-Schlange wartet) auszuschließen.
- **Tick-Verspätung Ø explore +22 %** (0,136 → 0,166 ms) und Tick max crowd +34 %, signal_components +25 %: Einzelticks von 2–5 ms, Verspätung weit unter einem Tick; Streuung bzw. der Stillstand oben.
- **Frame p99 sprint (Hauptreihe) +12 %**: zweigipflig (10–12 oder 14–16 ms), in der Wiederholung −1 %, über 12 Runden +4 %. Streuung.
- **Block-Entities 99 → 85 an der Showcase, 53 → 4 und Wesen 10 → 0 am Ende des Showcase-Flugs:** gewollt. `dev` behält gefüllte Truhen und Wesen von Chunks außerhalb der Sicht im Register (E-358 gibt nur leere frei), 31.3 nimmt sie mit dem Chunk in den Speicher. Gegenprobe headless (`/worldinfo`): in der Mitte der Showcase (Sicht über die ganze Welt) zeigen beide Stände 91 Behälter und 5 Maschinen; Hin- und Rückflug (40/−19 → −150/150 → 40/−19) gibt bei 31.3 73 → 0 → 73 Behälter und 10 → 0 → 10 Wesen, also nichts verloren.
- **Signal-Phase im Showcase-Flug −57 %, Tick −19 %**: die Stationen außerhalb der Sicht laufen bei 31.3 nicht mehr (Chunk entladen), bei `dev` schon.

**Absolute Ziele (`docs/Performance.md`):** Bildrate 118–119 FPS (Ziel ≥ 60) und Frame-Kosten (8 Chunks höchstens 2,2 ms GPU in crowd, 16 Chunks 2,9 ms) erfüllt. p99 ≤ 12,5 ms diesmal auch im Median verfehlt (14,5 ms über die 72 Läufe der Hauptreihe, 48 darüber), bei beiden Ständen gleich und schon ab dem ersten Lauf vor dem Monitor, Frame-Kosten-p99 höchstens 3,3 ms: verpasste VSyncs von außerhalb des Spiels (bekannter Vorbehalt). **Spitzenspeicher:** stand, showcase, Stationen und crowd 318–453 MB erfüllt; **sprint 8 Chunks 607–620 MB** (Mittel 610, `dev` 609) und **sprint 16 Chunks 1 390 MB** (`dev` 1 389) **weiter verfehlt, durch 31.3 nicht verändert**: Ein 20-s-Sprint hält auf dem Server kaum mehr Chunks als die Sicht, das Entladen hilft dort nicht; der Speicher liegt beim Client (bei 16 Chunks allein 349 MB Sektionspuffer). Im langen Flug senkt 31.3 die Spitze um 14 MB (615 → 601 MB, immer noch knapp über 600). Beide Befunde bleiben für die Neugliederung des Clients bzw. den Feinschliff der Darstellung (46/47) offen. Tick: kein Tick über 50 ms außer dem einen Stillstand von 85,6 ms oben, keine übersprungenen Ticks.

### Fazit Etappe 31.3

Das Entladen wirkt wie geplant: Server-Chunks und -Wesen bleiben beim Erkunden bei der Sicht, der Tick wächst nicht mehr mit der Flugstrecke (−36 % nach 5 km), große Welten öffnen in 3 statt 80 ms, nichts geht beim Entladen und Wiederladen verloren. Im Fenster keine Regression bei Frame-Zeit, GPU, Render-/GUI-CPU oder Speicher. Echte kleine Mehrkosten von 0,02–0,03 ms je Tick an den Signal-Stationen (benötigte Chunks jeden Tick neu, Prüfung der Signal-Leser, Abfrage der Hintergrund-Ein-/Ausgabe) und ein einzelner Stillstand von 86 ms in einem von sechs Flügen sind als Folgeaufgaben vermerkt. Die beiden Speicher-Befunde (sprint 8 Chunks über 600 MB, 16 Chunks 1 390 MB statt ≤ 800 MB) ändert 31.3 nicht; sie liegen beim Client.

### Folgeaufgaben nach der gerenderten Messung (headless, 2026-10-15)

Release-Builds (`RelWithDebInfo`), vorher `f82ce8b` (Stand der gerenderten Messung), nachher `a2039a6`; Sperre geprüft, keine Fensterläufe.

**1. Benötigte Chunks nicht mehr jeden Tick** (`ac56c49`, `0bfd938`, `f6daf79`): `QcChunkLoading::unloadUnneeded` überspringt den Durchgang, solange sich nichts geändert hat, wovon er abhängt (geladene Chunks, Marken der Gruppen, Spieler-Chunk, Sichtweite und gesendete Chunks je Spieler, Tickets, erzwungene Chunks, Spawn-Chunks, Lader, wartende Signale) und der vorige Durchgang keinen Chunk gehen ließ; dazu fragt das Streaming einen schon angeforderten Chunk nicht mehr jeden Tick nach seiner Gruppe. Test `aCutWireLetsTheFarPartLeave` (eine durchtrennte Leitung lässt den fernen Teil sofort gehen, obwohl der Spieler steht; ohne die Marken im Schlüssel bleibt er). An den Stationen (Showcase-Welt, Spawn an der Station, 8 Chunks, je 5 Läufe rotiert, 1050 Ticks, Mittel der STATS-Fenster 3–10, Echtzeit):

| Station | Tick Ø ms | Streaming ms | Signal ms | Client-Sync ms | Chunk-Laden ms |
|---|---:|---:|---:|---:|---:|
| signal_farm | 0,168 → 0,129 (−23 %) | 0,053 → 0,014 (−75 %) | 0,030 → 0,033 | 0,020 → 0,018 | 0,004 → 0,005 |
| signal_components | 0,235 → 0,148 (−37 %) | 0,061 → 0,013 (−78 %) | 0,066 → 0,051 | 0,026 → 0,019 | 0,005 → 0,005 |

Profil (`sample`, signal_farm, je 30 s): `unloadUnneeded` 16 → 1 Proben bei 82 / 73 Proben im Tick. Die Streaming-Phase liegt damit unter der von `dev` (0,088–0,101 ms in der Gegenprobe der gerenderten Messung).

**2. Prüfung je Signal-Leser:** Es gibt keine. `QcCircuitState::takeReaders` und `QcServerCircuits::tick` sind in 31.3 unverändert (nur die Kennzeichnung `signal` der Wartepositionen kam dazu); `takeReaders` sortiert die von Trichtern gemeldeten Komparatoren und liest ihren Block. Im Profil oben 4 / 3 bzw. 10 / 9 Proben; die Signal-Phase streut an den Stationen zwischen den Läufen stärker (0,021–0,043 ms) als ihr Unterschied. Der Zuwachs gegen `dev` im Profil der gerenderten Messung folgt aus der Arbeit der Szene (welche Trichter und Komparatoren laufen), nicht aus einer Prüfung, die sich zwischenspeichern ließe. Kein Zwischenspeicher.

**3. Stillstand im Chunk-Laden (85,6 ms):** nicht wiederholt.
- Schneller Flug `QTCRAFT_EXPLORE_MEASURE=300 tests/test_explore_measure` (neu, siehe `docs/Performance.md`; 6000 Ticks, Saatwert 42, Sicht 8, Speichern alle 1200 Ticks, je 6 Läufe rotiert, rund 3 s je Lauf): Tick Ø 0,104 → 0,089 ms (−15 %), p99 0,600 → 0,588 ms, Maximum typisch 0,9–1,0 ms. In 24 weiteren Läufen (12 je Stand) ein Tick von 15,5 ms (vorher) und einer von 8,4 ms (nachher, Streaming-Phase), sonst höchstens 1,2 ms.
- Zwei Flüge in Echtzeit mit Client (`scripts/explore-headless.sh`, 300 s, STATS je Sekunde, nachher-Stand): Tick max 10,6 und 6,2 ms, Chunk-Laden max 2,8 und 3,1 ms.
- **Ausgeschlossen am Code:** In der Phase Chunk-Laden wartet nichts auf die Platte: Ankünfte und Anforderungen laufen im Hintergrund; synchron gelesen (`QcChunkIo::readNow`, das auf einen laufenden Auftrag wie ein Speichern warten kann) wird nur über `QcServerWorld::loadChunk` (Befehle wie `/tp` an einen nicht geladenen Ort, Beitritt, Spawn-Suche, Streuner vor einem Speichern), und Gruppen werden nur dort sofort geladen. Entladen ist auf `chunkUnloadsPerTick` (32) begrenzt. Unbegrenzt ist das Zulassen angekommener Chunks: Nach einem Rückstau treten alle im selben Tick ein. **Wache:** Zulassen oder Entladen ab 20 ms in einem Tick steht jetzt mit Zahlen im Log (`a2039a6`). Dass zugleich ein Bild 505 ms brauchte, spricht für einen Stillstand des ganzen Prozesses, nicht des Server-Threads.
- **Beim Suchen gefunden** (Ticket-Tests unter Last, `4db72fc`): Lief das Ticket eines Chunks ab, in dem ein Signal noch wartete oder gerade einen Repeater-Tick geplant hatte, ging der Chunk mit dem Signal in den Speicher und niemand lud ihn wieder; die Tür blieb in etwa einem von 20 Läufen zu. Jetzt bleibt ein Ticket, solange ein Signal in seinem Chunk wartet, und reicht bis zu seiner Dauer weiter, solange eines durchläuft (E-378); die Tests warten auf das Signal statt auf eine feste Zahl Ticks (40 von 40 unter TSan-Last).

**4. Client-Sync +0,007–0,009 ms:** 31.3 ändert im Client-Abgleich nur das Anfordern fehlender Chunks (`QcChunkStreaming::stream` über `QcChunkLoading::request`, das je fehlendem Chunk und Tick die Gruppe suchte; jetzt nur beim ersten Mal) und die Umwandlungen `as<T>()` in `QcEntitySync` (im Release ohne Kosten). Nachher an den Stationen 0,020 → 0,018 und 0,026 → 0,019 ms; der Rest der Differenz zu `dev` ist nicht zuzuordnen.

**Messweise:** Die Erkundungsflüge in Echtzeit (je rund 9,5 min) wurden nach zwei Läufen abgebrochen; Tick-Kosten werden jetzt mit `test_explore_measure` ohne Echtzeit gemessen (oben).
