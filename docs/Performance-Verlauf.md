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
