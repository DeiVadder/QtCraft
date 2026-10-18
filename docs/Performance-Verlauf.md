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

## Etappe 33.1: Licht auf dem Server, headless Messung (2026-10-15)

Release (`RelWithDebInfo`): `dev` `b236bb7` in einem temporären Worktree gegen `feature/33-server-light`, ohne Fenster und ohne Echtzeit (E-388). Der Server hält jetzt Block- und Himmelslicht aller geladenen Chunks (E-389).

**Szenarien** (`scripts/bench-headless.sh`, 3 verschränkte Runden à 60 s Spielzeit, Mittel [Spanne]; `light` mit dem Fackelring des Spiel-Benchmarks als Chat-Zeilen, neu in dieser Etappe):

| Szenario | | Tick Ø (ms) | Tick p99 (ms) | Tick max (ms) | Phase | Spitzen-RSS (MB) |
|---|---|---:|---:|---:|---|---:|
| `light` | dev | 0,078 [0,076–0,080] | 0,104 | 0,134–0,180 | messages 0,0020 | 48,3 |
| | neu | 0,079 [0,076–0,083] | 0,109 | 0,117–0,205 | messages 0,0120 (Licht bei `/setblock`) | 50,8 |
| `gravel-collapse` | dev | 0,489 [0,481–0,502] | 1,375 | 1,46–1,59 | blockUpdates 0,030 | 70,3 |
| | neu | 0,488 [0,478–0,494] | 1,386 | 1,53–2,96 | blockUpdates 0,035 | 70,4 |
| `explore` | dev | 0,120 [0,119–0,121] | 0,558 | 0,61–0,73 | chunkLoading 0,016 / max 0,29 | 52,8 |
| | neu | 0,162 [0,161–0,163] | 0,792 | 1,06–1,12 | chunkLoading 0,036 / max 0,74 | 55,9 |

Eine erste Fassung, die das Licht ankommender Chunks ganz im Tick berechnete, kostete in `explore` Tick Ø 0,322 ms, p99 2,25 ms, max 2,7 ms (chunkLoading Ø 0,17, max 2,5 ms); seitdem berechnet der Hintergrund (Generator-Threads, I/O-Thread) das Licht eines Chunks für sich, der Tick verbindet nur noch die Ränder.

**Licht je Chunk** (`QTCRAFT_LIGHT_MEASURE=full build/release/tests/test_light_measure`: 17 × 17 Chunks des Geländes mit Seed 42, jeder Schritt einzeln gemessen):

| | Ø (ms) | max (ms) |
|---|---:|---:|
| Chunk ankommend, nur Blocklicht (was der Client rechnet) | 0,025 | 0,195 |
| Chunk ankommend, Block- und Himmelslicht im Tick | 0,077 | 0,281 |
| davon im Hintergrund: eigenes Licht des Chunks (`prepare`) | 0,063 | 0,198 |
| im Tick: Ränder verbinden | 0,012 | 0,086 |
| Chunk entladen | 0,016 | 0,100 |
| `QcServerWorld::setBlock` mit Lichtnachführung (4000 zufällige Änderungen) | 0,008 | 0,114 |
| Lichtabfrage aus der Engine / Flutung auf Anfrage (früher fürs Spawnen) | < 0,001 / 0,363 | 0,001 / 0,563 |

**Speicher:** 8,2 KB je geladenem Chunk (Spalte rund 1 KB, Abschnitte mit Zellen 2 KiB je Kanal); von 4624 Abschnitten haben 524 Blocklicht- und 482 Himmelslicht-Zellen, die übrigen sind einheitlich dunkel oder voll vom Himmel beleuchtet und belegen nichts. Bei 306 geladenen Chunks (Sicht 8) rund 2,5 MB, passend zum Mehr an Spitzen-RSS (+2,5 MB in `light`, +3,1 MB in `explore`).

**Bewertung:** Neuberechnung beim Laden ist schnell genug (0,06 ms im Hintergrund je Chunk gegen mehrere Millisekunden für das Erzeugen), also wird nichts gespeichert. `light` und `gravel-collapse` ändern den Tick nicht messbar. `explore` +35 % Tick Ø und +0,4 ms Spitze headless, das Verbinden der Ränder und das Entladen; headless sind die Werte rund zehnmal kleiner als im Fenster (E-388). Gerendert nachzuholen (Hauptsitzung): `@light` und `@core` A/B, `explore` mit `STATS=5000` (Tick-Spitzen beim Fliegen).

## Etappe 33.2: Renderer-Vorsorge, headless Messung (2026-10-15)

Release (`RelWithDebInfo`), `QTCRAFT_MESH_MEASURE=full build/release/tests/test_mesh_measure`: 15 × 15 Chunks des Geländes mit Seed 42 je Gebiet (Land um den Ursprung, Küste um Chunk (52, 26)), jeder Abschnitt einzeln vernetzt (mit Blocklicht-Schnappschüssen wie im Client), Puffergrößen wie der Renderer sie hochlädt (16-Bit-Indizes, wo sie passen). Je 3 Läufe vor (`c670ff4`) und nach den Änderungen (E-390):

| Gebiet | | vernetzte Abschnitte | Vernetzen Ø (ms) | max (ms) | Sektionspuffer (MB) |
|---|---|---:|---|---:|---:|
| Land | vorher | 1046 | 0,665 / 0,686 / 0,682 | 1,41–1,53 | 74,57 |
| | nachher | 1046 | 0,659 / 0,699 / 0,680 | 1,41–1,45 | 74,57 |
| Küste | vorher | 884 | 0,654 / 0,625 / 0,629 | 1,02–1,15 | 40,52 |
| | nachher | 884 | 0,607 / 0,639 / 0,635 | 1,00–1,06 | 40,52 |

Der Vertex bleibt 24 Byte (Normale und Materialklasse im freien Byte), die Wasser-Listen teilen nur auf, was vorher in der durchsichtigen Liste lag (Küste: 121 824 Wasser-Vertices = 7,7 % aller Vertices, 3,1 MB; Land 1528). Abschnitte mit abwechselnd Wasser und Glas (Läufe, mehr Draw-Calls): 0 in beiden Gebieten. Das Licht der eigenen Zellen (Himmelslicht kopiert, Blocklicht geteilt) ändert die Vernetzungszeit nicht messbar. Licht-Pool auf der GPU, falls ein Schritt es liest (heute keiner): Land 691 von 1046 Abschnitten mit Platz (5,4 MB), Küste 660 von 884 (5,2 MB). Gerendert nachzuholen (Hauptsitzung): `@core` A/B (Draw-Calls, GPU, Render-CPU), dazu ein Szenario mit Wasser.

Nach der Prüfung (E-391: Licht im Netz nur, wenn der Plan es liest), gleiche Messung, je 3 Läufe:

| Gebiet | vernetzte Abschnitte | Vernetzen Ø (ms) | max (ms) | Sektionspuffer (MB) |
|---|---:|---|---:|---:|
| Land | 1046 | 0,666 / 0,682 / 0,698 | 1,46–1,60 | 74,57 |
| Küste | 884 | 0,611 / 0,611 / 0,639 | 1,06–1,11 | 40,52 |

Innerhalb der Streuung der Messung davor (das Weglassen der Himmelslicht-Kopie ist nicht messbar). Mit `QTCRAFT_SECTION_LIGHT=1`: Land 691 Plätze (5,4 MB), Küste 660 (5,2 MB), wie zuvor. Der gerenderte Benchmark braucht zusätzlich ein Szenario in gefrorenem Gelände (Eis über Wasser: zwei Listen je Abschnitt).

## Etappe 33 gerendert (2026-10-15)

Pflicht-Benchmark am Etappen-Ende (GUI-Messungen vom Menschen freigegeben, externer Monitor angeschlossen, Fenster auf dem eingebauten Display im Hintergrund). Release-Builds (`RelWithDebInfo`, Ziele `qtcraft`, `qtcraft-headless`, `test_scenario_measure`): `dev` `b236bb7` in einem temporären Worktree gegen `feature/33-server-light` (`3ec2476` plus das neue Szenario `frozen`, `01effba`). Verschränkt mit rotierender Reihenfolge (E-331), Ton stumm, 8 Chunks, Mittel. Reihen: headless `@server @light` (`bench-headless.sh`, adaptiv, 60 Spielsekunden, 19:22–19:25); im Fenster `@core @light frozen` (`bench-ab.sh`, adaptiv 3 → 6 Runden à 10 s, 19:25–19:48), `explore` 3 Runden à 75 s mit `STATS=5000`, sprint 2 Runden à 20 s bei 8 und bei 16 Chunks (19:49–20:00). Last 2–4, belastet markiert 10 von 90 Läufen der Hauptreihe und je einer in explore und sprint; ohne sie ändert sich keine Aussage. Wanduhr aller Messreihen samt Builds rund 40 min.

**Neues Szenario `frozen`** (`scripts/bench-common.sh`): Stehen am Tag auf dem gefrorenen Meer der Tundra (Seed 42, `--spawn -325,-300 --look 270,-15`), Eis über Wasser bis zum Horizont. Jeder Abschnitt mit Wasser unter Eis zeichnet seit E-390 zwei Listen (Wasser, Durchsichtiges). Ein `SCENE_SPAWN` gilt im Fenster jetzt in jeder Welt (vorher nur in der Showcase-Welt). Wasser mit Glas zeigen `showcase` (Glasgehege, Wasser in Glas) und `glass`/`glass-full` (Eis, kein Wasser). **Bild:** Offscreen-Screenshots von `frozen` in beiden Ständen unterscheiden sich in 135 von 3,7 Mio. Pixeln (Toleranz 3 %; Wolken, Hand), also gleich.

### Fenster: 33 gegen `b236bb7`

| Szenario (Runden) | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | Welt-Renderer ms | GUI-CPU ms | Tick Ø ms | Spitzenspeicher MB | Draw-Calls |
|---|---|---|---|---|---|---|---|---|---|
| stand (3) | 117 / 117 | 14,9 → 14,8 | 1,74 → 1,65 | 1,71 → 1,71 | 1,02 → 1,04 | 0,450 → 0,439 | 0,577 → 0,567 | 443 → 442 | 347 → 347 |
| sprint (6) | 117 / 116 | 14,2 → 15,4 (+8 %) | 1,98 → 2,03 | 1,75 → 1,82 (+4 %) | 1,09 → 1,14 | 0,456 → 0,453 | 0,688 → 0,642 | 554 → 554 | 388 → 388 |
| showcase (3) | 117 / 117 | 15,5 → 15,7 | 0,701 → 0,675 | 1,25 → 1,25 | 0,540 → 0,546 | 0,535 → 0,523 | 0,363 → 0,358 | 325 → 327 | **148 → 180 (+22 %)** |
| crowd (6) | 118 / 117 | 14,4 → 14,6 | 2,21 → 2,25 | 1,41 → 1,48 (+5 %) | 1,11 → 1,16 | 0,849 → 0,847 | 0,849 → 0,803 | 449 → 448 | 345 → 345 |
| light (6) | 117 / 117 | 15,3 → 15,4 | 1,47 → 1,45 | 1,66 → 1,66 | 0,958 → 0,984 | 0,568 → 0,548 | 0,566 → 0,540 | 450 → 451 | 346 → 346 |
| glass (6) | 117 / 118 | 14,8 → 13,9 | 1,86 → 1,87 | 1,72 → 1,72 | 1,00 → 1,01 | 0,525 → 0,515 | 0,626 → 0,578 | 454 → 453 | 349 → 349 |
| glass-full (3) | 118 / 117 | 14,2 → 14,9 | 1,81 → 1,84 | 1,71 → 1,74 | 0,990 → 1,04 | 0,524 → 0,519 | 0,576 → 0,553 | 453 → 453 | 349 → 349 |
| void-light (6) | 117 / 117 | 15,3 → 15,0 | 0,655 → 0,672 | 0,916 → 0,940 | 0,132 → 0,140 | 0,405 → 0,420 | 0,111 → 0,111 | 255 → 257 | 4 → 4 |
| **frozen** (6) | 118 / 117 | 14,9 → 15,5 | 1,48 → 1,49 | **1,73 → 1,94 (+12 %)** | **1,06 → 1,30 (+23 %)** | 0,370 → 0,363 | 0,267 → 0,206 | 400 → 400 | **406 → 634 (+56 %)** |
| explore, 75 s (3) | 117 / 117 | 15,6 → 15,5 | 0,587 → 0,589 | 0,760 → 0,753 | – | 0,400 → 0,396 | **0,749 → 0,846 (+13 %)** | 555 → 554 | 42 → 45 |
| sprint, 20 s (2) | 117 / 118 | 14,7 → 14,4 | 2,14 → 2,07 | 1,85 → 1,86 | – | 0,475 → 0,469 | 0,758 → 0,752 | 609 → 608 | 400 → 400 |
| sprint, 16 Chunks, 20 s (2) | 118 / 119 | 13,7 → 14,6 | 2,95 → 2,85 | 2,46 → 2,51 | – | 0,311 → 0,298 | 0,916 → 0,901 | 1391 → 1393 | 1430 → 1433 |

Sichtbare Abschnitte, Dreiecke und Sektionspuffer in jedem Szenario gleich (z. B. frozen 306 Abschnitte, 344 300 Dreiecke, 65,2 MB). `entity_problems` und `block_entity_problems` = 0, keine übersprungenen Ticks, Tick max höchstens 5,6 ms (explore).

### Headless: 33 gegen `b236bb7` (Tick, Mittel über 3 bzw. 6 Runden)

| Szenario | Tick Ø ms | Tick p99 ms | Spitzen-RSS MB | Phasen Ø ms |
|---|---:|---:|---:|---|
| crowd | 0,128 → 0,131 (+2 %) | 0,161 → 0,167 | 47,5 → 49,9 | – |
| gravel-collapse | 0,493 → 0,494 | 1,40 → 1,43 | 69,5 → 70,7 | – |
| farm / farm-seeds | 0,007 → 0,007 / 0,016 → 0,016 | – | 42,5 → 43,6 | – |
| explore | 0,119 → 0,161 (+35 %) | 0,571 → 0,813 | 52,6 → 55,8 | Chunk-Laden 0,016 → 0,036, Streaming 0,048 → 0,074 |
| signal_components | 0,0220 → 0,0248 (+13 %) | 0,052 → 0,091 | 43,4 → 44,3 | Signal 0,0069 → 0,0096 (max 0,05 → 0,10) |
| signal_machines | 0,0257 → 0,0285 (+11 %) | 0,059 → 0,091 | 43,3 → 44,2 | Signal 0,0096 → 0,0124 (max 0,06 → 0,12) |
| signal_farm | 0,0193 → 0,0218 (+13 %) | 0,049 → 0,094 | 43,7 → 44,5 | Signal 0,0042 → 0,0065 (max 0,05 → 0,17) |
| light | 0,079 → 0,082 (+3 %) | 0,104 → 0,107 | 47,8 → 50,5 | Nachrichten 0,0019 → 0,0120 (Licht bei `/setblock`) |
| glass / glass-full | 0,080 → 0,080 / 0,079 → 0,080 | 0,106 → 0,107 | 48,0 → 50,4 | – |
| void-light | 0,004 → 0,004 | – | – | – |

**Bewertung:**

- **Bild unverändert, Frame-Zeit und GPU überall innerhalb von 3 %.** GPU in `frozen` 1,48 → 1,49 ms trotz 228 Zeichenaufrufen mehr.
- **Draw-Calls nur dort mehr, wo Wasser und andere durchscheinende Flächen in einem Abschnitt liegen:** `frozen` +228 (+56 %), `showcase` +32 (Wasser in Glas, Glasgehege am Wasser); alle übrigen Szenarien gleich (sprint ±2 aus dem Streaming). Wie in E-390 (3) erwartet.
- **Render-CPU `frozen` +12 % (Welt-Renderer +0,24 ms), in allen 6 Runden** (33: 1,82–2,01 ms, `dev` 1,64–1,80 ms). Ursache sind die zusätzlichen Zeichenaufrufe (rund 1 µs je Aufruf; Dreiecke, Abschnitte, Puffer und Uploads gleich); die Pipeline wechselt nicht (E-391). Kein Profil nötig: die Szenario-Statistik ordnet den Zuwachs vollständig zu. Absolut 1,94 ms gegen das Ziel 4 ms, Frame-Zeit unverändert. Abhilfe, falls nötig: Wasser und Eis eines Abschnitts in einen Puffer mit zwei Bereichen legen (ein Puffer, zwei Aufrufe ohne Neubindung) oder sie in der klassischen Darstellung wieder gemeinsam vernetzen; für Etappe 45 bleibt die Trennung gewollt. In `showcase` (+32 Aufrufe) nicht messbar.
- **Render-CPU sprint +4 % und crowd +5 %**: ohne mehr Draw-Calls, Runden überlappen (crowd 33: 1,31–1,59, `dev` 1,35–1,53), in der 20-s-Reihe sprint +0,5 %; Streuung.
- **Tick `explore` im Fenster +13 % (0,75 → 0,85 ms), headless +35 %**: das Serverlicht ankommender und entladener Chunks (Ränder verbinden, Entladen; Phasen Chunk-Laden und Streaming), wie in der headless Messung von 33.1 (E-389); +0,1 ms je Tick von 50 ms, Tick max 3,4–4,3 → 5,1–5,6 ms. Gewollt.
- **Tick an den Signal-Stationen headless +11 bis +13 %, p99 +50 bis +90 %**: Lampen und Fackeln, die schalten, führen jetzt das Serverlicht nach (Phase Signal +0,0025 ms Ø, Spitzen 0,1–0,17 ms). Absolut 0,003 ms je Tick; gewollt (E-389).
- **Spitzen-RSS headless +1 bis +3 MB**: Licht der geladenen Chunks (8,2 KB je Chunk, E-389); im Fenster nicht messbar.
- Tick Ø im Fenster in den übrigen Szenarien −2 bis −8 %, in `frozen` −23 %: gleiche Arbeit (headless gleich), Lage des Server-Threads; keine Aussage.

**Absolute Ziele (`docs/Performance.md`):** Bildrate 116–119 FPS (Ziel ≥ 60) und Frame-Kosten (8 Chunks höchstens 2,25 ms GPU in crowd, 1,94 ms Render-CPU in frozen; 16 Chunks 2,85 ms GPU) erfüllt. p99 ≤ 12,5 ms im Median verfehlt (15,0 ms bei beiden Ständen, 45 von 45 bzw. 43 von 45 Läufen der Hauptreihe darüber, externer Monitor angeschlossen), Frame-Kosten-p99 klein: verpasste VSyncs von außerhalb des Spiels, wie in 31.3 (bekannter Vorbehalt). **Spitzenspeicher:** stand, showcase, crowd, light, glass, frozen, explore 255–555 MB erfüllt; **sprint 8 Chunks (20 s) 607–610 MB** (10-s-Sprint 554 MB) und **sprint 16 Chunks 1 390–1 394 MB** weiter verfehlt (offene Befunde, Anteil des Clients; durch 33 nicht verändert: `dev` 609 / 1 391 MB). Tick: kein Tick über 50 ms, keine übersprungenen Ticks.

### Fazit Etappe 33

Serverlicht und Renderer-Vorsorge ändern das Bild nicht und kosten im Fenster keine Frame-Zeit und keine GPU-Zeit. Messbar sind die erwarteten Posten: Render-CPU +0,2 ms in gefrorenem Meer durch die getrennten Wasser-Listen (+56 % Zeichenaufrufe, alle Runden, absolut weit unter dem Ziel) und +0,1 ms Tick beim Erkunden durch das Licht ankommender Chunks auf dem Server. Keine unerklärte Regression. Die beiden Speicher-Befunde bleiben offen und unverändert.

## Etappe 32 gerendert (nachgeholt, 2026-10-15)

Nachgeholter Pflicht-Benchmark von Etappe 32 (Schienen und Loren, E-382 bis E-387; GUI-Messungen vom Menschen freigegeben, externer Monitor angeschlossen, Fenster auf dem eingebauten Display im Hintergrund). Release-Builds (`RelWithDebInfo`, Ziele `qtcraft`, `qtcraft-headless`, `test_scenario_measure`) in temporären Worktrees: `9cd256d` (`dev` vor Etappe 32) gegen `285324f` (Etappe 32), Skripte aus `dev` `ff21ef6`. Verschränkt mit rotierender Reihenfolge (E-331), Ton stumm, 8 Chunks, Mittel [Spanne]. Reihen: headless `@server` plus `showcase-rails showcase-rails_signal` (`bench-headless.sh`, adaptiv, 60 Spielsekunden, 21:14–21:16); im Fenster `@core` plus dieselben zwei Stationen (`bench-ab.sh`, adaptiv 3 → 6 Runden à 10 s, 21:17–21:30). Last 1,8–3,5, belastet markiert 2 von 54 Fensterläufen (je einer `stand`) und 1 von 72 headless (`farm`); ohne sie ändert sich keine Aussage. Wanduhr samt Builds rund 22 min.

**Szenarien:** Die Stationen `rails` (Schleife mit Lore, Rampe, Kolben) und `rails_signal` (Antriebs-, Melde- und Aktivierungsschienen, Trichter- und Kistenlore) laufen über das allgemeine `showcase-<demo>` (Haltepunkt `demo-rails` bzw. `demo-rails_signal` der Showcase-Tour des Builds); `9cd256d` kennt sie nicht und misst sie nicht (nur Werte von 32). Kein neues Szenario in `scripts/bench-common.sh`: Loren entstehen nur aus dem Lorenitem auf einer Schiene oder aus `minecarts` einer Struktur; `/summon` kennt keine Loren und die Szenen kennen keine Item-Benutzung, also gibt es ohne Code-Änderung weder „viele Loren im Bild“ noch eine Fahrt mit Kamera. Dafür bräuchte es z. B. `/summon minecart` oder einen Benchmark-Hook für Fahrten (offen).

### Fenster: 32 gegen `9cd256d`

| Szenario (Runden) | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | Welt-Renderer ms | GUI-CPU ms | Tick Ø ms | Spitzenspeicher MB | Draw-Calls |
|---|---|---|---|---|---|---|---|---|---|
| stand (6) | 116 / 117 | 14,8 → 14,2 | 1,74 → 1,68 | 1,85 → 1,81 | 1,10 → 1,07 | 0,486 → 0,470 | 0,651 → 0,712 (+9 %) | 442 → 444 | 347 → 347 |
| sprint (6) | 118 / 117 | 13,6 → 15,2 (+12 %) | 2,06 → 2,02 | 1,92 → 1,94 | 1,18 → 1,20 | 0,480 → 0,494 | 0,751 → 0,768 | 554 → 555 | 388 → 389 |
| showcase (6) | 115 / 116 | 15,7 → 15,4 | 0,687 → 0,697 | 1,31 → 1,32 | 0,568 → 0,570 | **0,410 → 0,547 (+33 %)** | **0,327 → 0,362 (+11 %)** | 321 → 325 | 152 → 148 |
| crowd (6) | 118 / 117 | 14,3 → 14,5 | 2,30 → 2,30 | 1,58 → 1,59 | 1,25 → 1,26 | 0,946 → 0,943 | 0,951 → 0,946 | 448 → 449 | 346 → 345 |
| showcase-rails (3) | – / 117 | 14,7 | 0,683 | 1,33 | 0,598 | 0,554 | 0,389 | 325 | 144 |
| showcase-rails_signal (3) | – / 116 | 16,0 | 0,662 | 1,34 | 0,623 | 0,542 | 0,376 | 326 | 134 |

Wesen und Entity-Vernetzung (Client, je Bild) und Lorenphase (Server):

| Szenario | Wesen | Entity-Vernetzung Ø ms | Phase Loren Ø ms | Server-Block-Entities |
|---|---:|---:|---:|---:|
| stand / sprint / crowd | gleich (19 / 19 / 94) | gleich | 0 → 0,014 / 0,017 / 0,018 (ohne Loren) | – |
| showcase | 4 → 10 | 0,065 → 0,198 | 0 → 0,030 | 85 → 44 |
| showcase-rails | 13 | 0,207 | 0,030 | 78 |
| showcase-rails_signal | 11 | 0,212 | 0,033 | 78 |

`entity_problems` und `block_entity_problems` = 0, keine übersprungenen Ticks, Tick max höchstens 5,7 ms (sprint).

### Headless: 32 gegen `9cd256d` (Tick, Mittel über 3 bzw. 6 Runden)

| Szenario | Tick Ø ms | Tick p99 ms | Spitzen-RSS MB | Phasen Ø ms |
|---|---:|---:|---:|---|
| crowd | 0,123 → 0,124 | 0,155 → 0,154 | 46,5 → 47,8 | Loren 0 → 0,0009 (ohne Loren) |
| gravel-collapse | 0,478 → 0,475 | 1,33 → 1,33 | 68,4 → 69,3 | – |
| farm / farm-seeds | 0,0070 → 0,0073 / 0,016 → 0,016 | – | 42,2 → 43,8 / 42,0 → 43,0 | – |
| explore | 0,118 → 0,118 | 0,561 → 0,557 | 51,7 → 52,9 | Loren 0 → 0,0007 |
| signal_components (6) | **0,0182 → 0,0220 (+21 %)** | 0,049 → 0,052 | 42,7 → 43,7 | Loren 0 → 0,0019, Signal 0,0061 → 0,0069 |
| signal_machines (6) | 0,0230 → 0,0253 (+10 %) | 0,054 → 0,058 | 42,8 → 43,7 | Loren 0 → 0,0019, Signal 0,0090 → 0,0096 |
| signal_farm (6) | **0,0158 → 0,0187 (+18 %)** | 0,044 → 0,047 | 42,9 → 43,8 | Loren 0 → 0,0019, Signal 0,0033 → 0,0041 |
| rails | – → 0,0220 | 0,053 | 43,8 | Loren 0,0018, Signal 0,0069 |
| rails_signal | – → 0,0250 | 0,054 | 43,6 | Loren 0,0019, Signal 0,0101 |

**Bewertung:**

- **Bild, GPU und Render-CPU unverändert** (alle `@core`-Szenarien innerhalb von 3 %); die Bahnstationen selbst kosten nicht mehr als der übrige Showcase (GPU 0,66–0,68 ms, Render-CPU 1,33–1,34 ms).
- **GUI-CPU `showcase` +33 % (+0,14 ms), in allen 6 Runden** (32: 0,533–0,565, `9cd256d` 0,383–0,445): die Loren der neuen Stationen sind im Bild (Wesen 4 → 10), und die Entity-Vernetzung steigt um denselben Betrag (0,065 → 0,198 ms je Bild). Rund 0,02 ms je Lore und Bild, etwa das Drei- bis Vierfache eines Tiers in `crowd` (0,006 ms je Wesen), vermutlich durch die Blockmodelle der Kisten- und Trichterlore. Neuer Inhalt im Bild, absolut klein (GUI-CPU 0,55 ms); kein Handlungsbedarf, Ansatz bei vielen Loren: Lorenmodelle zwischenspeichern und nur die Lage neu setzen.
- **Tick `showcase` im Fenster +11 % (+0,035 ms)**: die neue Phase Loren (0,030 ms) plus etwas Signal; vollständig zugeordnet, gewollt.
- **Tick an den Signal-Stationen headless +10 bis +21 % (+0,002 bis +0,004 ms)**: Phase Loren (0,0019 ms, die Loren der benachbarten Bahnstationen sind geladen) und Signal +0,0006 bis +0,0008 ms (Antriebs- und Melderails der Bahnstationen in Reichweite). Die Szenen sind nicht mehr dieselben: Haltepunkte gleich, aber die Umgebung ist anders belegt (Server-Block-Entities 77/85 → 44, Bahnstationen nebenan). Absolut 0,004 ms je Tick von 50 ms; erklärt, keine Regression im eigentlichen Sinn.
- **Phase Loren ohne Loren: headless 0,0007–0,0009 ms, im Fenster 0,014–0,018 ms** (stand, sprint, crowd). `QcServerMinecarts::tick` sammelt jeden Tick die Körper aller Wesen und Spieler (Lore anschieben) und ruft die Trichter-Loren auf, auch wenn es keine Lore gibt; ein früher Ausstieg ohne Loren spart das. Tick `stand` im Fenster +9 % (Runden überlappen: 0,62–0,70 gegen 0,67–0,76 ms) liegt zum Teil darin, der Rest ist Streuung; headless in den Szenen ohne Loren kein messbarer Unterschied.
- **Frame p99 `sprint` +12 %**: GPU, Render- und GUI-CPU gleich, p99 zweigipflig (`9cd256d` 10,3–14,8, 32: 14,1–16,4 ms); in `stand` −4 %, in `showcase` −2 %. Streuung der verpassten VSyncs (bekannter Vorbehalt), keine Regression.
- **Spitzen-RSS headless +0,9 bis +1,6 MB, im Fenster +0 bis +4 MB**: Inhalt und Code der Etappe (Loren, Schienen, Texturen).

**Absolute Ziele (`docs/Performance.md`):** Bildrate 115–118 FPS (Ziel ≥ 60) und Frame-Kosten (8 Chunks höchstens 2,30 ms GPU in crowd, 1,94 ms Render-CPU in sprint) erfüllt. p99 ≤ 12,5 ms verfehlt (14,2–16,0 ms, externer Monitor; bekannter Vorbehalt wie in 31.3 und 33). Spitzenspeicher stand, showcase, crowd und die Bahnstationen 321–449 MB erfüllt; sprint 555 MB im 10-s-Lauf wie zuvor. Tick: kein Tick über 50 ms, keine übersprungenen Ticks.

### Fazit Etappe 32

Schienen und Loren kosten im Fenster keine GPU- und keine Render-CPU-Zeit. Messbar und vollständig zugeordnet: GUI-CPU +0,14 ms im Showcase durch die Vernetzung der sichtbaren Loren (rund 0,02 ms je Lore) und die neue Server-Phase Loren (0,03 ms im Fenster bei den Showcase-Loren, 0,015 ms auch ohne Loren). Keine unerklärte Regression. Offen: Szenario mit vielen Loren und einer Fahrt mit Kamera (braucht `/summon minecart` oder einen Hook), früher Ausstieg der Lorenphase ohne Loren.

## Etappe 34.1: Feuer, headless Messung (2026-10-15)

Release (`RelWithDebInfo`), `QTCRAFT_FIRE_MEASURE=full build/release/tests/test_fire measureForestFire`: Wald von 160 × 160 Blöcken (Bäume alle 3 Blöcke, Stamm 4 hoch, Krone 3 × 3 × 2, die Kronen berühren sich) auf Steinboden, 25 Funken im Raster von 32 Blöcken, Ticks von Hand so schnell wie der Server kann, bis kein Feuer mehr brennt (keine Echtzeit, 0,8 s Wanduhr je Lauf). Standardgrenzen (`fireStepsPerTick` 256, `firesPerChunk` 256, `fires` 4096, `fireSpreadRadius` 12). 3 Läufe, alle gleich im Ablauf (deterministisch):

| | Lauf 1 | Lauf 2 | Lauf 3 |
|---|---:|---:|---:|
| Ticks bis aus | 1601 | 1601 | 1601 |
| Phase Feuer Ø (ms) | 0,336 | 0,337 | 0,339 |
| Phase Feuer max (ms) | 1,995 | 2,013 | 1,975 |
| Tick Ø (ms) | 0,500 | 0,500 | 0,503 |
| Tick max (ms) | 2,099 | 2,119 | 2,079 |

46 007 Feuer-Schritte (Ø 29 je Tick), 7833 entzündet, 5952 Blöcke abgebrannt, höchstens 2365 Feuer zugleich. Die Phase enthält das Setzen der Blöcke mit dem Nachführen des Server-Lichts (Licht 15 je Feuer, E-389) und die Nachricht an die Clients. Der höchste Tick (2,1 ms, 4 % des Budgets von 50 ms) liegt im Anstieg des Brandes. Die Kosten hängen von den brennenden Feuern ab, nicht von der Weltgröße: ohne Feuer kostet die Phase nur den Blick auf die leere Warteschlange. Pflicht-Test (`aForestFireEndsWithinBoundsOfTimeAndArea`, Wald 72 × 72, ein Funke): aus nach 1584 Ticks (Grenze 46 × 39 = 1794), 379 entzündet, 266 abgebrannt, 146 Spalten verändert (höchstens 25 × 25 = 625 erlaubt). Der gerenderte Benchmark Waldbrand (Licht- und Mesh-Last beim Client, verschränkter A/B-Vergleich) ist Teil von 34.2.

## Etappe 34.2: Zündquellen und brennende Körper, headless Messung (2026-10-15)

Release (`RelWithDebInfo`), `SCENARIOS="forest-fire crowd" scripts/bench-headless.sh <Datei> 3 60 new=build/release` (keine Echtzeit, unter 1 s Wanduhr je Lauf). Neues Szenario `forest-fire` (`scripts/bench-common.sh`, Gruppe `@fire`): flache Welt, 1024 Eichen im Abstand von 3 m auf 96 × 96 m nördlich des Spielers (Stamm 4, Kronen berühren sich), 25 Funken im Raster nach 3 s, `fireSpread` an; 60 s Spielzeit gemessen, der Brand läuft die ganze Zeit.

| | Lauf 1 | Lauf 2 | Lauf 3 |
|---|---:|---:|---:|
| Tick Ø (ms) | 0,264 | 0,264 | 0,265 |
| Tick p99 (ms) | 0,724 | 0,720 | 0,708 |
| Tick max (ms) | 0,975 | 1,055 | 0,981 |
| Phase Feuer Ø (ms) | 0,178 | 0,178 | 0,179 |
| Phase Feuer max (ms) | 0,741 | 0,807 | 0,752 |
| Phase Block-Updates Ø (ms) | 0,066 | 0,065 | 0,065 |
| Spitzen-RSS (MB) | 45,6 | 45,6 | 45,7 |

- Phase Loren in `crowd` (keine Loren): 0,0000 ms (früher Ausstieg, E-393; Etappe 32 maß hier 0,0007–0,0009 ms headless, im Fenster 0,014–0,018 ms).
- Die Kosten der brennenden Körper liegen in der Phase Wesen (`crowd`: Ø 0,113–0,115 ms, Tick gesamt 0,128–0,130 ms); ein A/B gegen `dev` steht aus (Hauptsitzung, `@server` und `@fire`).
- Offen: der gerenderte Waldbrand im Fenster (Licht- und Mesh-Last beim Client) mit `scripts/bench-ab.sh` und `SCENARIOS=@fire` durch die Hauptsitzung.

## Etappe 34 (Feuer)

Pflicht-Benchmark am Etappen-Ende (GUI-Messungen vom Menschen freigegeben, externer Monitor angeschlossen, Fenster auf dem eingebauten Display im Hintergrund). Release-Builds (`RelWithDebInfo`): `dev` `53707b8` in einem temporären Worktree gegen `feature/34-fire` `8fa475b`. Verschränkt mit rotierender Reihenfolge (E-331), Ton stumm, 8 Chunks, Mittel [Spanne]. Reihen: headless `@server @fire` (`bench-headless.sh`, adaptiv, 60 Spielsekunden, 22:52–22:55); im Fenster `@core` (`bench-ab.sh`, adaptiv 3 → 6 Runden à 10 s, 22:56–23:05) und `forest-fire` 3 Runden à 30 s (23:06–23:10). Last 1,5–4,0 im Fenster, belastet markiert 2 von 36 Läufen der `@core`-Reihe (je ein `sprint`), headless 1 von 60 (`explore`); ohne sie ändert sich keine Aussage. Wanduhr samt Builds rund 25 min.

**`forest-fire` auf `dev`:** `dev` kennt weder den Feuerblock noch die Spielregel `fireSpread`; die Chat-Zeilen `/gamerule fireSpread` und `/setblock … fire` laufen ins Leere, die Szene ist derselbe Wald ohne Brand. Der Vergleich zeigt also die Kosten des Brandes gegenüber dem stehenden Wald.

**Showcase verschoben:** Die neue Feuerstation steht in der ersten Reihe der Showcase-Welt; alle Stationen dahinter rücken (`signal_components` 17,−21 → 34,−21, `signal_machines` 134,−21 → 4,−38, `signal_farm` 4,−43 → 40,−38, Bahnstationen 26/46,−43 → 62/82,−38). Der Blick aus `showcase` (40,−19) und die Umgebung der Signal-Stationen sind darum nicht mehr dieselben Szenen (wie in Etappe 32).

### Fenster: 34 gegen `53707b8`

| Szenario (Runden) | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | Welt-Renderer ms | GUI-CPU ms | Entity-Vernetzung ms | Tick Ø ms | Spitzenspeicher MB | Draw-Calls |
|---|---|---|---|---|---|---|---|---|---|---|
| stand (3) | 118 / 117 | 13,9 → 14,4 | 1,75 → 1,72 | 1,84 → 1,86 | 1,11 → 1,13 | 0,473 → 0,471 | 0,150 → 0,153 | 0,657 → 0,654 | 445 → 446 | 347 → 347 |
| sprint (6) | 117 / 118 | 15,1 → 13,5 (−10 %) | 2,02 → 2,10 (+4 %) | 1,90 → 1,90 | 1,21 → 1,19 | 0,472 → 0,467 | 0,142 → 0,144 | 0,694 → 0,661 | 557 → 556 | 389 → 389 |
| showcase (3) | 117 / 117 | 15,2 → 15,5 | 0,710 → 0,686 | 1,34 → 1,32 | 0,593 → 0,623 | **0,554 → 0,692 (+25 %)** | **0,195 → 0,353** | 0,388 → 0,373 | 328 → 331 | 180 → 153 |
| crowd (6) | 117 / 116 | 15,7 → 15,4 | 2,27 → 2,25 | 1,55 → 1,52 | 1,24 → 1,21 | 0,925 → 0,910 | 0,573 → 0,560 | 0,884 → 0,902 (+2 %) | 450 → 450 | 345 → 345 |

Server-Phasen im Fenster (Ø ms): Loren ohne Loren (stand, sprint, crowd) 0,015–0,016 → 0,0006–0,0008 (−95 %); in `showcase` mit Loren 0,031 → 0,026. Phase Feuer ohne Brand 0,001–0,002. Phase Wesen crowd 0,701 → 0,717 (+2 %, Runden überlappen: 0,670–0,742 gegen 0,688–0,800), stand und sprint ±2 %. `entity_problems` und `block_entity_problems` = 0, keine übersprungenen Ticks, Tick max höchstens 3,8 ms (crowd).

### Fenster: Waldbrand (`forest-fire`, 30 s, 3 Runden; `dev` = Wald ohne Brand)

| | `dev` (ohne Feuer) | 34 (Brand) |
|---|---:|---:|
| FPS / Frame Ø ms | 117 / 8,54 | 117 / 8,54 |
| Frame p99 ms | 14,8 [14,5–15,3] | 15,2 [14,7–15,8] |
| GPU Ø / p99 ms | 1,36 / 1,61 | 1,29 / 1,57 |
| Render-CPU Ø / p99 ms | 1,36 / 2,10 | 1,34 / **2,90** |
| Welt-Renderer Ø ms | 0,472 | 0,555 (+18 %) |
| GUI-CPU Ø / p99 ms | 0,352 / 0,758 | 0,324 / 0,765 |
| Upload Ø ms / MB in 30 s | 0,004 / 7 | 0,086 / 657 |
| Neu vernetzte Abschnitte | 0 | 6593 [6583–6609] |
| Mesh-Jobs, Ø / max ms | 0 | 4189, 2,42 / 11,7 |
| Mesh-Rückstau max | 0 | 104 [92–111] |
| Änderung bis Mesh Ø / max ms | – | 12,8 / 67 [51–81] |
| Licht-Jobs, Ø / max ms | 0 | 769, 0,163 / 1,75 |
| Tick Ø / max ms | 0,116 / 1,16 | 0,304 / 2,41 [2,19–2,66] |
| Phase Feuer Ø / max ms | – | 0,168 / 2,30 |
| Phase Block-Updates Ø / max ms | 0,002 / 0,09 | 0,033 / 0,55 |
| Spitzenspeicher MB | 369 | 390 (+21) |
| Draw-Calls / sichtbare Abschnitte | 104 / 103 | 113 / 112 |

Keine übersprungenen Ticks, kein Tick über 3 ms.

### Headless: 34 gegen `53707b8` (Tick, Mittel über 3 bzw. 6 Runden)

| Szenario | Tick Ø ms | Tick p99 ms | Spitzen-RSS MB | Phasen Ø ms |
|---|---:|---:|---:|---|
| crowd | 0,126 → 0,126 | 0,162 → 0,158 | 50,2 → 50,6 | Wesen 0,110 → 0,112 (+2 %), Loren 0,0009 → 0 |
| gravel-collapse | 0,482 → 0,480 | 1,37 → 1,38 | 70,9 → 70,5 | Loren 0,0002 → 0 |
| farm (6) / farm-seeds | 0,0070 → 0,0065 / 0,0167 → 0,0173 | 0,011 → 0,011 / 0,052 → 0,053 | 43,6 → 44,3 / 43,6 → 43,9 | Loren 0,0001 → 0 |
| explore | 0,159 → 0,166 (+4 %) | 0,790 → 0,813 | 56,1 → 56,4 | Wesen 0,046 → 0,049, Chunk-Laden 0,035 → 0,037, Streaming 0,073 → 0,076, Loren 0,0007 → 0 |
| signal_components (6) | **0,0248 → 0,0285 (+15 %)** | 0,088 → 0,092 | 44,3 → 44,5 | Signal 0,0096 → 0,0124, Block-Updates 0,0004 → 0,0006, Feuer 0,0001 |
| signal_machines (6) | 0,0288 → 0,0260 (−10 %) | 0,092 → 0,090 | 44,3 → 44,5 | Wesen 0,0082 → 0,0058, Signal gleich 0,0125 |
| signal_farm | **0,0210 → 0,0287 (+37 %)** | 0,090 → 0,092 | 44,6 → 44,5 | Signal 0,0062 → 0,0124, Block-Updates 0,0003 → 0,0006, Abgleich 0,0014 → 0,0023 |
| forest-fire | 0,003 (Wald ohne Feuer) → 0,263 | 0,006 → 0,711 | 44,7 → 45,5 | Feuer 0,178 (max 0,81), Block-Updates 0,065, Pflanzen 0,017 |

**Bewertung:**

- **`@core` innerhalb von 10 %, Bild und GPU unverändert** (GPU, Render-CPU, Frame Ø überall innerhalb von 4 %; Draw-Calls in stand, sprint, crowd gleich). Neue Tick-Phase Feuer ohne Brand 0,001–0,002 ms im Fenster, 0 headless.
- **Lorenphase ohne Loren fort:** im Fenster 0,015–0,016 → 0,0006–0,0008 ms, headless 0,0002–0,0009 → 0 (früher Ausstieg, E-393). Erwartet.
- **Brennen je Körper (Phase Wesen) und 16-Bit-Flags:** crowd Phase Wesen headless +2 % (0,110 → 0,112 ms), im Fenster +2 % mit überlappenden Runden; Entity-Vernetzung und GUI-CPU in crowd (94 Wesen) gleich (−2 %). Kein messbarer Preis.
- **GUI-CPU `showcase` +25 % (+0,14 ms), in allen 3 Runden** (34: 0,685–0,699, `dev` 0,519–0,586): Die Szene ist eine andere. Die Feuerstation schiebt die Stationen der ersten Reihen; vom Startpunkt aus sind jetzt 25 statt 10 Entities im Bild (naheliegend die Station der Rüstungsständer-Posen, jetzt auf 4,−21 und 36 m vom Startpunkt, vorher weiter weg; nicht einzeln gezählt), die Entity-Vernetzung steigt um denselben Betrag (0,195 → 0,353 ms). Je Entity gleich teuer (crowd unverändert), Draw-Calls 180 → 153, GPU −3 %. Keine Regression des Codes; absolut 0,69 ms.
- **Tick an den Signal-Stationen headless +15 % und +37 % (+0,004 bzw. +0,008 ms), `signal_machines` −10 %:** Auch hier andere Szenen. Die drei Haltepunkte liegen jetzt in einer Umgebung, deren Signal-Phase der von `signal_machines` auf `dev` gleicht (alle drei 0,0124 ms; Block-Entities 44 → 68 in der Nähe von `signal_machines`/`signal_farm`). Absolut 0,03 ms je Tick von 50 ms; keine Regression im eigentlichen Sinn. Ein Szenen-Bezug unabhängig vom Layout der Showcase wäre für spätere Vergleiche besser (z. B. Haltepunkte relativ zur Station und nur deren Chunks).
- **`explore` headless +4 %** (Wesen, Chunk-Laden, Streaming je +3 bis +6 %, eine Runde belastet): innerhalb der Schwelle.
- **Frame p99 `sprint` −10 %:** Streuung der verpassten VSyncs (bekannter Vorbehalt), GPU +4 % mit überlappenden Runden.

**Waldbrand (34):** Der Brand kostet im Fenster keine Bildzeit (117 FPS, Frame Ø gleich, GPU sogar −5 %, vermutlich weil abgebrannte Kronen weniger Fläche zeichnen). Messbar sind die erwarteten Posten: der Client vernetzt in 30 s rund 6600 Abschnitte neu (4189 Mesh-Jobs, Ø 2,4 ms im Hintergrund, Rückstau bis 111) und rechnet 769 Licht-Jobs (Ø 0,16 ms); vom Setzen eines Blocks bis zum neuen Mesh Ø 13 ms, höchstens 51–81 ms (1–2 Bilder an der Spitze des Brandes). Render-CPU p99 2,1 → 2,9 ms (Uploads von 657 MB in 30 s, Ø 0,09 ms je Bild), Welt-Renderer +0,08 ms. Server: Tick Ø 0,30 ms im Fenster (headless 0,26), Phase Feuer Ø 0,17, Spitze 2,3 ms (5 % des Budgets); Block-Updates (Laubverfall, Erlöschen) 0,03–0,07 ms. Spitzenspeicher +21 MB (nicht aufgeschlüsselt; naheliegend Mesh-Rückstau und Uploads). Alle Werte weit unter den Zielen.

**Absolute Ziele (`docs/Performance.md`, 8 Chunks):** Bildrate 116–118 FPS (Ziel ≥ 60) und Frame-Kosten (höchstens 2,25 ms GPU in crowd, 1,90 ms Render-CPU in sprint, 1,34 ms im Waldbrand; Ziel ≤ 4 ms) erfüllt. p99 ≤ 12,5 ms im Median verfehlt (13,5–15,5 ms bei beiden Ständen, externer Monitor angeschlossen; bekannter Vorbehalt wie in 31.3, 32 und 33, Frame-Kosten-p99 klein). Spitzenspeicher stand, showcase, crowd, Waldbrand 331–450 MB und sprint (10 s) 556 MB unter 600 MB erfüllt; die offenen Befunde sprint 20 s (8 Chunks) und 16 Chunks nicht neu gemessen, durch 34 nicht berührt. Tick: kein Tick über 50 ms (höchstens 3,8 ms), keine übersprungenen Ticks.

### Fazit Etappe 34

Feuer kostet ohne Brand nichts Messbares: neue Phase Feuer 0,001–0,002 ms, Brennen je Körper und 16-Bit-Flags in crowd innerhalb von 2 %, die Lorenphase ohne Loren ist mit dem frühen Ausstieg praktisch fort (−95 %). Ein Waldbrand mit über 2000 Feuern bleibt bei 117 FPS; Client-Neuvernetzung und Licht laufen im Hintergrund mit Ø 13 ms Verzögerung, der Server-Tick steigt auf Ø 0,3 ms, Spitze 2,4 ms. Die Abweichungen über 10 % (GUI-CPU im Showcase, Tick an zwei Signal-Stationen) kommen vom verschobenen Layout der Showcase-Welt, nicht vom Code; keine unerklärte Regression.

## Etappe 35.1: Wetter (headless, 2026-10-15)

Release (`RelWithDebInfo`), `SCENARIOS="thunderstorm rain crowd" scripts/bench-headless.sh <Datei> 3 60 new=build/release` (keine Echtzeit, 0,25 s Wanduhr je Lauf; nur der neue Stand, die Szenarien gibt es auf `dev` nicht). Neue Tick-Phase Wetter bei voller Stärke (Regen bzw. Gewitter mit Blitzen, ein Spieler): Ø 0,0000–0,0001 ms, max 0,000–0,005 ms; Tick Ø thunderstorm 0,082–0,085 ms, rain 0,082–0,084 ms, crowd 0,127–0,134 ms (Phase Wetter dort 0).

Client, `QTCRAFT_WEATHER_MEASURE=1 build/release/tests/test_weather_client measureField` (Release, 3000 Frames, volles Budget von 3000 Teilchen unter offenem Himmel): Regen 0,046 ms GUI-CPU je Frame (Fortschreiben und Segmente, 6000 Vertices), Schnee 0,091 ms (12 000 Vertices, zwei Striche je Flocke). Der Render-Thread macht daraus Ecken (36 Byte, sechs je Segment): rund 0,65 MB (Regen) bzw. 1,3 MB (Schnee) Upload je Frame in einem dynamischen Puffer, ein Zeichenaufruf. Bildzeiten und GPU im Fenster sind nachzuholen (Hauptsitzung: `SCENARIOS=@weather` mit `scripts/bench-ab.sh`, Vergleich gegen `stand` bei klarem Wetter).

## Etappe 35.2: Auswirkungen des Wetters (headless, 2026-10-16)

Release (`RelWithDebInfo`), `SCENARIOS="forest-fire-storm forest-fire thunderstorm rain snow" scripts/bench-headless.sh <Datei> 3 90 new=build/release` (keine Echtzeit, unter 1 s Wanduhr je Lauf; nur der neue Stand). Neues Szenario `forest-fire-storm`: der Waldbrand von `forest-fire` in einem Gewitter bei voller Stärke (Gruppe `@weather`). Werte als Spanne der drei Runden.

| Szenario | Tick Ø ms | Tick max ms | Feuer Ø / max ms | Wetter Ø / max ms | Block-Updates Ø ms |
|---|---|---|---|---|---|
| forest-fire (klar) | 0,284–0,287 | 1,43–1,48 | 0,206–0,208 / 1,40–1,42 | 0 / 0 | 0,058 |
| forest-fire-storm | 0,179–0,181 | 0,79–0,81 | 0,144–0,146 / 0,76–0,77 | 0,0011 / 0,22–0,25 | 0,019 |
| thunderstorm | 0,083–0,085 | 0,21–0,24 | 0,0004 / 0,13–0,14 | 0,0021 / 0,12–0,13 | 0,0001 |
| rain | 0,082–0,083 | 0,14–0,15 | 0 / 0 | 0,0016–0,0017 / 0,04–0,06 | 0 |
| snow | 0,024 | 0,066–0,068 | 0 / 0 | 0,0022 / 0,04–0,05 | 0,0003 |

**Bewertung:**

- **Regen über dem Waldbrand macht ihn billiger:** Die Feuer in den Kronen (offener Himmel) erlöschen nach wenigen Schritten, in den Regen breitet sich nichts aus; Phase Feuer −30 %, Block-Updates −68 %, Tick-Spitze halbiert. Die Regen-Prüfung je Feuer-Schritt (`isRainingAt`: Säulen-Oberkante aus dem Chunk, abwärts über nicht leere Abschnitte) fällt nicht ins Gewicht; ein Zwischenspeicher der Oberkanten auf dem Server ist nicht nötig.
- **Phase Wetter 0,0001 → 0,0016–0,0022 ms** bei Niederschlag: der Schneefall sieht je Spieler und Tick vier Säulen an (Oberkante, Klima, Niederschlag), auch wo es regnet (eine Säule im Gebirge kann über der Schneegrenze liegen). Rund 0,5 µs je Säule, unabhängig von der Weltgröße.
- **Blitze:** Spitzen der Phase Wetter 0,12–0,25 ms im Tick eines Einschlags mit Zündung (Feuer setzen, Block-Update, Licht); im Mittel alle 200 Ticks.

## Etappe 35, Nachbesserungen (E-397, 2026-10-16)

Client, `QTCRAFT_WEATHER_MEASURE=1 build/release/tests/test_weather_client measureField` (Release, 3000 Frames, 3000 Teilchen): Regen 0,039 ms GUI-CPU je Frame (vorher 0,047), Schnee 0,066 ms (vorher 0,088); Upload je Frame 48 000 Byte in beiden Fällen (vorher 576 000 bzw. 1 152 000 Byte, dazu das Aufblähen im Render-Thread). Server, `SCENARIOS="thunderstorm snow" scripts/bench-headless.sh <Datei> 3 30 new=build/release` (Last 5,3): Phase Wetter Ø 0,0023–0,0024 ms (Gewitter) bzw. 0,0026–0,0027 ms (Schnee), vorher 0,0021 bzw. 0,0022 – im Rahmen der Last; ein Mersenne-Twister je Tick neu gesetzt hatte 0,0068 bzw. 0,0050 ms gekostet (verworfen zugunsten eines splitmix64-Stroms).

## Etappe 35 (Wetter)

Pflicht-Benchmark am Etappen-Ende (GUI-Messungen vom Menschen freigegeben, externer Monitor angeschlossen, Fenster auf dem eingebauten Display im Hintergrund). Release-Builds (`RelWithDebInfo`): `dev` `17c54d4` in einem temporären Worktree gegen `feature/35-weather` `75f1954`. Verschränkt mit rotierender Reihenfolge (E-331), Ton stumm, 8 Chunks, Mittel [Spanne]. Reihen: headless `@server @weather` (`bench-headless.sh`, adaptiv 3 → 6 Runden, 60 Spielsekunden, 01:32–01:35); im Fenster `@core` (`bench-ab.sh`, adaptiv, alle Szenarien 6 Runden à 10 s, 01:36–01:49) und `@weather` (ebenso, 01:49–02:01). Last 3,0–6,0, belastet markiert 3 von 48 Läufen der `@core`-Reihe (stand), 1 von 48 der `@weather`-Reihe, headless 1 von 72 (`farm`); ohne sie ändert sich keine Aussage. Wanduhr samt Builds rund 32 min.

**Wetter-Szenarien auf `dev`:** `dev` kennt `/weather` und `/gamerule weatherCycle` nicht (unbekannter Befehl), `/tick sprint 200` läuft trotzdem. `rain` und `thunderstorm` sind auf `dev` also `stand` bei klarem Wetter (gleicher Ort, gleicher Blick), `snow` die klare Tundra, `forest-fire-storm` der Waldbrand von `forest-fire` bei klarem Wetter. Der Vergleich zeigt die Kosten des Wetters, keine Regression.

### Fenster: 35 gegen `17c54d4` (`@core`, klares Wetter)

| Szenario (Runden) | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | Welt-Renderer ms | GUI-CPU ms | Entity-Vernetzung ms | Tick Ø ms | Phase Wetter Ø ms | Spitzenspeicher MB | Draw-Calls |
|---|---|---|---|---|---|---|---|---|---|---|---|
| stand (6) | 120 / 120 | 9,83 → 9,89 | 1,75 → 1,75 | 1,57 → 1,58 | 0,974 → 0,987 | 0,433 → 0,445 (+3 %) | 0,151 → 0,145 | 0,638 → 0,610 | – → 0,003 | 445 → 447 | 347 → 347 |
| sprint (6) | 120 / 120 | 10,1 → 10,1 | 2,08 → 2,20 (+6 %) | 1,68 → 1,73 (+3 %) | 1,10 → 1,13 | 0,459 → 0,463 | 0,134 → 0,141 | 0,596 → 0,614 (+3 %) | – → 0,005 | 556 → 560 | 388 → 388 |
| showcase (6) | 120 / 120 | 10,0 → 10,2 | 0,594 → 0,594 | 1,18 → 1,13 (−4 %) | 0,558 → 0,507 | 0,708 → 0,687 | 0,351 → 0,360 | 0,375 → 0,367 | – → 0,002 | 329 → 332 | 153 → 153 |
| crowd (6) | 120 / 120 | 9,96 → 10,0 | 2,57 → 2,65 (+3 %) | 1,45 → 1,40 (−3 %) | 1,14 → 1,13 | 0,898 → 0,892 | 0,524 → 0,522 | 0,890 → 0,929 (+4 %) | – → 0,003 | 451 → 453 | 345 → 345 |

Keine übersprungenen Ticks, Tick max höchstens 3,9 ms (crowd, beide Stände). Bei klarem Wetter zeichnet der Regen-Renderer nichts (Draw-Calls gleich); die Tick-Phase Wetter kostet im Fenster 0,002–0,005 ms. Spitzenspeicher +2 bis +4 MB (+1 %).

### Fenster: Wetter (`@weather`, 6 Runden à 10 s; `dev` = dieselbe Szene bei klarem Wetter)

| | rain | snow | thunderstorm | forest-fire-storm |
|---|---|---|---|---|
| FPS / Frame Ø ms | 120 / 8,33 → 8,33 | 120 / 8,35 → 8,34 | 120 / 8,33 → 8,33 | 120 / 8,35 → 8,34 |
| Frame p99 ms | 9,83 → 9,86 | 9,81 → 10,0 | 9,98 → 10,0 | 9,97 → 9,97 |
| GPU Ø ms (Median) | 1,60 → 1,85 [1,64–2,35] (1,60 → 1,67) | 1,50 → 1,48 | 1,89 → 1,76 (1,92 → 1,65) | 1,18 → 1,40 [1,20–1,85] (1,17 → 1,30) |
| Render-CPU Ø / p99 ms | 1,55 → 1,59 / 2,36 → 2,43 | 1,83 → 1,70 / 2,82 → 2,91 | 1,71 → 1,57 / 2,50 → 2,43 | 1,28 → 1,25 / 1,99 → 2,05 |
| GUI-CPU Ø / p99 ms | **0,479 → 0,601** / 0,80 → 1,00 | **0,363 → 0,569** / 0,71 → 0,96 | 0,532 → 0,606 / 0,92 → 1,00 | **0,344 → 0,490** / 0,72 → 0,95 |
| Upload Ø ms | 0,017 → 0,023 | 0,010 → 0,055 | 0,021 → 0,024 | 0,016 → 0,026 |
| Neu vernetzte Abschnitte / Mesh-Jobs in 10 s | 0 → 21 / 21 | **0 → 495 / 471** | 0 → 39 / 35 | 263 → 543 / 195 → 407 |
| Mesh-Rückstau max / Änderung bis Mesh Ø, max ms | – → 4 / 7,7, 10 | – → 12 / 7,4, 14 [8–25] | – → 11 / 10,8, 16 [11–33] | 106 → 105 / 17,4 → 13,1, 44 → 44 |
| Tick Ø / max ms | 0,537 → 0,533 / 1,6 → 1,1 | 0,207 → 0,192 / 0,67 → 0,46 | 0,739 → 0,657 / 1,8 → 1,8 | 0,143 → 0,199 / 1,4 → 1,5 |
| Phase Wetter Ø / max ms | – → 0,024 / 0,09 | – → 0,037 / 0,16 [0,08–0,35] | – → 0,035 / 0,69 [0,34–1,21] | – → 0,015 / 0,47 |
| Phase Feuer Ø / max ms | 0,001 | 0,001 | 0,002 / 0,09 → 0,08 | 0,050 → 0,086 / 1,3 → 1,4 |
| Spitzenspeicher MB | 445 → 452 (+6) | 401 → 413 (+11) | 446 → 457 (+11) | 378 → 387 (+9) |
| Draw-Calls | 347 → 348 | 634 → 635 | 347 → 348 | 120 → 116 |

Keine übersprungenen Ticks, kein Tick über 3 ms. Die Teilchenzahl steht nicht in der BENCH-Zeile; nach `/tick sprint 200` herrscht volle Stärke, also das volle Budget von 3000 Teilchen (E-395), gezeichnet in einem Aufruf (Draw-Calls +1).

### Headless: 35 gegen `17c54d4` (Tick, Mittel über 3 bzw. 6 Runden)

| Szenario | Tick Ø ms | Tick p99 ms | Spitzen-RSS MB | Phasen Ø ms |
|---|---:|---:|---:|---|
| crowd | 0,134 → 0,132 | 0,194 → 0,189 | 50,4 → 50,6 | Wesen 0,118 → 0,117, Wetter 0,0001 |
| gravel-collapse | 0,494 → 0,495 | 1,46 → 1,43 | 70,8 → 71,1 | Block-Updates 0,036 gleich, Wetter 0,0001 |
| farm / farm-seeds | 0,0073 → 0,0070 / 0,0170 → 0,0173 | 0,015 → 0,015 / 0,056 → 0,055 | 44,0 → 44,7 / 44,0 → 44,2 | Pflanzen gleich, Wetter 0,0002 / 0 |
| explore | 0,177 → 0,177 | 0,863 → 0,878 | 56,5 → 56,5 | Streaming, Chunk-Laden, Wesen gleich, Wetter 0,0003 |
| signal_components / machines / farm | 0,031 → 0,031 / 0,028 → 0,028 / 0,030 → 0,031 | 0,099 → 0,100 / 0,097 → 0,097 / 0,095 → 0,098 | 44,6 → 44,7 / 44,7 → 44,9 / 44,4 → 44,7 | Signal gleich, Wetter 0 |
| rain (6) | 0,087 → 0,090 (+3 %) | 0,133 → 0,140 (+6 %) | 50,4 → 50,9 | Wetter 0,0023 (max 0,08) |
| snow (6) | **0,0227 → 0,0262 (+15 %)** | 0,040 → 0,044 (+11 %) | 47,3 → 48,1 | Wetter 0,0028 (max 0,05), Block-Updates 0 → 0,0004 |
| thunderstorm (6) | 0,087 → 0,092 (+6 %) | **0,129 → 0,198 (+53 %)** | 50,5 → 51,3 | Wetter 0,0028 (max 0,20–0,25), Feuer 0 → 0,0013 (max 0,16) |
| forest-fire-storm | 0,271 → 0,191 (−29 %) | 0,740 → 0,559 (−24 %) | 45,7 → 46,0 | Feuer 0,183 → 0,152, Block-Updates 0,067 → 0,022, Pflanzen 0,018 → 0,013, Wetter 0,0012 (max 0,16) |

**Bewertung:**

- **`@core` innerhalb von 10 %, kein Schlüssel über +6 %** (GPU sprint +6 % mit überlappenden Runden 1,84–2,49 gegen 1,80–2,59; Draw-Calls und Dreiecke überall gleich). Die neue Tick-Phase Wetter kostet bei klarem Wetter 0,002–0,005 ms im Fenster und ≤ 0,0003 ms headless, der Regen-Renderer ohne Teilchen nichts Messbares, Himmels-Shader und neuer Frame-Schritt Weather ändern GPU und Render-CPU nicht. Headless `@server` überall innerhalb von ±3 % (Tick Ø) bzw. ±3 % (p99).
- **Kosten des Wetters im Fenster (keine Regression, erwartet):**
  - **GUI-CPU +0,07 bis +0,21 ms** (Regen +0,12, Gewitter +0,07, Schnee +0,21, Waldbrand im Gewitter +0,15): Teilchenfeld fortschreiben und Instanzen füllen (laut `measureField` 0,039 ms Regen, 0,066 ms Schnee), dazu Klima, Himmel und Blitz; beim Schnee außerdem die Neuvernetzung der Schneedecke (siehe unten). Absolut höchstens 0,61 ms.
  - **GPU:** Mittel Regen +15 %, Waldbrand im Gewitter +18 %, aber bimodal: je zwei von sechs Läufen bei 2,1–2,4 bzw. 1,5–1,8 ms, die übrigen nahe `dev`; im Median +0,07 ms (Regen, +5 %) bzw. +0,12 ms (Waldbrand, +10 %). Gewitter (dieselbe Szene wie Regen) im Mittel −7 %, im Median −14 %, hier lagen die Ausreißer bei `dev`. Der Regen kostet die GPU also unter 0,1 ms; die Mittel spiegeln die Taktstufen der GPU. Schnee −2 %.
  - **Render-CPU** unverändert (−8 bis +3 %), Upload Ø ≤ 0,06 ms je Bild.
  - **Schneedecke (M8 in E-397): 495 neu vernetzte Abschnitte in 10 s** (471 Mesh-Jobs, Ø 1,9 ms im Hintergrund, Rückstau höchstens 12, Änderung bis Mesh Ø 7,4 ms, max 8–25 ms). Rund 50 Abschnitte je Sekunde bei vier Schichten je Tick: fast jede Schicht trifft einen anderen Abschnitt bzw. dessen Nachbarn. Bildzeit und Render-CPU bleiben gleich (Frame Ø 8,34 ms, p99 10,0 ms); Upload Ø 0,010 → 0,055 ms. Im Regen werden 21 Abschnitte neu vernetzt, im Gewitter 39 (dazu 3 Licht-Jobs): vermutlich Schneeschichten in Säulen über der Schneegrenze bzw. in kaltem Klima im Umkreis von 48 Blöcken, im Gewitter zusätzlich Einschläge und Funken; nicht einzeln gezählt.
  - **Spitzenspeicher +6 bis +11 MB** (+1,5 bis +3 %).
  - **Waldbrand im Gewitter, Fenster Tick Ø +39 % und Phase Feuer +73 %, headless −29 %:** kein Widerspruch. Auf `dev` frisst der klare Brand während `/tick sprint 200` vor der Messung schneller um sich und ist in den gemessenen 10 s schon über seiner Spitze (Phase Feuer 0,05 ms), im Regen brennt er langsamer und länger, dazu zünden Blitze (Neuvernetzung 263 → 543 Abschnitte, Licht-Jobs 53 → 81). Headless über 60 Spielsekunden ist der Regen-Brand billiger (Feuer −17 %, Block-Updates −67 %), wie in 35.2 gemessen.
- **Headless Wetter-Szenarien:** `snow` +15 % Tick Ø (+0,0035 ms: Schneefall 0,0028 ms, Block-Updates der gesetzten Schichten 0,0004 ms), `thunderstorm` p99 +53 % (+0,07 ms: Ticks mit Einschlag und Zündung, Phase Wetter max 0,20–0,25 ms, Feuer max 0,16 ms). Erwartete Kosten der neuen Funktion, absolut unter 0,3 ms je Tick von 50 ms; keine Regression.

**Absolute Ziele (`docs/Performance.md`, 8 Chunks):** Bildrate 120 FPS (Ziel ≥ 60), Frame-Kosten höchstens 2,65 ms (GPU crowd; in den Wetter-Szenen höchstens 1,85 ms GPU und 1,83 ms Render-CPU; Ziel ≤ 4 ms) und p99 ≤ 12,5 ms (alle 96 Fensterläufe 9,2–10,5 ms, diesmal auch ohne Ausreißer) erfüllt. Spitzenspeicher stand, showcase, crowd und alle Wetter-Szenen 332–457 MB, sprint (10 s) 560 MB unter 600 MB erfüllt; die offenen Befunde sprint 20 s (8 Chunks) und 16 Chunks nicht neu gemessen, durch 35 nicht berührt. Tick: kein Tick über 50 ms (höchstens 3,9 ms), keine übersprungenen Ticks.

### Fazit Etappe 35

Wetter kostet bei klarem Himmel nichts Messbares: `@core` innerhalb von 6 %, die Phase Wetter 0,002–0,005 ms, Renderer und Shader ohne Preis. Regen und Schnee bei vollem Budget bleiben bei 120 FPS und p99 um 10 ms; sie kosten 0,07–0,21 ms GUI-CPU, unter 0,1 ms GPU (Median) und 6–11 MB. Die Schneedecke lässt rund 50 Abschnitte je Sekunde neu vernetzen, ohne Einfluss auf die Bildzeit. Keine Regression über 10 % in `@core` oder `@server`; die Abweichungen in den Wetter-Szenen sind die Kosten der neuen Funktion gegenüber klarem Wetter.

## Etappe 36: Erfahrungskugeln (headless, 2026-10-16)

Release (`RelWithDebInfo`), `QTCRAFT_EXPERIENCE_MEASURE=full build/release/tests/test_experience measureManyOrbs` (Ticks von Hand, keine Echtzeit, rund 0,1 s Wanduhr je Lauf; Grenze `experienceOrbs` für die Messung 4096; Last 8). Phase Items je Tick (Items und Kugeln; Items gab es keine), je Fall 200 Ticks, Spanne über drei Läufe. Fälle: **farm** – 1000 Kugeln zu 1 Punkt auf der Tötungsstelle einer Mob-Farm (200 Zombies × 5 Punkte auf 0,5 × 0,5 m), 10 m vom Spieler, also ohne Anziehung; **collect** – der Spieler steht danach in der Stelle und sammelt; **spread** – 900 Kugeln im Abstand von 1,5 m über die geladenen Chunks verteilt (nichts verschmilzt, die nahen fliegen zum Spieler).

| Fall | Kugeln vorher → nachher | Items Ø ms | Items max ms |
|---|---|---|---|
| farm | 1000 → 1 | 0,016–0,019 | 0,47–0,56 |
| collect | 1 → 0 | 0,0001 | 0,005–0,006 |
| spread | 900 → 831 | 0,173–0,182 | 0,73–0,82 |

**Bewertung:**

- **Verschmelzen hält die Farm klein:** Im ersten Verschmelz-Tick (nach 10 Ticks) werden aus 1000 Kugeln eine mit 1000 Punkten; die Spitze von 0,5 ms ist der erste Tick, in dem alle 1000 noch fallen. Danach kostet die ruhende Kugel nichts. Ohne Verschmelzen bliebe es beim Fall spread: rund 0,2 µs je Kugel und Tick (Ruhe-Prüfung, nächster Spieler), Spitze 0,8 ms beim Fallen. Die Grenze von 1024 Kugeln (Standard) deckelt diesen Fall; mehr Erfahrung geht in die nächste Kugel.
- **Einsammeln:** eine Kugel je Spieler alle 2 Ticks (wie im Vorbild); 1000 nicht verschmolzene Kugeln bräuchten 100 s, verschmolzen ist die Farm in einem Tick abgeholt.
- **Ohne Kugeln** kehrt die Kugel-Phase sofort zurück; je Tick bleibt der Vergleich der Erfahrung jedes Spielers mit der zuletzt gesendeten (`QcServerExperience::sendChanges`). Kein Fenster- oder `@server`-Benchmark gelaufen (nicht nötig für diese Phase; beim Merge mit dem Etappen-Benchmark nachholen, falls gewünscht).

## Etappe 36 (Erfahrung und Lapislazuli)

Pflicht-Benchmark am Etappen-Ende (GUI-Messungen vom Menschen freigegeben, Fenster auf dem eingebauten Display im Hintergrund, `--screen builtin`). Release-Builds (`RelWithDebInfo`): `dev` `51e331d` in einem temporären Worktree gegen `feature/36-experience` `ffae444` (Nachbesserungen E-399 eingeschlossen). Verschränkt mit rotierender Reihenfolge (E-331), Ton stumm, 8 Chunks, Mittel. Reihen: headless `@server` (`bench-headless.sh`, adaptiv, alle Szenarien nach 3 Runden fertig, 60 Spielsekunden, 03:46–03:48); im Fenster `@core` (`bench-ab.sh`, adaptiv, alle Szenarien 6 Runden à 10 s, 03:49–04:02); das neue Szenario `orbs` headless (6 Runden, 20 Spielsekunden) und im Fenster (3 Runden à 10 s, 04:07–04:10). Last 2,3–9,5 (einmal 9,5 nach einem stand-Lauf), belastet markiert 7 von 48 Läufen der `@core`-Reihe, verteilt auf beide Stände; ohne sie ändert sich keine Aussage. Wanduhr samt Builds und Aufbau des Szenarios rund 30 min.

**Neues Szenario `orbs`** (`scripts/bench-common.sh`): flache Welt, 15 Reihen zu 20 Öfen im Abstand von 3 m nördlich des Spielers, je ein Trichter darüber (4 Eisenerz) und daneben (1 Kohle), der Spieler wirft die Stapel per `/tp` und `/spawnitem` darauf (rund 2100 Zeilen); `/tick sprint 1000` schmilzt, nach 2 s räumt `/fill` die Reihen: jeder Ofen gibt seine Erfahrung (4 × 0,7) als Kugel frei und verschüttet seine Barren. Rund 300 Kugeln in 3 m Abstand (keine verschmilzt) und 300 Items; der Spieler schaut von einem Block 14 m hoch darauf (keine Kugel im Umkreis von 8 m, alle ruhen) und steigt nach der halben Laufzeit in die Mitte des Felds hinab, die Kugeln im Umkreis fliegen zu ihm (eine je 2 Ticks). Auf `dev` läuft dieselbe Szene ohne Kugeln (der Ofen sammelt dort keine Erfahrung); der Vergleich zeigt also die Kosten der Kugeln. Ein erster Entwurf mit Trichterketten (ein Trichterband je Reihe) bewegte keine Items und wurde verworfen (nicht in git). Nachträglich untersucht (`test_hopper_lines`): Die Trichter sind nicht die Ursache. Gerade Linien, Kurven, Truhen in der Linie, Chunk-Grenzen, ein erst später geladener Ziel-Chunk und 15 Bänder mit zusammen 2310 Trichtern über 300 Öfen bewegen ein Item je 8 Ticks und Trichter, ohne Budget-Grenze (höchstens 75 handelnde je Tick) und ohne Verlust. Am Entwurf lag es: (1) Ein Item braucht 8 Ticks je Trichter, über ein Band von rund 150 Trichtern also rund 1200 Ticks (60 s Spielzeit), bevor am Ende etwas ankommt. (2) Ein Trichter unter dem Band zieht jedes Item, das über ihn läuft (der überholende Trichter ruht nach dem Empfang 8 Ticks, wie im Vorbild); ein Band über einer Reihe von Öfen füllt nur den ersten Ofen, die übrigen 19 der Reihe bekommen nichts. Ein Band zum Verteilen braucht gesperrte Trichter (Sortierer) oder einen eigenen Zweig je Ofen.

### Fenster: 36 gegen `51e331d` (`@core`, 6 Runden à 10 s)

| Szenario | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | Welt-Renderer ms | GUI-CPU ms | Entity-Vernetzung ms | Tick Ø ms | Phase Items Ø ms | Spitzenspeicher MB | Draw-Calls |
|---|---|---|---|---|---|---|---|---|---|---|---|
| stand | 120 / 120 | 9,84 → 10,0 | 1,70 → 1,81 (+6 %) | 1,55 → 1,58 | 0,973 → 0,980 | 0,452 → 0,441 | 0,141 → 0,145 | 0,547 → 0,580 (+6 %) | 0,0045 → 0,0047 | 447 → 449 | 347 → 347 |
| sprint | 120 / 120 | 10,0 → 10,0 | 2,23 → 2,17 (−3 %) | 1,64 → 1,58 (−3 %) | 1,03 → 0,989 | 0,424 → 0,413 | 0,137 → 0,131 | 0,652 → 0,619 (−5 %) | 0,0042 → 0,0041 | 558 → 559 | 388 → 388 |
| showcase | 120 / 120 | 10,1 → 10,0 | 0,594 → 0,592 | 1,15 → 1,13 | 0,542 → 0,518 | 0,702 → 0,724 (+3 %) | 0,337 → 0,354 (+5 %) | 0,340 → 0,321 (−6 %) | 0,0097 → 0,0091 | 333 → 333 | 153 → 153 |
| crowd | 120 / 120 | 10,0 → 10,0 | 2,61 → 2,57 | 1,30 → 1,30 | 1,02 → 1,02 | 0,844 → 0,826 | 0,515 → 0,506 | 0,817 → 0,819 | 0,0126 → 0,0131 | 453 → 454 | 345 → 345 |

Keine übersprungenen Ticks, Tick max höchstens 4,8 ms. GPU stand zweigipflig wie in den Etappen davor (Läufe 1,57–2,15 gegen 1,58–1,99 ms, Median 1,65 → 1,77 ms, +7 %); Tick Ø im Fenster mit Spannen von 40–70 % je Stand (Schlaf zwischen den Ticks), bewertet headless.

### Headless: 36 gegen `51e331d` (`@server`, je 3 Runden)

| Szenario | Tick Ø ms | Tick p99 ms | Spitzen-RSS MB | Phasen Ø ms |
|---|---:|---:|---:|---|
| crowd | 0,132 → 0,135 (+2 %) | 0,186 → 0,185 | 50,3 → 50,2 | Wesen 0,117 → 0,119, Items 0,001 gleich, Client-Sync 0,0100 → 0,0102 |
| gravel-collapse | 0,493 → 0,497 | 1,44 → 1,42 | 71,1 → 71,0 | Client-Sync 0,150 gleich |
| farm / farm-seeds | 0,0073 gleich / 0,0170 → 0,0173 | 0,015 gleich / 0,053 → 0,055 | 44,4 → 44,3 / 44,0 → 43,5 | Items (farm-seeds, 150 Stapel) 0,0064 → 0,0065, Client-Sync 0,0042 → 0,0043 |
| explore | 0,181 → 0,181 | 0,906 → 0,895 | 56,6 → 56,4 | Streaming 0,081 gleich, Wesen 0,052 gleich |
| signal_components / machines / farm | 0,031 / 0,028 / 0,031 gleich | 0,100 / 0,098 → 0,101 / 0,100 → 0,099 | 44,7–44,9 | Maschinen 0,0003 gleich, Items 0,0004–0,0005 |

### Kugeln (`orbs`; `dev` = dieselbe Szene ohne Kugeln)

| | Fenster (3 Runden à 10 s) | headless (6 Runden, 20 Spielsekunden) |
|---|---|---|
| Entities beim Client am Ende | 304 → 548 (rund 245 Kugeln nach dem Einsammeln) | – |
| FPS / Frame Ø / p99 ms | 120 / 8,34 → 8,33 / 9,37 → 9,37 | – |
| GPU Ø ms | 0,408 → 0,421 (+3 %) | – |
| Render-CPU / Welt-Renderer ms | 1,14 → 1,10 / 0,402 → 0,401 | – |
| GUI-CPU Ø ms | **0,845 → 1,065 (+0,22)** [0,83–0,87 → 1,00–1,17] | – |
| Entity-Vernetzung Ø / p99 ms | **0,483 → 0,684 (+0,20)** / 1,17 → 1,43 | – |
| Tick Ø / max ms | 0,226 → 0,290 / 0,92 → 1,09 | 0,0245 → 0,0412 (+0,017) / p99 0,095 → 0,165 |
| Phase Items Ø / max ms | 0,083 → 0,117 / 0,43 → 0,56 | 0,0134 → 0,0230 / 0,093 → 0,151 |
| Phase Client-Sync Ø / max ms | – | 0,0062 → 0,0123 / 0,14 → 0,26 |
| Spitzenspeicher MB | 586 → 587 | 45,1 → 45,5 |
| Draw-Calls | 52 → 52 | – |

**Bewertung:**

- **`@core` und `@server` innerhalb von 10 %:** im Fenster kein Schlüssel über +6 % (GPU stand +6 % bei zweigipfligen Läufen, Median +7 %; Tick Ø stand +6 % bei 46 % Spanne, headless gleich), Draw-Calls und Spitzenspeicher gleich (+0 bis +2 MB). Headless Tick Ø −1 bis +2 %, p99 −1 bis +4 %.
- **Kugel-Phase ohne Kugeln:** Phase Items in allen `@server`-Szenarien gleich (crowd 0,001 ms, farm-seeds mit 150 Stapeln 0,0064 → 0,0065 ms); `sendChanges` (Vergleich der Erfahrung je Spieler) ist nicht messbar.
- **Stopp-Proben in der Entity-Sync (E-399):** Phase Client-Sync in crowd (24 Items, 56 Wesen) 0,0100 → 0,0102 ms, farm-seeds 0,0042 → 0,0043 ms, gravel-collapse (fallende Blöcke) gleich: eine Nachricht je Landung kostet nichts Messbares.
- **Ofen mit Erfahrung ruht (E-399):** Phase Maschinen in den Signal-Stationen (Trichter, Sortierer, Öfen) 0,0003 ms gleich; im Szenario `orbs` mit 300 Öfen 0,0007 ms (`dev`) gegen 0,0007 ms.
- **Kosten der Kugeln (neue Funktion, keine Regression):** rund 245 sichtbare Kugeln kosten im Fenster **0,22 ms GUI-CPU**, fast ganz Entity-Vernetzung (+0,20 ms, rund 0,8 µs je Kugel und Bild; die 300 Items derselben Szene kosten rund 1,6 µs je Item); GPU +3 %, Render-CPU und Draw-Calls gleich (Kugeln liegen im Entity-Puffer, kein eigener Draw-Call). Bildrate und p99 unverändert. Server: headless +0,017 ms je Tick (Phase Items +0,010 ms, davon das Einsammeln in der zweiten Hälfte, Client-Sync +0,006 ms für die Bewegungen fliegender Kugeln), im Fenster +0,064 ms Tick Ø. Ruhende Kugeln ohne Spieler im Umkreis kosten fast nichts (headless-Messung `measureManyOrbs` oben: rund 0,2 µs je wache Kugel); der Rest ist Anziehen, Einsammeln und Senden.

**Absolute Ziele (`docs/Performance.md`, 8 Chunks):** Bildrate 120 FPS (Ziel ≥ 60), Frame-Kosten höchstens 2,75 ms (GPU crowd, einzelner Lauf; Mittel höchstens 2,61 ms; Ziel ≤ 4 ms) und p99 ≤ 12,5 ms (alle 54 Fensterläufe 9,3–10,4 ms) erfüllt. Spitzenspeicher stand, showcase, crowd 333–454 MB, sprint (10 s) 559 MB, orbs 587 MB, alle unter 600 MB; die offenen Befunde sprint 20 s (8 Chunks) und 16 Chunks nicht neu gemessen, durch 36 nicht berührt. Tick: kein Tick über 50 ms (höchstens 4,8 ms), keine übersprungenen Ticks.

### Fazit Etappe 36

Erfahrung kostet ohne Kugeln nichts Messbares: `@core` innerhalb von 6 % (Median-GPU stand +7 %, bekannt zweigipflig), `@server` innerhalb von 2 % (p99 4 %), Stopp-Proben und ruhende Öfen ohne Preis. Mit rund 250 Kugeln im Blick kostet das Zeichnen 0,22 ms GUI-CPU (Entity-Vernetzung) bei unveränderter Bildrate, der Server 0,02 ms je Tick headless. Keine Regression über 10 %; alle Ziele erfüllt.

## Etappe 37.1a: Wegfindung der Wesen (headless, 2026-10-16)

Release (`RelWithDebInfo`), `QTCRAFT_PATH_MEASURE=1 build/release/tests/test_mob_path_measure` (E-400; Ticks von Hand, keine Echtzeit, rund 3 s Wanduhr für alle Fälle). Gemessen ist `QcMobManager::tick` (die Phase Wesen) je Tick, 200 Ticks nach 20 zum Anlaufen, Mittel über drei verschränkte Läufe gegen `fcb436b` (dieselbe Messdatei im Stand vor der Wegfindung, verschränkt: alt, neu, alt, neu …; Last 4–9). 200 bzw. 1000 Zombies 8–20 m um einen Spieler, der einen Kreis von 4 m mit 2 m/s geht. Fälle: **hills** – Testgelände mit Hügeln und See; **maze** – flache Welt mit Räumen von 3 × 3 Zellen hinter 3 Blöcke hohen Wänden, die Hälfte der Wandstücke mit einer Lücke (Sackgassen, abgeschlossene Taschen); **closed** – derselbe Irrgarten, der Spieler in einem geschlossenen Glasraum (niemand erreicht ihn, jede Suche läuft bis zu ihrem Knotenbudget). „gerade“: dieselbe Szene mit `pathNodesPerTick` 0 (Wegfindung aus, Wesen gehen gerade aufs Ziel; Ausweichen und Sichtlinie bleiben).

| Fall | Zombies | vorher Ø ms | nachher Ø ms | nachher gerade Ø ms | nachher max ms | Knoten je Tick | Suchen in 220 Ticks |
|---|---|---|---|---|---|---|---|
| hills | 200 | 0,196 | 0,264 | 0,243 | 0,49 | 50 | 1990 |
| hills | 1000 | 0,971 | 1,313 | 1,192 | 2,21 | 275 | 9766 |
| maze | 200 | 0,217 | 0,274 | 0,262 | 0,88 | 110 | 1773 |
| maze | 1000 | 1,078 | 1,452 | 1,381 | 2,56 | 529 | 8345 |
| closed | 200 | 0,228 | 0,841 | 0,273 | 1,39 | 2493 | 1328 |
| closed | 1000 | 1,134 | 2,239 | 1,473 | 2,57 | 3783 | 2013 |

Spannen über die drei Läufe höchstens ±3 %.

**Bewertung:**

- **Das Budget hält:** Im schlimmsten Fall (closed, 1000) laufen 3783 von 4096 Knoten je Tick (eine Suche startet nur, wenn ihre 512 Knoten noch hineinpassen); die Wartenden kommen der Reihe nach dran. Ein Knoten kostet rund 0,2 µs (closed 1000: +0,77 ms für 3783 Knoten gegenüber gerade). Mit dem Zurückweichen (1, 2, 4, 8 s) suchen 1000 Zombies vor dem Glasraum in 11 s rund 2000-mal, nicht 220 000-mal.
- **Erreichbare Beute ist billig:** Wege über offenes Gelände und durch den Irrgarten brauchen wenige Knoten (die gewichtete Schätzung führt gerade aus); 1000 Zombies, die einem gehenden Spieler folgen, suchen rund 44-mal je Tick mit zusammen 275–529 Knoten, +0,07–0,12 ms gegenüber gerade.
- **Ohne Suche +20–30 %:** Ausweichen (`QcMobSeparation`: Sortieren der Gehenden, drei Bereichssuchen je Wesen, im Gedränge höchstens 32 angesehen), die Prüfung, ob ein Schub über eine Kante führt, die Sichtlinie im Nahkampf und mehr Bewegung (geschobene Wesen gehen statt zu stehen) kosten bei 1000 Zombies im Gedränge um den Spieler +0,2–0,34 ms. Das Gedränge ersetzt 37.1b (ein Schub je Zelle, KI-Stufen nach Dichte).
- **Absolut:** Selbst 1000 Zombies im schlimmsten Fall brauchen 2,2 ms Ø und 2,6 ms höchstens von 50 ms je Tick; die Obergrenzen für natürlich erscheinende Feinde (8 je Spieler) liegen weit darunter. Kein Fenster- oder `@server`-Benchmark gelaufen (beim Etappen-Benchmark von 37.1 nachholen; `@server` crowd hat 56 Wesen).

## Etappe 37.1b: Gedränge der Wesen (headless, 2026-10-16)

Release (`RelWithDebInfo`), `QTCRAFT_CROWD_MEASURE=1 build/release/tests/test_mob_crowd_measure` (E-401; Ticks von Hand, keine Echtzeit, unter 1 s Wanduhr für alle Fälle). Gemessen ist `QcMobManager::tick` (die Phase Wesen) je Tick, beim Fall mit Gedränge-Schaden einschließlich des Schadens (wie `QcServerCreatures` mit `maxEntityCramming` 24); drei verschränkte Läufe gegen `1445968` (Stand 37.1a, dieselbe Messdatei, die ohne `QcMobCrowd` nur die Schnittstelle von 37.1a nutzt; Last 11–22). Fälle: **cell** – 1000 Zombies in einer Zelle von 1 × 1 hinter drei Blöcken Stein, der Spieler fünf Blöcke daneben (unsichtbar), 200 Ticks nach 20 zum Anlaufen, Regel aus; **cell-cramming** – dasselbe mit Regel 24; **spread** – 1000 Zombies auf 80 × 80 Blöcken flacher Welt um einen Spieler, der einen Kreis von 4 m geht (Regel 24); **farm** – vier Zombies je Sekunde fallen einen Schacht von 20 Blöcken in eine Zelle von 1 × 1, 60 s (1200 Ticks, alle gemessen), der Spieler wartet 10 Blöcke daneben hinter Stein.

| Fall | vorher Ø ms | nachher Ø ms | vorher max ms | nachher max ms | Wesen am Ende vorher → nachher | Suchen vorher → nachher | Stufen am Ende voll/gedrosselt/aus |
|---|---|---|---|---|---|---|---|
| cell | 0,829 | 0,778 (−6 %) | 1,10 | 0,90 | 1000 → 1000 | 4000 → 0 | 0/0/1000 |
| cell-cramming | 0,827 | 0,102 (−88 %) | 1,07 | 1,74 | 1000 → 24 | 4000 → 0 | 0/0/24 |
| spread | 1,005 | 1,027 (+2 %) | 1,24 | 1,27 | 1000 → 891 | 3373 → 2783 | 791/71/29 |
| farm | 0,099 | 0,021 (−79 %) | 0,27 | 0,06 | 240 → 28 | 1521 → 139 | 4/0/25 |

Spannen über die drei Läufe höchstens ±3 %. Die Messung der Wegfindung (`test_mob_path_measure`, alle Fälle aus 37.1a) liegt im selben verschränkten Vergleich gegen `1445968` innerhalb von ±2 % (closed 1000: 2,21 → 2,25 ms; hills 1000: 1,31 → 1,31 ms).

**Bewertung:**

- **Ein Gedränge sucht nicht mehr:** 1000 Zombies in einer Zelle stellten in 37.1a jeder alle paar Sekunden eine Suche an (4000 in 220 Ticks, jede mit vollem Budget, weil kein Weg aus der Zelle führt); jetzt ist ihre KI aus (keine Suche, kein Ziel, keine Sichtlinie). Übrig bleiben Physik (rund 0,45 ms: jeder Körper wird geschoben und stößt an die Wände) und das Zellenraster; daher nur −6 % ohne Regel. Der Schub ist in einer vollen Zelle nutzlos, aber verlangt (Schwerkraft, Strömung, Schaden, Schub); ein Ruhen geschobener Körper wäre der nächste Schritt, falls Farmen ohne Gedränge-Schaden je teuer werden.
- **Mit Gedränge-Schaden schrumpft die Zelle auf 24:** Die Höchstzeit (1,7 ms) ist der Tick, in dem die ersten Gruppen sterben (Entfernen aus der Liste); danach kostet die Zelle fast nichts.
- **Verteilt unverändert:** 1000 verteilte Zombies laufen fast alle mit voller KI (eine Sichtprüfung mit einem Strahl alle zehn Ticks, statt jeden Tick den nächsten Spieler ohne Sicht zu nehmen); die Zombies, die sich beim Spieler drängen, werden gedrosselt oder sterben am Gedränge (109).
- **Farm:** Eine typische Fallschacht-Farm hält ihre Zelle bei 24 Zombies und braucht ein Fünftel der Zeit; ohne Regel wären es wie vorher 240 Zombies mit KI aus statt mit voller KI.
- **Kein Fenster- oder `@server`-Benchmark gelaufen** (beim Etappen-Benchmark von 37.1 nachholen, zusammen mit 37.1a).

## Etappe 37.1: Nachbesserungen nach der Prüfung (headless, 2026-10-16)

Release (`RelWithDebInfo`), `QTCRAFT_CROWD_MEASURE=1 build/release/tests/test_mob_crowd_measure` und `QTCRAFT_PATH_MEASURE=1 build/release/tests/test_mob_path_measure` (E-402; Ticks von Hand, keine Echtzeit, zusammen rund 5 s Wanduhr je Lauf); drei verschränkte Läufe gegen `6c9dc5d` (Stand vor den Nachbesserungen, nur die Messziele gebaut, mit derselben Messdatei; deren Gedränge-Schaden bildet ohne `QcMobCramming` den damaligen `QcServerCreatures` nach: jedes Wesen nachschlagen, dann die Phase prüfen, dann über die Id verwunden). Last 7–14. Neuer Fall **cell-refill**: wie cell-cramming, aber vor jedem Tick wird die Zelle wieder auf 1000 Zombies aufgefüllt (ein Spawner ohne Ende, rund 1000 Gedrängte in jedem Tick).

| Fall | vorher Ø ms | nachher Ø ms | vorher max ms | nachher max ms |
|---|---|---|---|---|
| cell | 0,775 | 0,762 (−2 %) | 0,91 | 0,98 |
| cell-cramming | 0,113 | 0,092 (−18 %) | 1,92 | 1,40 |
| cell-refill | 1,036 | 0,892 (−14 %) | 2,46 | 2,05 |
| spread | 1,030 | 1,005 (−2 %) | 1,26 | 1,34 |
| farm | 0,021 | 0,021 (0 %) | 0,09 | 0,10 |

| Wegfindung | Zombies | vorher Ø ms | nachher Ø ms | Knoten je Tick | Suchen in 220 Ticks |
|---|---|---|---|---|---|
| hills | 200 | 0,261 | 0,260 | 41 → 40 | 1546 → 1614 |
| hills | 1000 | 1,333 | 1,325 | 213 → 229 | 6215 → 6385 |
| maze | 200 | 0,239 | 0,237 | 53 → 55 | 529 → 550 |
| maze | 1000 | 1,341 | 1,316 | 331 → 316 | 2478 → 2552 |
| closed | 200 | 0,416 | 0,364 (−13 %) | 788 → 546 | 545 → 455 |
| closed | 1000 | 2,005 | 1,917 (−4 %) | 3054 → 2789 | 2332 → 2271 |

Spannen über die drei Läufe höchstens ±4 %; die Fälle „gerade“ (ohne Wegfindung) liegen innerhalb von ±2 %.

**Bewertung:**

- **Gedränge-Schaden:** Das Nachschlagen jedes Gedrängten vor der Phasenprüfung (linear in der Liste) kostete bei rund 1000 Gedrängten je Tick etwa 0,15 ms; jetzt wird nur jedes vierte über seinen Index gefunden. Die Höchstzeit fällt im Tick der ersten Tode (1,9 → 1,4 ms).
- **Wegfindung:** Die neuen Zellarten, Höhen in Achteln und Fälle in tiefes Wasser ändern die Kosten nicht messbar. Vor dem Glasraum (closed) suchen Zombies seltener, weil ein bewegtes Ziel das Zurückweichen nicht mehr umgeht (−13 % bei 200).
- **Kein Fenster- oder `@server`-Benchmark gelaufen** (beim Etappen-Benchmark von 37.1 nachholen).

## Etappe 37.1 (Wegfindung und gestapelte Mobs)

Pflicht-Benchmark am Etappen-Ende (GUI-Messungen vom Menschen freigegeben, Fenster auf dem eingebauten Display im Hintergrund, `--screen builtin`). Release-Builds (`RelWithDebInfo`): `dev` `8d714f0` in einem temporären Worktree gegen `feature/37-1-pathfinding` `b9a8733` (Nachbesserungen E-402 eingeschlossen; die Messung lief mit `4e50e00`, das nur das neue Szenario ergänzt). Verschränkt mit rotierender Reihenfolge (E-331), Ton stumm, 8 Chunks, Mittel. Reihen: headless `@server` plus `flat-crowd` und `horde` (`bench-headless.sh`, adaptiv, 60 Spielsekunden, 06:46–06:48; farm-seeds, explore, signal_farm und flat-crowd 6 Runden, sonst 3); im Fenster `@core` plus `horde` (`bench-ab.sh`, adaptiv, alle Szenarien 6 Runden à 10 s, 06:48–07:07). Last 2,1–14,5, belastet markiert 10 von 60 Fensterläufen, verteilt auf beide Stände. Wanduhr samt Builds und Aufbau des Szenarios rund 25 min.

**Neues Szenario `horde`** (`scripts/bench-common.sh`): Gelände von Seed 42 am Spawn, Tag; der Spieler steht auf einer Steinsäule von 4 Blöcken, 64 Zombies (die Grenze `summonLimit`) werden in vier Gruppen 14 m östlich, westlich, südlich und nördlich 8 m über der Säulenspitze gerufen (per `/tp` hin und zurück). Sie sehen den Spieler, suchen Wege durch die Hügel zur Säule, erreichen ihn nie (Teilwege, Zurückweichen) und drängen sich an ihrem Fuß (Schub, Sichtstrahlen). Auf `dev` laufen sie gerade auf den Spieler zu. Rund 140 Wesen samt natürlichen Tieren. Mehr als 64 gerufene Wesen gehen per Chat nicht (`summonLimit` steht nur in `world.json`/`server.json`); den Fall mit 200 und 1000 Zombies und die Farm-Zelle decken `test_mob_path_measure` und `test_mob_crowd_measure` ab (oben, 37.1a/b und Nachbesserungen; Knoten je Tick: hills 1000 rund 230, closed 1000 rund 2800 von 4096).

**Showcase verschoben:** Die neue Station `path_station` verschiebt die Bereiche wieder; im Blick des Szenarios `showcase` stehen jetzt 14 statt 25 Entities beim Client (der Server hat 13 statt 10 Wesen: Zombie und zwei Schweine der Station), 158 statt 153 Draw-Calls. Die Werte von `showcase` sind darum nur eingeschränkt vergleichbar.

### Fenster: 37.1 gegen `8d714f0` (`@core` und `horde`, 6 Runden à 10 s)

| Szenario | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | Welt-Renderer ms | GUI-CPU ms | Entity-Vernetzung Ø / p99 ms | Tick Ø ms | Phase Wesen Ø ms | Spitzenspeicher MB | Draw-Calls |
|---|---|---|---|---|---|---|---|---|---|---|---|
| stand | 120 / 120 | 10,9 → 9,9 | 1,83 → 1,91 (+4 %) | 1,60 → 1,54 | 0,978 → 0,927 | 0,424 → 0,397 | 0,138 → 0,135 / 0,31 gleich | 0,592 → 0,651 | 0,451 → 0,519 | 449 → 450 | 347 → 347 |
| sprint | 120 / 120 | 10,9 → 10,1 | 2,12 → 2,14 | 1,60 → 1,57 | 1,01 → 0,985 | 0,413 → 0,416 | 0,130 → 0,132 / 0,27 → 0,28 | 0,566 → 0,685 | 0,425 → 0,536 | 561 → 561 | 388 → 388 |
| showcase | 120 / 120 | 10,2 → 10,4 | 0,594 → 0,624 (+5 %) | 1,13 → 1,09 | 0,519 → 0,508 | 0,647 → 0,559 (−14 %) | 0,327 → 0,226 / 0,64 → 0,47 | 0,339 → 0,389 | 0,063 → 0,100 | 333 → 335 | 153 → 158 |
| crowd | 120 / 120 | 10,1 → 11,2 (Median 10,2 → 10,4) | 2,57 → 2,47 (−4 %) | 1,38 → 1,34 | 1,07 → 1,02 | 0,821 → 0,811 | 0,501 → 0,494 / 1,00 → 0,99 | 0,846 → 0,999 | 0,666 → 0,817 | 455 → 456 | 346 → 345 |
| horde | 120 / 120 | 10,4 → 11,3 (Median 10,3 → 10,4) | 1,94 → 1,81 (−7 %) | 1,38 → 1,37 | 1,01 → 0,856 | 1,01 → 0,926 (−8 %) | 0,711 → 0,649 / 1,56 → 1,21 | 0,797 → 0,928 | 0,634 → 0,796 | 459 → 458 | 343 → 344 |

Keine übersprungenen Ticks, Tick max höchstens 5,0 ms. Frame Ø 8,33–8,65 ms in allen Läufen.

### Headless: 37.1 gegen `8d714f0` (Tick, Mittel über 3 bzw. 6 Runden)

| Szenario | Tick Ø ms | Tick p99 ms | Tick max ms | Phase Wesen Ø / max ms | Wesen | Spitzen-RSS MB |
|---|---:|---:|---:|---|---:|---:|
| crowd | 0,135 → 0,162 (+20 %) | 0,183 → 0,251 (+37 %) | 0,26 → 0,33 | 0,119 → 0,146 / 0,23 → 0,30 | 122 | 50,6 → 50,7 |
| flat-crowd | 0,0527 → 0,0598 (+14 %) | 0,075 → 0,087 (+16 %) | 0,11 → 0,12 | 0,0407 → 0,0480 / 0,088 → 0,100 | 56 → 54 | 42,5 → 42,8 |
| horde | 0,142 → 0,169 (+19 %) | 0,194 → 0,391 (+102 %) | 0,27 → 0,69 | 0,126 → 0,154 / 0,24 → 0,68 | 125 → 118 | 50,5 → 51,7 |
| explore | 0,177 → 0,194 (+10 %) | 0,872 → 0,891 (+2 %) | 1,20 → 1,26 | 0,051 → 0,067 / 0,14 → 0,26 | 58 | 56,3 → 57,2 |
| gravel-collapse | 0,497 → 0,491 | 1,42 → 1,43 | 1,72 → 1,62 | – | 0 | 71,1 → 70,9 |
| farm / farm-seeds | 0,0070 → 0,0073 / 0,0170 → 0,0173 | 0,014 → 0,015 / 0,053 → 0,055 | – | – | 0 | 44,2 → 44,4 / 43,8 → 43,6 |
| signal_components / machines / farm | 0,030 → 0,035 / 0,029 → 0,035 / 0,031 → 0,035 | 0,095 → 0,112 / 0,103 → 0,106 / 0,095 → 0,109 | 0,20–0,21 gleich | 0,009 → 0,013 / 0,006 → 0,013 / 0,009 → 0,013 | 10 → 13 (7 → 13) | 44,9–45,4 |

Spannen je Stand höchstens 13 %. Client-Sync, Streaming und die übrigen Phasen gleich.

**Bewertung:**

- **Fensterwerte innerhalb von 10 %, nichts zu orten außer p99:** GPU, Render-CPU, GUI-CPU, Entity-Vernetzung, Draw-Calls und Spitzenspeicher in stand, sprint, crowd und horde −8 bis +4 %. Frame p99 crowd +11 % und horde +8 % im Mittel kommen je von einem einzelnen Lauf mit 16,4 bzw. 16,6 ms (beide als belastet markiert, GPU-p99 dort 3,1 bzw. 2,5 ms wie in den anderen Läufen): verpasste VSyncs von außerhalb des Spiels, Median +2 %. Showcase GPU +5 % (alle sechs Läufe 0,62 statt 0,59 ms) und GUI-CPU −14 % (Entity-Vernetzung −31 %) folgen der verschobenen Showcase (mehr Geometrie, weniger Entities im Blick), keine Regression.
- **Server: die Wegfindung kostet, wie erwartet, rund 20 % in Mob-Szenen:** crowd +0,027 ms je Tick headless (Phase Wesen +0,027 ms: Wegsuchen, Zellenraster für den Schub, Sichtstrahlen alle 10 Ticks, Sichtlinie im Nahkampf), horde +0,027 ms im Mittel, p99 +0,2 ms und max 0,69 ms (Ticks, in denen viele der 64 Zombies zugleich suchen; das Budget von 4096 Knoten deckelt das). Im Fenster crowd 0,85 → 1,00 ms und sprint 0,57 → 0,69 ms Tick Ø (Phase Wesen +0,15 bzw. +0,11 ms, natürliche Tiere wandern jetzt über Wege), bei Spannen von 30–60 % je Stand. Die Signal-Stationen der Showcase steigen um 0,005 ms, weil die neue Station drei Wesen mehr in die geladenen Chunks bringt (Phase Wesen 0,009 → 0,013 ms, Signal gleich). Das entspricht den Kostenmessungen von E-400 bis E-402 (200 Zombies hills +0,07 ms, rund 0,2 µs je Knoten) und ist neue Arbeit, keine Regression; absolut höchstens 1 ms Tick Ø und 5 ms Tick max von 50 ms.
- **Gedränge:** crowd und horde halten ihre Wesen (keine Zelle über 24, kein Gedränge-Schaden); die Zellen mit 1000 Zombies und die Fallschacht-Farm sind in den headless Messungen oben bewertet (Farm −79 %, Zelle mit Regel −88 %).

**Absolute Ziele (`docs/Performance.md`, 8 Chunks):** Bildrate 120 FPS (Frame Ø 8,33–8,65 ms; Ziel ≥ 60), Frame-Kosten höchstens 2,69 ms (GPU crowd, einzelner Lauf; Mittel höchstens 2,57 ms; Ziel ≤ 4 ms) und p99 ≤ 12,5 ms im Median (10,3 ms über 60 Läufe; 4 Läufe über 12,5 ms, verteilt auf beide Stände, verpasste VSyncs bei Last) erfüllt. Spitzenspeicher stand, showcase, crowd, horde 331–459 MB, sprint (10 s) 561 MB, alle unter 600 MB; die offenen Befunde sprint 20 s (8 Chunks) und 16 Chunks nicht neu gemessen, durch 37.1 nicht berührt. Tick: kein Tick über 50 ms (Fenster höchstens 5,0 ms, headless 2,1 ms), keine übersprungenen Ticks.

### Fazit Etappe 37.1

Wegfindung und Gedränge kosten im Bild nichts: alle Fenster-Schlüssel innerhalb von 10 % (p99-Ausreißer einzelner belasteter Läufe, Median +2 %), Showcase durch die neue Station verschoben. Der Server zahlt in Mob-Szenen rund 20 % mehr Tick (0,03 ms headless, 0,1–0,15 ms im Fenster) für Wegsuchen, Schub und Sichtstrahlen, gedeckelt durch das Knotenbudget; Farmen und dichte Zellen werden dagegen deutlich billiger. Alle Ziele erfüllt.

## Etappe 37.2a: Dorf-Generierung (headless, 2026-10-16)

Messprogramm `test_village_measure` (Release, `QTCRAFT_VILLAGE_MEASURE=1`, ein Thread, Seed 42, je Wert das Beste aus 3 Läufen, drei Aufrufe nacheinander): Chunks um das Dorf bei 2067 237 erzeugt, einmal mit den Dörfern des Grundspiels, einmal mit derselben Welt ohne Dörfer (Dorf-Chance 0); dazu die Planung der Bereiche um einen Punkt, wie `/village` und `/locate village` sie auf dem Server-Thread machen (bis 13 × 13 Bereiche im Umkreis von 2048 Blöcken).

| Messung | mit Dörfern | ohne Dörfer | Änderung |
|---|---|---|---|
| 32 × 32 Chunks um das Dorf, ms je Chunk | 0,569 / 0,574 / 0,571 | 0,568 / 0,570 / 0,570 | +0,1 / +0,8 / +0,2 % |
| 8 × 8 Chunks des Dorfs, ms je Chunk | 0,567 / 0,554 / 0,555 | 0,579 / 0,565 / 0,563 | −2,1 / −1,9 / −1,4 % |
| Nächstes Dorf im Umkreis von 2048 Blöcken, erste Suche | 8,8 / 8,4 / 8,5 ms | – | – |
| dieselbe Suche, Bereiche schon geplant | 0,002 / 0,001 / 0,001 ms | – | – |

**Bewertung:** Dörfer kosten die Weltgenerierung nichts Messbares. Der Plan eines Bereichs (rund 0,05 ms, aus dem Gelände-Rauschen) entsteht einmal und liegt im Zwischenspeicher; das Schreiben der Häuser und Wege wiegt das Weglassen der Bäume und Pflanzen in den Dorf-Spalten auf (die Chunks des Dorfs selbst sind 1–2 % schneller). Die erste Suche nach dem nächsten Dorf kostet auf dem Server-Thread rund 9 ms (169 Bereiche) und ist danach frei; sie läuft nur auf Befehl eines Operators.

## Etappe 37.2a2: Dorf-Vielfalt (headless, 2026-10-16)

Messprogramm `test_village_measure` wie bei 37.2a (Release, `QTCRAFT_VILLAGE_MEASURE=1`, ein Thread, Seed 42, je Wert das Beste aus 3 Läufen, drei Aufrufe nacheinander); das Dorf bei 2067 237 hat jetzt ein Tiergehege und eine Fischerhütte mit Steg. Der Rechner war von anderen Sitzungen belastet (Last 20–22); eine erste Serie unter noch höherer Last lag bei 0,69–0,80 ms je Chunk für beide Welten und ist verworfen.

| Messung | mit Dörfern | ohne Dörfer | Änderung |
|---|---|---|---|
| 32 × 32 Chunks um das Dorf, ms je Chunk | 0,586 / 0,578 / 0,581 | 0,578 / 0,581 / 0,586 | +1,4 / −0,5 / −0,8 % |
| 8 × 8 Chunks des Dorfs, ms je Chunk | 0,565 / 0,559 / 0,571 | 0,586 / 0,574 / 0,635 | −3,5 / −2,6 / −10,1 % |
| Nächstes Dorf im Umkreis von 2048 Blöcken, erste Suche | 10,6 / 10,5 / 10,5 ms | – | – |
| dieselbe Suche, Bereiche schon geplant | 0,002 / 0,002 / 0,002 ms | – | – |

**Bewertung:** Die Erzeugung je Chunk bleibt bei rund 0,58 ms und mit Dörfern gleich teuer wie ohne. Die Planung wird teurer, weil jedes Gebäude mit Wahl oder optionalen Teilen seine eigene Struktur bekommt (Kopie der Zellen, Betten und Arbeitsplätze neu gesucht) und die Regeln Plätze und Stege prüfen: die erste Suche über 169 Bereiche rund 10,5 statt 8,5 ms (unter Last gemessen, also eher weniger), weiterhin nur auf Befehl eines Operators und danach aus dem Zwischenspeicher.

## Etappe 37.2a4: Sumpf-Dorf (headless, 2026-10-16)

Messprogramm `test_village_measure` (Release, `QTCRAFT_VILLAGE_MEASURE=1`, ein Thread, Seed 42, je Wert das Beste aus 3 Läufen, drei Aufrufe nacheinander), neu mit dem ersten Sumpf-Dorf um den Ursprung (563 −204: Stelzen, Stege, Treppen und das geformte Sumpfland, E-407). Rechner von anderen Sitzungen belastet (Last 10–13).

| Messung | mit Dörfern | ohne Dörfer | Änderung |
|---|---|---|---|
| Ebene: 32 × 32 Chunks um das Dorf bei 2067 237, ms je Chunk | 0,574 / 0,570 / 0,574 | 0,574 / 0,570 / 0,578 | ±0,0 / ±0,0 / −0,7 % |
| Ebene: 8 × 8 Chunks des Dorfs | 0,564 / 0,564 / 0,562 | 0,563 / 0,567 / 0,572 | +0,1 / −0,7 / −1,7 % |
| Sumpf: 32 × 32 Chunks um das Dorf bei 563 −204 | 0,584 / 0,583 / 0,589 | 0,580 / 0,576 / 0,583 | +0,8 / +1,1 / +1,0 % |
| Sumpf: 8 × 8 Chunks des Dorfs | 0,607 / 0,609 / 0,610 | 0,568 / 0,573 / 0,579 | +6,9 / +6,3 / +5,5 % |
| Nächstes Dorf im Umkreis von 2048 Blöcken, erste Suche | 13,4 / 13,5 / 13,7 ms | – | – |
| dieselbe Suche, Bereiche schon geplant | 0,002 / 0,002 / 0,002 ms | – | – |

**Bewertung:** Dörfer der Ebene unverändert. Die Chunks eines Sumpf-Dorfs kosten rund 0,035 ms mehr (+6 %): das Sumpfland rechnet je Spalte des Dorfs die Gelände-Höhe und den Abstand zu Häusern und Stegen, dazu kommen die Pfosten und rund 500 Stegzellen; über die 32 × 32 Chunks um das Dorf bleibt es bei rund 1 %. Die erste Suche nach dem nächsten Dorf steigt von 10,5 auf 13,5 ms, weil ein Bereich mit einer nassen Mitte alle seine Versuche prüft (die nasseste gewinnt) und Sumpf-Dörfer dazukommen; sie läuft nur auf Befehl eines Operators, danach ist sie frei.

## Etappe 37.2a5: Dorf-Grundrisse (headless, 2026-10-16)

Messprogramm `test_village_measure` (Release, `QTCRAFT_VILLAGE_MEASURE=1`, ein Thread, Seed 42, je Wert das Beste aus 3 Läufen, drei Aufrufe nacheinander). Vorher-Werte auf demselben Zweig vor der Änderung (Last 2), Nachher-Werte unter Last 7–12 einer anderen Sitzung; die Welt ohne Dörfer zeigt, wie stark die Last allein wirkt (+4–6 %). Die Ebene-Mitte liegt jetzt bei 2067 243 (Glocke, der Brunnen ist anders gedreht), das Sumpf-Dorf bei 555 −214 (eine Hauptstraße).

| Messung | vorher mit / ohne Dörfer | nachher mit / ohne Dörfer |
|---|---|---|
| Ebene: 32 × 32 Chunks um das Dorf, ms je Chunk | 0,530–0,541 / 0,529–0,541 | 0,560–0,562 / 0,559–0,563 (±0,4 %) |
| Ebene: 8 × 8 Chunks des Dorfs | 0,520–0,536 / 0,522–0,547 | 0,552–0,556 / 0,553–0,558 (−0,4 bis +0,1 %) |
| Sumpf: 32 × 32 Chunks um das Dorf | 0,546–0,561 / 0,545–0,558 | 0,590–0,593 / 0,588 (+0,3 bis +0,8 %) |
| Sumpf: 8 × 8 Chunks des Dorfs | 0,568–0,576 / 0,542–0,550 | 0,605–0,611 / 0,583–0,585 (+3,4 bis +4,7 %) |
| Nächstes Dorf im Umkreis von 2048 Blöcken, erste Suche | 12,5 / 12,7 / 13,0 ms | 14,2 / 14,3 / 14,2 ms |
| dieselbe Suche, Bereiche schon geplant | 0,002 ms | 0,002 ms |

**Bewertung:** Die Erzeugung je Chunk bleibt mit und ohne Dörfer gleich teuer; das Sumpf-Dorf kostet wie bisher einige Prozent in seinen eigenen Chunks. Die erste Suche steigt um rund 1,3 ms (+10 %), davon ein Teil Last (die Chunks ohne Dörfer wurden um 5 % langsamer): Wege werden Zelle für Zelle gelegt und in einer Menge gemerkt, geschwungene Wege lesen das Gelände voraus, Häuser an Diagonalen suchen ihren Abstand und Vorplatz. Die Suche läuft nur auf Befehl eines Operators, danach aus dem Zwischenspeicher.

## Etappe 37.2a: Nachbesserungen nach der Prüfung der Dorf-Generierung (headless, 2026-10-16)

Messprogramm `test_village_measure` (Release, `QTCRAFT_VILLAGE_MEASURE=1`, ein Thread, Seed 42, je Wert das Beste aus 3 Läufen, drei Aufrufe nacheinander), Rechner unter Last 10–12 anderer Sitzungen. Neu: der Speicher der Pläne (`memoryOfThePlans`, E-409), gezählt als Heap der Listen nach ihrer Kapazität und der Strukturen, die nur der Plan hält (die Grundbaupläne der Stile zählen nicht), über die 25 × 25 Bereiche um den Ursprung.

| Messung | 37.2a5 | nachher mit / ohne Dörfer |
|---|---|---|
| Ebene: 32 × 32 Chunks um das Dorf, ms je Chunk | 0,560–0,562 / 0,559–0,563 | 0,559–0,566 / 0,562–0,567 (−1,4 bis +0,7 %) |
| Ebene: 8 × 8 Chunks des Dorfs | 0,552–0,556 / 0,553–0,558 | 0,550–0,563 / 0,560–0,564 (−2,5 bis +0,4 %) |
| Sumpf: 32 × 32 Chunks um das Dorf | 0,590–0,593 / 0,588 | 0,593–0,602 / 0,591–0,596 (−0,4 bis +1,8 %) |
| Sumpf: 8 × 8 Chunks des Dorfs | 0,605–0,611 / 0,583–0,585 | 0,615–0,628 / 0,578–0,588 (+6,4 bis +6,9 %) |
| Nächstes Dorf im Umkreis von 2048 Blöcken, erste Suche | 14,2 / 14,3 / 14,2 ms | 15,2 / 15,3 / 14,9 ms |
| Speicher je Dorf-Plan (Mittel / größter) | – (vor `squeeze`: 35,4 / 61,1 KB) | 33,4 / 53,6 KB: Strukturen 22,1, Platzierungen 2,5, Wegzellen 7,2, Rest 1,5 KB |
| Voller Zwischenspeicher (512 Bereiche, 105 Dörfer in 625 Bereichen) | – | rund 2,8 MB, höchstens rund 28 MB |

**Bewertung:** Die Erzeugung je Chunk bleibt gleich; das Sumpf-Dorf kostet in seinen eigenen Chunks unter der Last etwas mehr als zuvor gemessen (die Welt ohne Dörfer streut um 2 %). Die erste Suche wird rund 1 ms (+7 %) teurer: Straßendörfer tragen mehr Häuser (Nebenstraßen alle 12 Blöcke, ein Haus wird nach einem Fehlversuch schon einen Block weiter versucht), und Häuser an Diagonalen suchen ihre Türen für den Vorplatz. Ein Plan hält im Mittel rund 33 KB, vor allem die abgewandelten Gebäude und die Wegzellen; `squeeze` am Ende der Planung spart rund 12 %. Ein voller Zwischenspeicher von 512 Bereichen bleibt bei wenigen MB, `cachedPlans` bleibt (die Suche von `/locate village` braucht 169 Bereiche).

## Etappe 37.2a (Dorf-Generierung) headless (2026-10-16)

Etappenende 37.2a–a5 samt E-410 (ein durchlaufendes Signal hält seinen Chunk, `QcChunkLoading::holdPassingSignals`): Kandidat `a14fbbc` gegen `dev` (`1ef69d8`), beide Release, `scripts/bench-headless.sh` verschränkt, 60 Spielsekunden, Runden `auto`; Rechner mit Last 3–5 anderer Sitzungen. Zur Ortung zusätzlich ein Stand nur mit E-410 (`dd19ef4`, `dev` plus die Fehlerbehebung) und ein Kandidat mit den wilden Karotten und Kartoffeln zurück in den Biomen. Kein Fensterlauf (der Mensch arbeitet am Rechner).

| Szenario | Tick Ø ms | Tick p99 ms | Tick max ms | auffällige Phase Ø ms | Wesen | Spitzen-RSS MB |
|---|---:|---:|---:|---|---:|---:|
| crowd | 0,165 → 0,168 (+2 %) | 0,255 → 0,253 | 0,34 → 0,31 | Wesen 0,149 → 0,151 | 121 | 51,4 → 53,3 |
| gravel-collapse | 0,509 → 0,504 (−1 %) | 1,45 → 1,46 | 1,61 → 1,67 | fallende Blöcke 0,309 → 0,306 | 0 | 70,9 → 72,5 |
| farm / farm-seeds | 0,0080 → 0,0080 / 0,0177 → 0,0177 | 0,014 → 0,015 / 0,057 → 0,055 | – | – | 0 | 44,3 → 45,8 / 44,0 → 44,9 |
| explore (60 s) | 0,201 → 0,202 (+0,5 %) | 0,935 → 0,947 | 1,30 → 1,27 | Chunk-Laden 0,0416 → 0,0421 | 54 | 57,0 → 59,3 |
| explore (300 s, 3 Runden) | 0,196 → 0,197 (+0,7 %) | 0,976 → 0,987 | 1,29 → 1,48 (+15 %) | Chunk-Laden 0,047 → 0,048, max 0,82 → 1,00 | 54 | 60,5 → 61,7 |
| explore (300 s, 4 Runden, dev / nur E-410 / Kandidat) | 0,200 / 0,201 / 0,216 | 0,983 / 0,993 / 1,027 | 1,35 / 1,37 / 1,57 | Chunk-Laden max 0,84 / 0,89 / 1,06; Wesen 0,060 / 0,059 / 0,073 | 54–55 | 59,8 / 59,3 / 62,0 |
| signal_components / machines / farm (Wiederholung, 6 Runden) | 0,0373 → 0,0368 / 0,0363 → 0,0365 / 0,0368 → 0,0368 | 0,118 → 0,114 / 0,113 → 0,110 / 0,116 → 0,114 | 0,18–0,22 gleich | Signal 0,014 gleich | 13 | 45,4–45,9 → 47,0–47,7 |
| light / glass / glass-full (`@light`, zweite Reihe, 6 Runden) | 0,126 → 0,101 / 0,129 → 0,103 / 0,124 → 0,103 | 0,208 → 0,174 / 0,214 → 0,172 / 0,197 → 0,173 | – | Wesen 0,116 → 0,091 (−21 %) | 75–76 | 52,1–52,4 → 53,6–53,7 |
| void-light | 0,0042 → 0,0040 | 0,0082 → 0,0072 | – | – | 0 | 30,9 → 31,8 |

Erzeugung je Chunk (`test_village_measure`, Release, `QTCRAFT_VILLAGE_MEASURE=1`, ein Thread, Seed 42): Ebene 32 × 32 Chunks um das Dorf 0,559 mit / 0,564 ms ohne Dörfer (−0,9 %), 8 × 8 Chunks des Dorfs 0,549 / 0,538 (+2,1 %); Sumpf 32 × 32 0,599 / 0,596 (+0,4 %), 8 × 8 0,616 / 0,591 (+4,3 %); erste Suche des nächsten Dorfs 14,9 ms; 33,4 KB je Plan. Wie in den Nachbesserungen oben.

**Bewertung:**

- **Keine Regression über 10 % im Tick Ø.** Die erste `@server`-Reihe zeigte signal_farm +10 % (Phase Wesen 0,013 → 0,017 ms bei gleich vielen Wesen), die erste `@light`-Reihe light +25 % und glass +28 % (Phase Wesen 0,088 → 0,113 ms). Wiederholt mit 6 festen Runden: Signal-Stationen −1 bis +0,5 %, light und glass jetzt −20 % (dev langsam). Die Phase Wesen dieser Szenen ist zweigipflig (rund 0,09 oder 0,115 ms in light/glass), und innerhalb einer Reihe landen alle Läufe eines Stands auf demselben Gipfel, welcher, wechselt von Reihe zu Reihe; der Code der Wesen je Tick ist unverändert (nur `QcMobManager::spawnVillageMobs` beim ersten Laden eines Chunks), und mit den wilden Feldfrüchten zurück in den Biomen bleibt das Bild gleich (light dev 0,101 / Kandidat 0,101 / mit Feldfrüchten 0,101 ms). Streuung der Reihe, kein Befund. Ebenso Phase Wesen im 300-s-Erkundungsflug (+22 % in der Dreierreihe, ±0 % in der Reihe davor).
- **Erkundungsflug: Tick max +0,2 ms durch die Dorf-Chunks.** In beiden 300-s-Reihen liegt das Maximum der Phase Chunk-Laden bei jedem Lauf des Kandidaten bei 0,99–1,10 ms statt 0,78–0,89 ms; der Stand nur mit E-410 liegt bei 0,85–0,93 ms (+6 %, in der Streuung). Ursache sind die Dörfer am Flugweg (x 4801 und 9410 bei Seed 42 innerhalb der Sicht): kommt ein Dorf-Chunk an, legt `QcVillages::noteChunk` das Dorf an und durchsucht den Chunk nach Betten und Arbeitsplätzen (`rescan`), und `QcMobManager::populateChunk` setzt die Tiere der Gehege. Absolut 1,6 ms Tick max von 50 ms; Tick Ø und p99 bleiben innerhalb von 1–4 %. Im 60-s-Flug (2,4 km, kein Dorf in Sicht) kein Unterschied.
- **E-410 kostet nichts Messbares:** Chunk-Laden Ø 0,0484 → 0,0491 ms (+1,4 %) im Flug, der ständig Chunks entlädt; die Signal-Stationen unverändert.
- **Speicher +1,5 bis +2,3 MB** in jedem Szenario (Inhalt der Dörfer: Stile, Baupläne, Grundrisse; im Flug zusätzlich die Pläne im Zwischenspeicher).
- Ein eigenes headless Szenario durch ein Dorf wurde nicht angelegt (Szenen stehen in `scripts/bench-common.sh`, nicht Teil dieser Messung); der 300-s-Erkundungsflug kreuzt bei Seed 42 zwei Dörfer, und `test_village_measure` misst die Erzeugung der Dorf-Chunks direkt.

**Absolute Ziele (`docs/Performance.md`):** kein Tick über 50 ms (headless höchstens 1,67 ms in gravel-collapse, 1,64 ms im Erkundungsflug), keine übersprungenen Ticks (headless ohne Echtzeit). Bildrate, Frame-Kosten, p99 und Spitzenspeicher des ganzen Spiels werden nur im Fenster geprüft.

**Offen:** die gerenderte Messung von 37.2a (`@core` im Fenster plus eine Szene mit Dörfern in Sicht: Vernetzung und GPU der Häuser, Wege und Felder, `explore` mit `STATS=5000` für die Speicherkurve), sobald der Mensch Fensterläufe freigibt.

## Etappe 37.2b (Dorfbewohner) headless (2026-10-16)

Messprogramm `test_villager_measure` (Release, `QTCRAFT_VILLAGER_MEASURE=1`, Seed 42, drei Aufrufe nacheinander, Rechner mit Last 7–15 anderer Sitzungen): die Dörfer nächst dem Ursprung mit allen Chunks geladen, ein Bewohner in jedem Bett und sonst kein Wesen, ein ganzer Tag ab Sonnenaufgang (12 000 Ticks, so schnell der Server kann) mit Spaziergang, Arbeit, Treffen, Heimweg und Schlaf; danach derselbe Tag auf denselben Chunks ohne Bewohner. Werte je Tick über den ganzen Server-Tick.

| Szene | Bewohner | Chunks | Tick Ø ms mit / ohne | Tick p99 ms mit / ohne | Tick max ms mit | im Bett um Mitternacht |
|---|---:|---:|---|---|---:|---:|
| ein Dorf (Sumpf, 555 −214) | 20 | 57 | 0,027–0,028 / 0,002 | 0,156–0,161 / 0,005 | 0,63–0,65 | 10 |
| zehn Dörfer | 159 | 325 | 0,170–0,173 / 0,006–0,008 | 0,524–0,535 / 0,012–0,014 | 1,45–1,57 | 146 |

**Bewertung:** Ein Bewohner kostet im Mittel rund 1,0–1,3 µs je Tick (20 Bewohner +0,025 ms, 159 Bewohner +0,165 ms), linear in ihrer Zahl; die Spitzen (p99, max) kommen von den Entscheidungen mit Wegsuche (höchstens `villagerThoughtsPerTick` je Tick) und bleiben bei zehn Dörfern unter 1,6 ms von 50 ms. Bei 1024 Bewohnern (Grenze `villagers`) wären rund 1,1 ms Tick Ø zu erwarten. Die erste Messung zeigte einen Fehler: Die 20 Bewohner des Sumpfdorfs trafen sich an der Glocke, bildeten ein Gedränge und dachten als gedrosselte Wesen nie wieder, so blieben sie die ganze Nacht beim Treffen (um Mitternacht 0 von 20 im Bett, bei zehn Dörfern 102 von 159). Seitdem denken Bewohner im Gedränge in ihrem Takt weiter (`QcMobBehaviour::update`, `test_villagers::aCrowdAtTheBellStillFollowsTheRoutine`); die Kosten blieben gleich (vorher 0,025 / 0,167 ms Tick Ø). Damals offen: 10 der 20 Sumpf-Bewohner liegen um Mitternacht noch nicht im Bett (Wege über die Stege, nicht untersucht).

Nach den Nachbesserungen der Prüfung (E-411, 2026-10-16; Release, ein Aufruf, Rechner unter Last): ein Dorf 20 Bewohner 0,0245 ms Tick Ø (p99 0,190, max 0,670) gegen 0,0024 ohne; zehn Dörfer 159 Bewohner 0,168 ms (p99 0,558, max 1,59) gegen 0,0094. Um Mitternacht im Bett: Sumpfdorf über den ganzen Tag 17 von 20 (vorher 10), ab Tagesanteil 0,2 20 von 20 (Test `theSwampVillageSleepsAtMidnight`, vorher 9), zehn Dörfer 159 von 159. Kosten unverändert im Rahmen der Streuung.

Nach den letzten Nachbesserungen (E-411 (8)–(12), (14): Türen nacheinander, Sprung aus dem Wasser ans Ufer, Raster für `lookOut`, Felder im Plan; Release, zwei Aufrufe, Rechner unter Last): ein Dorf 20 Bewohner 0,021–0,022 ms Tick Ø (p99 0,160–0,174, max 0,57–0,64) gegen 0,002–0,003 ohne; zehn Dörfer 159 Bewohner 0,167–0,173 ms (p99 0,539–0,568, max 1,57–1,76) gegen 0,007–0,009. Um Mitternacht im Bett: Sumpfdorf über den ganzen Tag 20 von 20 (vorher 17), zehn Dörfer 159 von 159. Kosten unverändert im Rahmen der Streuung.

### Etappenende 37.2b: A/B gegen `dev` (headless, 2026-10-16)

Etappenende 37.2b (Dorfbewohner, E-411) samt E-413 (ein Abschnitt je Frame nur einmal hochladen; betrifft nur den Renderer, headless nicht messbar): Kandidat `deb1e72` gegen `dev` (`a5a7860`), beide Release, `scripts/bench-headless.sh` verschränkt, 60 Spielsekunden. Erste Reihe `@server`, `horde` und `@light` mit Runden `auto`; zweite Reihe mit 6 festen Runden für die Szenen mit Wesen (crowd, horde, light, glass-full, explore, signal_farm). Rechner mit Last 3–5,5. Kein Fensterlauf (der Mensch arbeitet am Rechner). Werte: Mittel `dev` → Kandidat.

| Szenario | Tick Ø ms (1. / 2. Reihe) | Tick p99 ms | Tick max ms | Phase Wesen Ø ms (1. / 2. Reihe) | Wesen | Spitzen-RSS MB |
|---|---|---|---|---|---:|---:|
| crowd | 0,170 → 0,208 (+22 %) / 0,171 → 0,174 (+2 %) | 0,266 → 0,300 / 0,265 → 0,262 | 0,38 → 0,37 / 0,33 → 0,36 | 0,153 → 0,190 / 0,153 → 0,157 | 122 | 53,2 → 53,2 |
| horde | 0,179 → 0,220 (+23 %) / 0,182 → 0,179 (−2 %) | 0,413 → 0,438 / 0,423 → 0,418 | 0,71 → 0,81 / 0,68 → 0,80 (+15 / +18 %) | 0,163 → 0,204 / 0,166 → 0,163; max 0,62–0,74 → 0,72–0,86 | 118–121 | 53,2 → 53,5 |
| gravel-collapse | 0,511 → 0,510 (−0 %) | 1,46 → 1,47 | 1,63 → 1,67 | fallende Blöcke 0,309 → 0,310 | 0 | 72,5 → 73,0 |
| farm / farm-seeds | 0,0077 → 0,0080 / 0,018 → 0,018 | 0,015 → 0,015 / 0,058 → 0,056 | – | – | 0 | 45,9 → 46,0 / 45,0 → 45,2 |
| explore (60 s) | 0,209 → 0,227 (+8 %) / 0,208 → 0,210 (+1 %) | 0,961 → 0,979 / 0,961 → 0,964 | 1,31 → 1,32 / 1,27 → 1,46 (ein Lauf 2,07) | 0,072 → 0,089 / 0,072 → 0,072 | 57 | 59,0 → 59,5 |
| signal_components / machines | 0,0377 → 0,0412 (+9 %) / 0,0360 → 0,0392 (+9 %) | 0,113 → 0,123 / 0,111 → 0,121 | 0,20 → 0,22 / 0,21 → 0,24 | 0,014 → 0,016 / 0,013 → 0,015 | 13 → 14 | 47,0–47,3 → 48,1–48,3 |
| signal_farm | 0,0370 → 0,0395 (+7 %) / 0,0368 → 0,0397 (+8 %) | 0,116 → 0,119 / 0,115 → 0,119 | 0,22 gleich | 0,013 → 0,015 / 0,013 → 0,015 | 13 → 14 | 46,8–47,1 → 48,0 |
| light / glass / glass-full | 0,103 → 0,124 / 0,102 → 0,128 / 0,103 → 0,128 (+20 bis +25 %); 2. Reihe light 0,102 → 0,103, glass-full 0,102 → 0,103 (+1 %) | 0,17 → 0,20 / 0,17 → 0,17 | 0,22–0,25 → 0,26–0,27 / 0,22–0,23 → 0,24–0,25 | 0,092 → 0,114–0,118 / 0,092 → 0,093 | 75 | 53,3–53,7 → 53,7–54,3 |
| void-light | 0,004 → 0,004 | 0,0087 → 0,0083 | – | – | 0 | 31,7 → 32,1 |

**Bewertung:**

- **Keine Regression über 10 % im Tick Ø, die sich wiederholt.** Die erste Reihe zeigte in allen Szenen mit vielen Wesen +20 bis +25 % (crowd, horde, light, glass, glass-full), jedes Mal nur in der Phase Wesen bei gleich vielen Wesen. Die zweite Reihe mit 6 festen Runden: −2 bis +2 %. Es ist derselbe zweigipflige Verlauf der Phase Wesen wie bei 37.2a (rund 0,09 oder 0,115 ms in light/glass, 0,15 oder 0,19–0,21 ms in crowd/horde): Innerhalb einer Reihe landen die Läufe eines Builds fast alle auf demselben Gipfel (erste Reihe: Kandidat 5 von 6 oben, `dev` 6 von 6 unten). Auch ein einzelner 3000-s-Lauf von `horde` je Build unter `sample` (0,115 → 0,129 ms, +12 %) zeigt keine einzelne neue Funktion. Die Anteile am Tick bleiben gleich (Wesen 80 → 81 %, `QcServerWorld::blockAt` 33 %, Wegsuche 7–8 %), nur das Bewegen (`QcPhysics::stepEntity`/`moveBox`) steigt leicht von 54 auf 57 %. Neu im Bewegen sind der zweite, niedrige Stufenversuch (`lowStepHeight`, 0,125) für ein Wesen an einer Wand, das die volle Stufe nicht schafft (bis zu drei weitere `moveBox`), und der Wasser-Test an den Füßen in `QcMobBehaviour::walk`. Keine Ursache für +20 %; Streuung zwischen Reihen, kein Befund.
- **horde: Tick max +0,1 ms, wiederholt.** In beiden Reihen liegt das Maximum der Phase Wesen beim Kandidaten bei 0,72–0,86 ms statt 0,61–0,74 ms (Tick max +15 / +18 %), bei unverändertem Tick Ø und p99. Das ist ein einzelner Tick unter 1 200, in dem viele der Zombies am Pfeiler gleichzeitig entscheiden und gehen. Naheliegend sind der zweite Stufenversuch an der Wand (die Zombies drängen gegen den Pfeiler) und die Sprungregel, die eine Wand nur noch am Boden oder im Wasser zählt. Ohne Code-Änderung nicht weiter getrennt. Absolut 0,8 ms von 50 ms, weit unter dem Ziel.
- **Signal-Stationen +7 bis +9 % durch ein Wesen mehr.** Der Kandidat hat in allen drei Stationen in jedem Lauf 14 statt 13 Wesen. Die Phase Wesen steigt um rund 0,002 ms; Signal und alle anderen Phasen bleiben gleich (+1 bis +5 %). Das wiederholt sich (signal_farm in der zweiten Reihe +8 %), ist aber die Arbeit eines zusätzlichen Wesens in der Sicht: Wahrscheinlich ein Bewohner aus dem Dorfbereich der Vorführwelt, der beim ersten Laden seines Chunks einzieht. Absolut 0,04 ms Tick Ø.
- **explore (60 s):** Tick Ø +1 bis +8 % (Phase Wesen auch hier zweigipflig). In der zweiten Reihe hatte ein Lauf des Kandidaten 2,07 ms Tick max (Chunk-Laden 1,06 ms), die übrigen 1,27–1,46 ms wie `dev`. Kein Dorf liegt auf dem 60-s-Flug. Dass ein ankommender Dorf-Chunk jetzt Bewohner einziehen lässt, zeigt erst der 300-s-Flug, der hier nicht gemessen wurde (bei 37.2a: Chunk-Laden max +0,2 ms durch die Dorf-Chunks).
- **Speicher +0,3 bis +1,2 MB** (Bewohner-Inhalt: `villagers.json`, die verbundene Kleidung je Aussehen und Beruf).
- **Kosten der Bewohner selbst:** Die stehen oben (`test_villager_measure`): 1,0–1,3 µs je Bewohner und Tick, zehn Dörfer mit 159 Bewohnern 0,17 ms Tick Ø, max unter 1,8 ms. Nicht neu gemessen.

**Absolute Ziele (`docs/Performance.md`):** Kein Tick über 50 ms (headless höchstens 2,07 ms, ein Lauf im Erkundungsflug, sonst 1,67 ms in gravel-collapse). Keine übersprungenen Ticks (headless ohne Echtzeit).

**Offen:** Die gerenderte Messung (`@core` im Fenster plus eine Szene mit Dorf und Bewohnern in Sicht: Vernetzung und Animation der Bewohner, Kleidung, E-413) folgt nach Etappe 37, zusammen mit der von 37.2a, sobald der Mensch Fensterläufe freigibt.

## Etappe 37.2c (Handel) headless (2026-10-17)

`scripts/bench-headless.sh`, `@server`, 3 verschränkte Runden zu 60 Spielsekunden, Release, `dev` `1977abe` gegen den Kandidaten nach den Nachbesserungen (E-414). Tick im Mittel: crowd 0,164 → 0,165 ms (+1 %), explore 0,195 → 0,200 ms (+3 %), farm 0,0070 → 0,0073 ms (+5 %, absolut 0,0003 ms), farm-seeds, gravel-collapse und die drei Signal-Stationen ±2 %; p99 −5 bis +5 %; Spitzenspeicher ±0,6 MB. Keine Regression; der Handel kostet nur, wenn gehandelt wird. Gerenderte Messung (Handelsfenster) nach Etappe 37.

## Etappe 37.3 (Vermehrung, Golem-Regel, Schneedorf) headless (2026-10-17)

Messprogramm `test_villager_measure` (Release, `QTCRAFT_VILLAGER_MEASURE=1`, Seed 42, zwei Aufrufe nacheinander, Rechner unter Last anderer Sitzungen). Die zehn Dörfer nächst dem Ursprung sind seit dem Schneestil andere Bewohnerzahlen (Tundra-Dörfer bauen jetzt Iglus und Hütten): 155 statt 159. Neu ist die Szene `tenVillagesGrow`: in jedem Dorf ein Bett frei (sein Bewohner entfernt) und alle Bewohner mit Nahrung, so dass sich jedes Dorf am Tag vermehren kann; dazu die Zeit einer Vermehrungsprüfung aller Dörfer (`QcVillagerBreeding::check`, 200 Wiederholungen auf einer Kopie der Wesen).

| Szene | Bewohner | Tick Ø ms mit / ohne | Tick p99 ms mit | Tick max ms mit | Kinder | Prüfung µs | im Bett um Mitternacht |
|---|---:|---|---:|---:|---:|---:|---:|
| ein Dorf (Sumpf) | 20 | 0,021–0,022 / 0,002 | 0,164–0,165 | 0,63–0,65 | 0 | 0,55 | 20 |
| zehn Dörfer | 155 | 0,162–0,164 / 0,007–0,009 | 0,534–0,539 | 1,46–1,48 | 0 | 3,1–3,2 | 153 |
| zehn Dörfer, wachsend | 145 | 0,171–0,175 / 0,007–0,009 | 0,508–0,514 | 1,49–1,53 | 10 | 4,8 | 145–148 |

**Bewertung:** Die Vermehrung kostet nichts Messbares: eine Prüfung aller zehn Dörfer 3–5 µs alle 100 Ticks, also unter 0,05 µs je Tick; der Tick Ø bleibt bei 0,16 ms wie in 37.2b (0,168–0,173). Die wachsenden Dörfer liegen 5 % höher, weil die zehn Kinder (laufen schneller, mehr Wege) und die Eltern neue Ziele suchen; ein Kind je Dorf und Tag, wie gewollt. Die Golem-Regel ruht im Grundspiel (kein Golem-Wesen) und kostet eine Namenssuche je Prüfung.

**Gefunden beim Messen:** Im ersten Lauf starben in den Schneedörfern 10 von 34 Bewohnern im Schlaf: Das Iglu hat über dem Bett nur zwei Blöcke Luft, und das Ersticken (`QcEnvironmentDamage::applySuffocation`) prüfte die Augen eines liegenden Bewohners in stehender Höhe, im Dach. Jetzt liegen die Augen eines Schlafenden in der Mitte seiner liegenden Box (`QcEnvironmentDamage::eyesOf`, E-415); danach stirbt keiner mehr (153 von 155 um Mitternacht im Bett).

## Etappe 37.3 (Vermehrung, Golem-Regel, Schnee-Stil) headless (2026-10-17)

`scripts/bench-headless.sh`, `@server`, 3 verschränkte Runden zu 60 Spielsekunden, Release, `dev` `1843c3f` gegen den Kandidaten nach den Nachbesserungen (E-415). Tick im Mittel: crowd +0,2 %, explore +2,1 %, farm-seeds +2 %, gravel-collapse +0,6 %, Signal-Stationen −3,4 bis +0,9 %; p99 −4,6 bis +2,6 %; Spitzenspeicher ±0,8 MB. Keine Regression. Die Kosten der Vermehrung selbst stehen oben (`tenVillagesGrow`, unter 0,05 µs je Tick).

## 37.R1 (Plan-Zwischenspeicher, Leben nach Abgleich) headless (2026-10-17)

`scripts/bench-headless.sh`, `@server`, 3 verschränkte Runden zu 60 Spielsekunden, Release, `dev` `ec53772` gegen den Kandidaten (E-416). Tick im Mittel −1,2 bis +2,8 %, p99 −4,8 bis +5,1 %, Spitzenspeicher ±0,6 MB. Keine Regression. Ein Dorf neu zu planen kostet rund 3,3 ms (Debug); gehaltene Dörfer zahlen das nicht mehr (`test_village_plan_cache`).

## 37.R2 (Entity-Komponenten, dauerhafte Ids) headless (2026-10-17)

`QTCRAFT_ENTITY_MEASURE=full QTCRAFT_ENTITY_MEASURE_TICKS=20 build/release/tests/test_entity_measure` (RelWithDebInfo, Apple M4 Max; 289 Chunks mit je zwei Schweinen, vier Items und einem Ständer, 2034 Entities), `dev` `d61da73` (A) gegen den Branch (B), 15 verschränkte Runden, Mediane in ms (E-420):

| Stand von B | erstes Speichern | zweites Speichern | Öffnen | alle Chunks laden |
|---|---|---|---|---|
| nach der Prüfung, vor den Nachbesserungen (Komponente je Puffer, Leser als `std::function`) | 16,32 → 17,28 (+5,9 %) | 3,94 → 4,64 (+17,6 %) | 2,46 → 2,48 (+0,8 %) | 36,53 → 38,75 (+6,1 %) |
| Komponenten an Ort und Stelle im Strom, Leser als `FieldReader`, Längen eingesetzt statt gesucht | 15,06 → 15,48 (+2,8 %) | 3,62 → 3,86 (+6,6 %) | 2,33 → 2,37 (+1,9 %) | 34,63 → 36,14 (+4,4 %) |

**Bewertung:** Die Kosten vor den Nachbesserungen waren sichtbar (zweites Speichern +0,7 ms, rund 0,35 µs je Entity): ein `QDataStream` mit eigenem `QBuffer` und Puffer je Komponente beim Schreiben und Lesen, dazu `std::function` je bekanntem Leser. Jetzt schreibt `QcComponentFrames::Writer` direkt in den Strom des Chunks (Anzahl und Längen als Platzhalter, danach in den Puffer eingesetzt, ohne Suchen), und `QcComponentFrames::read` liest bekannte Komponenten an Ort und Stelle; nur unbekannte und Schwänze bekommen einen eigenen Puffer. Es bleiben rund 0,24 ms beim zweiten Speichern und 1,5 ms beim Laden aller Chunks (etwa 0,1 bzw. 0,7 µs je Entity): der größere Kopf (Typname, Geschwindigkeit, Reihenfolge und Id bei jeder Art) und die Rahmen je Komponente. Bei einigen tausend Entities je Welt und Speichern alle paar Minuten ist das vernachlässigbar.

## Dorf-Sektion (37) gerendert (2026-10-17)

Gerenderter Benchmark der ganzen Dorf-Sektion (37.1 bis 37.R3, dazu 53) samt dem offenen Loren-Szenario aus Etappe 32; Fensterläufe vom Menschen am 2026-10-17 freigegeben, Fenster auf dem eingebauten Display im Hintergrund (`--screen builtin`, Standard von `run_game`). Release-Builds (`RelWithDebInfo`) in temporären Worktrees: `8d714f0` (Etappe 36, vor 37; alte Presets in `CMakePresets.json`) gegen `dev` `f406734` (37.R3; `CMakeUserPresets.json` über `scripts/ensure-user-presets.sh`). Skripte und neue Szenarien aus `feature/bench-villages`. Verschränkt mit rotierender Reihenfolge (E-331), Ton stumm, 8 Chunks, Runden `auto`, 10 s je Lauf; Mittel [Spanne]. Reihen (15:27–16:12, Wanduhr samt Builds 15:18–16:12, rund 55 min): Fenster `@core village village-night village-swamp minecarts horde` (alle 6 Runden, 27 min), `explore` 75 s mit `STATS=5000` (3 Runden), `VIEW=16` mit `sprint village` (3 Runden), Wiederholung `horde` (6 Runden); headless `@server` plus `stand horde village village-night village-swamp minecarts` (60 Spielsekunden, adaptiv). 8 von 108 Fensterläufen der Hauptreihe belastet markiert (Safari zeitweise 80 % CPU), auf beide Stände verteilt.

**Neue Szenarien** (`scripts/bench-common.sh`):

- `village`: Seed 42, Tag (Arbeitszeit der Bewohner), Spieler auf einer Steinsäule von 12 Blöcken 45 m südlich des Ebenen-Dorfs nächst dem Spawn (Mitte 964, −634; `/village 964 -634` auf `f406734`: `qtcraft:plains`), Blick nach Norden über das Dorf (`--spawn 964,-589 --look 0,-15`). `village-night`: dasselbe mit `/time set night` (Bewohner im Bett). `village-swamp`: 45 m südlich des Sumpf-Dorfs auf Stegen (Mitte 555, −214; `--spawn 555,-169`). `8d714f0` steht am selben Ort über Gelände ohne Dorf (Kostenreferenz).
- `minecarts` (aus 32 offen): flache Welt, zwei quadratische Schleifen (32 × 25 m) aus Antriebsschienen mit einem eingeschalteten Hebel neben jeder Schiene, 64 Loren (`/summon minecart 16`, je eine Reihe auf Nord- und Südseite). Ein Stein in der Nordwest- und Südost-Ecke stößt die Lore daneben an (eine eingeschaltete Antriebsschiene startet eine stehende Lore von einem Block am Gleisende weg), sie schiebt ihre Reihe an, nach 1,5 s werden die Steine zu Schienen und schließen die Schleifen. Blick von einer Säule (10 m) westlich der Schleifen. Läuft auf beiden Ständen gleich (64 Entities beim Client, Phase Loren 0,23–0,26 ms im Fenster). **Mitfahren mit der Kamera** geht nicht: keine Szene setzt den Spieler in eine Lore (kein Befehl dafür, Benutzung von Entities gibt es in den Szenen nicht).
- **Handelsfenster:** nicht gemessen. `--overlay` kennt `trade` nicht, und die Szenen haben keine Entity-Benutzung; das Handelsfenster öffnet nur die Steuerschnittstelle (`openTrade`), die das Fenster-Benchmark nicht nutzt.

### Fenster: `f406734` gegen `8d714f0` (8 Chunks, 6 Runden)

| Szenario | FPS | Frame p99 ms | GPU Ø ms | Render-CPU ms | GUI-CPU ms | Entity-Vernetzung Ø ms | Draw-Calls | Tick Ø ms | Wesen (Client / Server) | Spitzenspeicher MB |
|---|---|---|---|---|---|---|---|---|---|---|
| stand | 117 / 118 | 15,2 → 15,4 | 1,70 → 1,69 | 1,72 → 1,63 | 0,411 → 0,401 | 0,136 → 0,137 | 347 → 347 | 0,657 → 0,776 (+18 %) | 19 → 20 / 90 | 450 → 461 |
| sprint | 118 / 117 | 15,1 → 15,1 | 1,98 → 2,00 | 1,71 → 1,77 | 0,410 → 0,420 | 0,133 → 0,132 | 388 → 388 | 0,674 → 0,690 | 19 / 98 | 561 → 570 |
| showcase ¹ | 119 / 119 | 12,0 → 13,3 | 0,587 → 0,613 (+4 %) | 1,21 → 1,16 | 0,636 → 0,554 (−13 %) | 0,328 → 0,227 | 153 → 158 | 0,406 → 0,417 | 25 → 14 / 10 → 14 | 334 → 347 |
| crowd | 117 / 118 | 16,3 → 16,1 | 2,25 → 2,21 | 1,47 → 1,44 | 0,817 → 0,772 | 0,495 → 0,462 | 346 → 345 | 0,909 → 1,071 (+18 %) | 92 → 91 / 135 | 456 → 466 |
| village | 118 / 118 | 15,2 → 15,2 | 1,39 → 1,28 (−8 %) | 1,68 → 1,71 | **0,366 → 0,447 (+22 %)** | **0,105 → 0,188 (+79 %)** | 358 → 375 | 0,548 → 0,652 (+19 %) | 16 → 20 / 45 → 55 | 416 → 425 |
| village-night | 118 / 118 | 14,2 → 15,0 | 1,36 → 1,28 | 1,65 → 1,70 | **0,463 → 0,545 (+18 %)** | **0,193 → 0,260 (+35 %)** | 358 → 375 | 0,452 → 0,642 (+42 %) | 23 → 27 / 51 → 59 | 415 → 425 |
| village-swamp | 118 / 119 | 15,2 → 13,8 | 1,76 → 1,75 | 1,71 → 1,72 | **0,327 → 0,502 (+54 %)** | **0,062 → 0,234 (+277 %)** | 383 → 416 | 0,719 → 0,869 (+21 %) | 4 → 18 / 73 → 92 | 441 → 450 |
| minecarts | 118 / 118 | 16,3 → 16,2 | 0,599 → 0,605 | 1,35 → 1,40 | 0,731 → 0,769 (+5 %) | 0,456 → 0,488 (+7 %) | 119 → 121 | 0,396 → 0,458 (+16 %) | 64 / 0 | 325 → 334 |
| horde | 118 / 118 | 16,1 → 14,3 | 1,77 → 1,82 | 1,52 → 1,76 (+15 %) ² | 1,02 → 0,97 | 0,722 → 0,664 | 343 → 344 | 0,824 → 1,359 (+65 %) ³ | 77 → 75 / 143 | 459 → 468 |

¹ Die Showcase-Welt ist in 37 stark umgebaut (Wegfindungs-Station aus 37.1, Dorf-Referenz und Dorf-Stationen aus 37.2/37.3): im Blick des Szenarios 14 statt 25 Entities beim Client, 14 statt 10 Wesen beim Server, 158 statt 153 Draw-Calls, 92 000 statt 76 000 Dreiecke. Die Werte sind nur eingeschränkt vergleichbar. ² Wiederholung mit 6 Runden: 1,53 → 1,62 ms (+6 %, Spannen 1,22–1,71 und 1,29–1,94), Welt-Renderer −6 %, GPU gleich: keine wiederholte Regression. ³ Wiederholung: 0,81 → 0,97 ms (+20 %); Server-Kosten headless (unten).

Frame-Kosten-p99 (GPU) höchstens 2,92 ms; Spitzenspeicher +8 bis +13 MB in jedem Szenario (Inhalt der Dorf-Sektion: Stile, Baupläne, Handelssätze, Bewohner-Modelle). Keine übersprungenen Ticks, Tick max höchstens 5,9 ms (Erkundungsflug).

### Fenster: Erkundungsflug und 16 Chunks

| Szenario (Runden) | Frame p99 ms | GPU Ø ms | Render-CPU ms | GUI-CPU ms | Draw-Calls | Tick Ø / max ms | Spitzenspeicher MB |
|---|---|---|---|---|---|---|---|
| explore, 75 s, `STATS=5000` (3) | 14,6 → 14,5 | 0,517 → 0,517 | 0,672 → 0,720 (+7 %) | 0,317 → 0,353 (+11 %, Spannen bis 21 %) | – | 0,763 → 0,833 / 5,07 → 5,42 | 563 → 568 |
| sprint, 16 Chunks (3) | 9,35 → 9,29 | 3,33 → 3,41 | 2,42 → 2,33 | 0,284 → 0,292 | 1426 → 1426 | 0,732 → 0,855 / 3,4 → 3,3 | 1 232 → 1 244 |
| village, 16 Chunks (3) | 9,27 → 11,4 ⁴ | 2,50 → 2,36 | 2,46 → 2,43 | 0,277 → 0,322 (+16 %) | 1423 → 1440 | 0,624 → 0,738 / 1,8 → 1,8 | 827 → 837 |

⁴ ein Lauf mit 15,6 ms (verpasste VSyncs), die anderen 9,3 ms. Speicherkurve des Erkundungsflugs (`memoryMb` alle 5 s, 15 Punkte): beide Stände steigen bis rund 550–580 MB bei 50 s und fallen danach wieder auf 510–540 MB (Entladen wirkt); `f406734` liegt je Punkt 0–16 MB höher. Explore-Server-Werte: Phase Chunk-Laden max 2,82 → 3,18 ms (+13 %, Dörfer am Flugweg, wie in 37.2a headless), Phase Wesen 0,356 → 0,420 ms.

### Headless: `f406734` gegen `8d714f0` (60 Spielsekunden)

| Szenario (Runden) | Tick Ø ms | Tick p99 ms | Tick max ms | Phase Wesen Ø ms | Wesen | Spitzen-RSS MB |
|---|---|---|---|---|---|---|
| crowd (6) | 0,138 → 0,168 (+22 %) | 0,188 → 0,257 | 0,26 → 0,37 | 0,121 → 0,151 | 123 → 122 | 50,7 → 53,4 |
| stand (3) | 0,086 → 0,103 (+20 %) | 0,125 → 0,177 | 0,25 → 0,26 | 0,078 → 0,094 | 77 → 78 | 50,9 → 53,3 |
| horde (6) | 0,144 → 0,178 (+24 %) | 0,201 → 0,402 | 0,28 → 0,73 | 0,129 → 0,163 | 125 → 121 | 50,7 → 53,5 |
| explore (6) | 0,180 → 0,196 (+9 %) | 0,893 → 0,903 | 1,22 → 1,27 | 0,052 → 0,068 | 56 | 56,5 → 59,7 |
| gravel-collapse / farm / farm-seeds | 0,506 → 0,510 / 0,007 gleich / 0,017 → 0,018 | gleich | gleich | – | 0 | +1,4 bis +2,9 |
| Signal-Stationen (je 6) | 0,028–0,032 → 0,037–0,039 (+22 bis +32 %) | 0,096–0,103 → 0,108–0,117 | – | 0,006–0,009 → 0,014–0,015 | 7–10 → 14 | 45 → 48,6–48,9 |
| village (6) | 0,057 → 0,084 (+47 %) | 0,089 → 0,149 | 0,14 → 0,23 | 0,047 → 0,072 | 52 → 61 | 47,9 → 50,7 |
| village-night (6) | 0,054 → 0,068 (+26 %) | 0,081 → 0,168 | 0,15 → 0,21 | 0,044 → 0,056 | 48 → 58 | 48,0 → 51,3 |
| village-swamp (6) | 0,079 → 0,126 (+59 %) | 0,120 → 0,270 | 0,19 → 0,35 | 0,068 → 0,114 | 71 → 88 | 47,8 → 51,1 |
| minecarts (3) | 0,033 → 0,033 | 0,049 gleich | 0,12 → 0,09 | – | 0 | 43,5 → 44,8 |

Phase Loren headless 0,023 ms bei beiden Ständen (64 rollende Loren).

**Bewertung:**

- **Keine wiederholte Regression über 10 % auf der Client-Seite bei gleichem Bildinhalt.** stand, sprint, crowd, minecarts, explore: GPU, Render-CPU, GUI-CPU, Entity-Vernetzung und Draw-Calls innerhalb von −7 bis +7 % (explore GUI-CPU +11 % bei nur 3 Runden und einer Spanne von 21 % beim alten Stand). horde Render-CPU +15 % in der Hauptreihe, in der Wiederholung +6 % mit überlappenden Spannen, Welt-Renderer und GPU gleich: Streuung. showcase GUI-CPU −13 % und Entity-Vernetzung −31 % folgen aus 14 statt 25 Entities im Blick (umgebaute Showcase), GPU +4 % aus 21 % mehr Dreiecken.
- **Dörfer im Blick kosten GUI-CPU und Entity-Vernetzung, nicht GPU:** village +22 % / +79 %, village-night +18 % / +35 %, village-swamp +54 % / +277 % (absolut höchstens +0,18 ms GUI-CPU und +0,17 ms Vernetzung je Bild). Ursache sind die Wesen des Dorfs (Bewohner, Golem, Tiere der Gehege): 4 → 18, 16 → 20, 23 → 27 Entities beim Client; je Entity bleibt die Vernetzung bei 9–15 µs wie vorher (crowd 5 µs je einfacheres Tier). Häuser, Wege und Felder kosten nichts Messbares: GPU −1 bis −8 % (Häuser verdecken mehr als Bäume und Hügel, 3–5 % weniger Dreiecke), Draw-Calls +5 bis +9 %, Render-CPU +1 bis +3 %. Inhalt der Etappe, keine Regression.
- **Server: Phase Wesen +20 bis +25 % in Mob-Szenen (crowd, stand, horde) ist die Wegfindung aus 37.1**, dort gemessen und angenommen (crowd +0,027 ms headless; hier +0,030 ms, horde p99 und max wie in 37.1). 37.2–37.R3 legen bei gleichem Wesen-Bestand nichts Messbares darauf. Die Signal-Stationen +22 bis +32 % kommen aus 14 statt 7–10 Wesen in der umgebauten Showcase (Phase Wesen, Signal unverändert). Die Dorf-Szenen +26 bis +59 % sind das Dorfleben (9–17 Wesen mehr, Bewohner mit Tagesablauf, Wege, Arbeitsplätzen), absolut höchstens 0,13 ms Ø und 0,35 ms max je Tick. Im Fenster ist Tick Ø entsprechend +16 bis +42 % (Tick-Werte im Fenster streuen mit der Lage des Server-Threads, horde-Wiederholung +20 % statt +65 %). Loren unverändert.
- **Speicher:** +8 bis +13 MB im ganzen Spiel, +1,3 bis +3,9 MB headless.

**Absolute Ziele** (`docs/Performance.md`, „Ziele“): siehe Zielprüfung „Dorf-Sektion (37)“ dort.

## 52.0a (`QcMobManager` aufgeteilt) headless (2026-10-17)

`scripts/bench-headless.sh`, `@server`, 3 verschränkte Runden zu 60 Spielsekunden, Release, `dev` `0034999` (A) gegen den Kandidaten `02939ef` (B, E-422). Mittel in ms (Tick Ø, Tick p99, Phase Wesen Ø):

| Szenario | Tick Ø A → B | Tick p99 A → B | Wesen Ø A → B | Wesen |
|---|---|---|---|---|
| crowd | 0,163 → 0,162 (−0,2 %) | 0,246 → 0,242 | 0,146 → 0,146 | 121–123 |
| gravel-collapse | 0,495 → 0,500 (+1,1 %) | 1,419 → 1,408 | – | 0 |
| farm | 0,007 → 0,007 | 0,013 → 0,013 | – | 0 |
| farm-seeds | 0,017 → 0,017 (+2,0 %) | 0,054 → 0,052 | – | 0 |
| explore | 0,191 → 0,194 (+1,6 %) | 0,882 → 0,896 | 0,066 → 0,067 | 57–60 |
| showcase-signal_components | 0,036 → 0,042 (+14,7 %) | 0,106 → 0,113 | 0,0144 → 0,0195 | 14 |
| showcase-signal_machines | 0,037 → 0,042 (+13,5 %) | 0,121 → 0,114 | 0,0144 → 0,0194 | 14 |
| showcase-signal_farm | 0,040 → 0,037 (−8,3 %) | 0,110 → 0,114 | 0,0178 → 0,0144 | 14 |

**Bewertung:** Keine Regression. Die drei Signal-Stationen der Vorführwelt zeigen je Lauf eine von zwei festen Stufen der Phase Wesen (0,0144 oder 0,0195 ms, 5 µs je Tick): A die hohe bei `signal_farm` (zwei von drei Runden), B bei den beiden anderen (alle Runden); sonst gleich bis auf die Streuung, Spitzenspeicher ±0,6 MB. Dass das Lage und Ausrichtung des Codes sind und nicht anderes Verhalten, zeigt ein Abzug aller Wesen (Id, Art, `uid`, Ruhe, Position, Blickrichtung, Gesundheit) am Ende der Läufe, vorübergehend in `test_scenario_measure` eingebaut: bei allen drei Stationen sind A und B Zeile für Zeile gleich. crowd und explore sind schon zwischen zwei Läufen desselben Stands nicht gleich (Chunks entstehen in Hintergrund-Threads; A gegen A: 116 bis 247 Zeilen Unterschied), dort zählen nur die Mittel.

## 52.0b (Fähigkeiten, KI und Haltung aus Daten) headless (2026-10-17)

`scripts/bench-headless.sh`, `@server`, 3 verschränkte Runden zu 60 Spielsekunden, Release, `dev` `0034999` (A) gegen den Kandidaten `ffd701d` (B, E-423). Mittel in ms (Tick Ø, Tick p99, Phase Wesen Ø):

| Szenario | Tick Ø A → B | Tick p99 A → B | Wesen Ø A → B | Wesen |
|---|---|---|---|---|
| crowd (Serie 1) | 0,165 → 0,188 (+14,0 %) | 0,259 → 0,282 | 0,149 → 0,172 | 121–122 |
| crowd (Serie 2, 6 Runden) | 0,163 → 0,163 (−0,1 %) | – | 0,147 → 0,147 | 121–123 |
| gravel-collapse | 0,503 → 0,505 (+0,5 %) | 1,457 → 1,427 | – | 0 |
| farm | 0,007 → 0,007 | 0,014 → 0,013 | – | 0 |
| farm-seeds | 0,018 → 0,018 | 0,053 → 0,054 | – | 0 |
| explore | 0,193 → 0,193 (+0,2 %) | 0,902 → 0,883 | 0,067 → 0,068 | 56–63 |
| showcase-signal_components | 0,037 → 0,037 (−0,9 %) | 0,110 → 0,109 | 0,0146 → 0,0145 | 14 |
| showcase-signal_machines | 0,036 → 0,036 | 0,108 → 0,108 | 0,0142 → 0,0142 | 14 |
| showcase-signal_farm | 0,037 → 0,037 (+0,9 %) | 0,108 → 0,107 | 0,0144 → 0,0145 | 14 |

**Bewertung:** Keine Regression. In der ersten Serie lagen zwei der drei crowd-Läufe von B bei 0,201 ms (Phase Wesen 0,185 statt 0,146), der dritte gleich A; die Wiederholung nur von crowd mit 6 Runden zeigt A und B gleich (0,1633 gegen 0,1632 ms, jeder Lauf zwischen 0,160 und 0,167 ms). crowd ist schon zwischen zwei Läufen desselben Stands nicht gleich (Chunks in Hintergrund-Threads, 52.0a), die beiden Ausreißer kamen nicht wieder. Das Gehirn je Wesen (ein virtueller Aufruf) und der Kontext je Wesen kosten nichts Messbares; Spitzenspeicher ±0,6 MB.

## 52.0c (Testdörfer, Langzeittest, Grenzen) headless (2026-10-17)

Release, Apple M-Serie, Ticks von Hand (keine Echtzeit), Stand `52.0c` (E-424).

**Grenzen** (`QTCRAFT_VILLAGE_LIMITS_MEASURE=1 build/release/tests/test_village_limits_measure`, 6 s Wanduhr): 32 Testdörfer (Ebene, Wüste, Schnee, Taiga, Sumpf) mit je 32 Bewohnern, 1024 Bewohner (Grenzen je Dorf und Welt), 4 Spieler auf vier Dörfer verteilt, Wesen spawnen, 1123 Chunks geladen. Budget der Wegsuche `pathNodesPerTick` 4096, `pathNodesPerSearch` 512. Wartezeit: Ticks von der Anfrage bis zur Suche.

| Abschnitt (Tagesanteil) | Ticks | Tick Ø / p99 / max ms | Knoten je Tick Ø (Bewohner / andere) | Budget ausgeschöpft | Wartezeit Bewohner Ø (max) | Wartezeit andere Ø (max) |
|---|---|---|---|---|---|---|
| Abend, alle gehen heim (0,55–0,65) | 1200 | 0,53 / 2,28 / 2,51 | 124 (65 / 59), höchstens 4077 | 1,7 % der Ticks | 10,9 (20), 1281 Suchen | 1,02 (10), 2047 Suchen |
| Nacht, Feinde um die Spieler (0,65–0,80) | 1800 | 0,49 / 0,63 / 0,87 | 61 (0 / 61), höchstens 521 | 0 % | – | 1,00 (2), 3212 Suchen |
| Morgen, alle gehen zur Arbeit (0,80–1,10) | 3600 | 1,02 / 2,65 / 2,88 | 324 (265 / 59), höchstens 4096 | 1,7 % | 2,8 (39), 16 033 Suchen | 1,31 (39), 6124 Suchen |

**Bewertung:** Die Bewohner verbrauchen im Mittel 2–7 % des gemeinsamen Budgets; ausgeschöpft ist es nur in den Spitzen beim Aufbruch (Abend und Morgen je etwa 60 Ticks). Dann warten auch Feinde: im Mittel 1,3 Ticks, höchstens 39 Ticks (knapp 2 s) am Morgen, sonst höchstens 10. Kein dauerhaftes Aushungern, aber eine kurze Verzögerung im Morgengedränge. Vorschlag (nicht umgesetzt): ein Anteil des Budgets je Tick für Wesen, die keine Dorfbewohner sind (etwa ein Viertel, 1024 Knoten), oder Suchen der Verfolgung (`QcMobPath::Purpose::Chase`) vor Besorgungen und Wandern in der Warteschlange; Messgröße bleibt die Wartezeit der anderen in diesem Test.

**Langzeittest** (`QTCRAFT_VILLAGE_SOAK_DAYS=3 build/release/tests/test_village_soak`): vier Testdörfer (Ebene, Wüste, Sumpf auf Stelzen, Schnee), 39 Bewohner, 3 Spieltage (35 996 Ticks) in 1,3 s Wanduhr, Tick Ø 0,037 ms; jede Mitternacht 39 von 39 im Bett, 4 Kinder geboren, Ansprüche nach Speichern und Öffnen in der Mitte gleich. Debug: 18 s; der CI-Lauf ohne Variable (1 Tag) 7 s.

## 52.0 nach den Nachbesserungen headless (2026-10-17)

`scripts/bench-headless.sh`, `@server`, 3 verschränkte Runden zu 60 Spielsekunden, Release, `dev` `0034999` gegen den Kandidaten nach den Nachbesserungen der Prüfung (E-422 bis E-424). Tick im Mittel −2,2 bis 0 %, p99 −8,4 bis +4,4 %, Spitzenspeicher ±0,5 MB. Keine Regression; der Umbau der Wesen-Architektur kostet nichts Messbares.

## 52.1 (Geschosse, Bogen und Pfeile) headless (2026-10-17)

`QTCRAFT_PROJECTILE_MEASURE=full build/release/tests/test_projectile_measure`, Release, Ticks von Hand so schnell der Server kann (E-425). 500 Pfeile mit 30 m/s steil nach oben (55–85°) aus 40 m über der Bühne, alle in geladenen Chunks (11 × 11 Chunks), 100 Schweine und ein Spieler als Körper; dann 500 Pfeile steckend in einer Steindecke. Je 60 Ticks, drei Läufe:

| Szene | Phase `projectiles` Ø ms | Phase max ms | Tick Ø ms |
|---|---|---|---|
| 500 im Flug | 0,119–0,122 | 0,143–0,171 | 0,379–0,389 |
| 500 steckend | 0,0085–0,0087 | 0,011–0,015 | 0,192–0,196 |

**Bewertung:** Etwa 0,24 µs je fliegendem Pfeil und Tick (Raycast der 1,5-m-Strecke und Breitphase über 101 Körper), 0,02 µs je steckendem (ein Blick auf seinen Block). 500 fliegende Pfeile kosten 0,25 % eines 50-ms-Ticks; die Grenze von 1024 Geschossen hält auch eine Schießbude im Rahmen. Keine Messung gegen `dev`: die Phase ist neu, die anderen Phasen sind unverändert (ohne Geschosse kehrt sie nach dem Abholen der Schüsse der Wesen sofort zurück).

## 52.1b (Skelett-Bogenschützen) headless (2026-10-17)

`QTCRAFT_PROJECTILE_MEASURE=full build/release/tests/test_projectile_measure skeletonsShootAtAPlayer`, Release, Ticks von Hand so schnell der Server kann (E-426). 50 Skelette im Ring 7–11 m um einen Spieler auf einer freien Fläche bei Nacht, alle sehen ihn; der Spieler wird jeden Tick geheilt, damit geschossen wird. 200 Ticks (10 s Spielzeit), drei Läufe, Last 9–10 (andere Sitzungen):

| Phase | Ø ms | max ms |
|---|---|---|
| Tick | 0,089–0,092 | 0,31–0,41 |
| `mobs` (Gehirne, Sichtlinie, Zielen, Bewegung, Brennen) | 0,072–0,073 | – |
| `projectiles` | 0,0042–0,0043 | – |

172 Pfeile in 10 s (je Skelett etwa einer in 2 s nach dem ersten Blick; Pfeile, die ein anderes Skelett im Ring treffen, zählen mit).

**Bewertung:** Etwa 1,5 µs je Skelett und Tick in der Phase der Wesen (Sichtlinie jeden Denk-Tick, Zielen nur beim Schuss) und 0,2 % eines 50-ms-Ticks für alle 50. Die Obergrenze der feindlichen Wesen je Spieler (`hostileCap`) liegt weit darunter. Keine Messung gegen `dev`: das Gehirn ist neu, die anderen Wesen sind unverändert.

## 37.R2b (Block-Entities als versionierte Tabellen) headless (2026-10-17)

`QTCRAFT_BLOCK_ENTITY_MEASURE=full build/release/tests/test_block_entity_measure` (RelWithDebInfo, Apple M4 Max; 441 Chunks mit je 5 gefüllten Truhen, 3 brennenden Öfen, 3 Trichtern unter Truhen und 3 Setzlingen, 200 Ticks, danach erstes Speichern, ein Tick, zweites Speichern, Öffnen, alle Chunks laden), Kandidat 52.1 `27220fe` (A, per `git archive` außerhalb von git gebaut) gegen den Branch (B), 10 verschränkte Runden (4 × A zuerst, 6 × B zuerst), Mediane in ms [Minimum] (E-440). Parallel liefen Builds und Tests anderer Sitzungen; die Streuung je Wert liegt bei ±20 %.

| Wert | A | B | Änderung |
|---|---|---|---|
| Tick | 0,98 | 1,02 | +4 % (keine Änderung im Tick) |
| erstes Speichern | 44,56 [41,4] | 44,25 [41,4] | −1 % |
| zweites Speichern | 16,83 [14,74] | 18,21 [14,86] | +8 % [+1 %] |
| Öffnen | 4,62 [3,23] | 4,88 [3,35] | +6 % [+4 %] |
| alle Chunks laden | 185,1 | 185,9 | +0 % |

**Bewertung:** Im Ziel (≤ +10 %); die Minima liegen beim zweiten Speichern und Öffnen 1–4 % auseinander, die Mediane im Rauschen der belasteten Maschine. Die Mehrarbeit sind 6 Bytes Rahmen je Eintrag (Version und Länge, Länge danach in den Puffer eingesetzt wie in `QcComponentFrames::Writer`) und 8 Bytes je nicht leerer Tabelle; leere Tabellen werden weggelassen. Ohne behaltene Teile kostet die Suche nach Schwänzen je Eintrag nur die Prüfung einer leeren Tabelle (`QcBlockEntityKept::entryTail`), `pruneKept` kehrt sofort zurück.

## 52.1 (Geschosse, Skelette, Munition, 52.P, HUD) gerendert und headless (2026-10-18)

Release-Builds, `dev` `f249fc8` gegen den Kandidaten `feature/52-1-projectiles` (52.1a–c samt Nachbesserungen, dazu 52.P und HUD/Trichter), Fenster auf dem eingebauten Display, verschränkt, 10 s je Lauf, Mediane. Last: andere Sitzungen bauten zeitweise (clangd-Index bis 400 % CPU, Tests anderer Pakete); Läufe mit Fremdlast sind markiert und auf beide Stände verteilt.

**Headless** (`@server`, 3 Runden, 60 Spielsekunden): crowd Tick 0,177 → 0,158 ms (−11 %, aber 107 statt 123 Wesen: die Zombies der Tag-Szene verbrennen seit 52.1b), gravel-collapse, farm-seeds, explore unverändert, farm 0,007 → 0,008 ms (Rauschen), Signal-Stationen 0 bis +5 % (ein Wesen mehr: Skelett-Gehege der Showcase), Speicher +0 bis +1,5 MB. Nachts (`SCENE_TIME=night`, 6 Runden): horde 0,181 → 0,182 ms, crowd 0,165 → 0,167 ms bei gleicher Wesenzahl.

**Fenster** (`@core horde village`, 6 Runden): stand, sprint, showcase, village bei GPU, Render-CPU, GUI-CPU und Speicher innerhalb ±4 %; p99 streut wie bisher mit VSync-Aussetzern von außen (sprint +25 %, horde −34 %). Auffällig bei Tag: crowd GPU 2,18 → 2,58 ms (+18 %, Spannen getrennt) und GUI-CPU +8 %, horde GUI-CPU +12 %. **Ursache:** die Zombies der Tag-Szenen brennen jetzt (Feuer an Körpern, E-393); bei Nacht (`SCENE_TIME=night`, 4 Runden) crowd GPU 2,19 → 2,20 ms (+0 %), GUI −2 %; horde GPU +3 %, GUI +9 % und Render-CPU +13 % mit überlappenden Spannen bei Fremdlast (Tests anderer Pakete). Inhalt der Etappe, keine Regression des Codes.

**Ziele:** Frame-Kosten höchstens 2,58 ms (crowd, Tag) von 4 ms, Spitzenspeicher 8 Chunks höchstens 573 MB (sprint 10 s) erfüllt; p99 wie bisher von außen verfehlt. `crowd` und `horde` nehmen die Tageszeit jetzt aus `SCENE_TIME` (Standard Tag).

## Zweiter Zug: 37.R2b, 52.H, 54 gerendert und headless (2026-10-18)

Release-Builds, `dev` `30f30ea` (52.1) gegen den Zug `feature/37-r2b-block-entity-components` (Block-Entities als versionierte Tabellen, einstellbare Unverwundbarkeit, rutschige Blöcke und Loren-Schwung), verschränkt, Mediane. Ruhiger Desktop in der Nacht.

**Headless** (`@server minecarts horde village`, 3 Runden, 60 Spielsekunden): Tick −9 bis 0 % in allen Szenarien (crowd 0,157 → 0,150, horde 0,140 → 0,128, village 0,084 → 0,083, minecarts 0,033 → 0,033 ms); Phase Loren in den Signal-Stationen 0,0021 → 0,0027 ms (Loren der neuen Station `ice_slide`); p99 ±10 % (absolut höchstens +0,04 ms); Spitzenspeicher −1 bis +2 %.

**Fenster** (`@core minecarts village`, 8 Chunks, 6 Runden, 10 s): GPU −8 bis +1 %, Render-CPU −1 bis +7 % (showcase, 12 statt 14 Entities im Blick durch die neue Station), GUI-CPU −4 bis +5 %, Spitzenspeicher unverändert; p99 10,24–10,41 ms bei beiden Ständen (Ziel ≤ 12,5 ms erfüllt). Tick im Fenster streut wie immer (stand +28 %, minecarts −51 %, village −24 %); headless gleich. Keine Regression.

## 52.3 (Endermen) headless, Debug-relativ (2026-10-18)

`QTCRAFT_ENDERMAN_MEASURE=full build/debug/tests/test_endermen thirtyEndermenMeasure`, **Debug-Build** (Release-Builds waren während der parallelen Pakete nicht erlaubt), Ticks von Hand so schnell der Server kann (E-460). 30 Endermen im Ring 6–12 m um einen Spieler auf einer freien Grasfläche bei Nacht, der Spieler blickt jeden Tick auf den Kopf des ersten (einer ist wütend und greift an), `mobGriefing` an mit zehnfachen Chancen zum Aufnehmen (0,5/s) und Absetzen (0,05/s): nach 20 s tragen 25 einen Block. 400 Ticks (20 s Spielzeit), verschränkt mit der Debug-Messung der Skelette als Bezug, Last 23–36 (andere Sitzungen):

| Messung (Debug) | Tick Ø ms | Tick max ms | `mobs` Ø ms | `mobs` je Wesen µs |
|---|---|---|---|---|
| 30 Endermen (4 Läufe) | 1,54–2,39 | 6,7–27 | 1,36–2,09 | 45–70 |
| 50 Skelette, `skeletonsShootAtAPlayer` (2 Läufe) | 3,01–3,48 | 30–31 | 2,72–3,06 | 54–61 |

**Bewertung:** Je Wesen kostet ein Enderman in der Phase der Wesen im Debug-Build etwa so viel wie ein Skelett (45–70 gegen 54–61 µs; Blick-Prüfung nur im Blick-Takt, Teleport- und Block-Suche nur bei Bedarf). Das Skelett braucht im Release 1,5 µs je Wesen und Tick (52.1b), der Faktor Debug/Release liegt dort bei etwa 40; für 30 Endermen also grob 0,04 ms je Tick im Release, unter 0,1 % eines 50-ms-Ticks. Die Spitzen (bis 27 ms) fallen mit der Fremdlast zusammen. Eine Release-Messung steht aus.

## 52.3 (Enderman, Enderperle) gerendert und headless (2026-10-18)

Release-Builds, `dev` `7d71ee6` gegen `feature/52-3-enderman` nach der Nachbesserung, verschränkt, Mediane; andere Pakete bauten zeitweise (Läufe markiert).

**Headless** (`@server horde village`, 3 Runden, 60 Spielsekunden): Tick −1 bis +8 % (Signal-Stationen +1 Wesen: Enderman-Gehege der Showcase; village 59 → 67 Wesen bei +1 % Tick, Endermen in der Nacht-Spawnliste), p99 −5 bis +6 % (horde +19 %, absolut +0,08 ms bei gleicher Wesenzahl), Speicher ±1 %.

**Fenster** (`@core horde`, 8 Chunks, 6 Runden): GPU −5 bis +2 %, Render-CPU −8 bis +4 % (sprint +19 %) und GUI-CPU −12 bis +2 % (sprint +24 %) mit überlappenden Spannen und gleicher Entity-Zahl, Speicher +0–1 %, p99 10,0–10,4 ms (Ziel erfüllt); Tick im Fenster streut wie immer. Keine wiederholte Regression.

## 52.6 (Eisengolem, Golems aus Panik und Tratsch) headless, Debug-relativ (2026-10-18)

`QTCRAFT_GOLEM_MEASURE=2 build/debug/tests/test_golem_measure` (Debug, Apple M4 Max, `nice -n 10`, andere Sitzungen bauten parallel: Lastmittel 4–14). Ein Testdorf im Ebenen-Stil mit jedem Haus doppelt (32 Bewohner) auf einem Server ohne Spieler, zwei Spieltage ab dem Abend, Ticks von Hand so schnell wie möglich; abwechselnd mit `villageGolems` 0 (keine Golems) und 256, je 3 Runden, Mediane.

| Wert | ohne Golems | mit Golems |
|---|---|---|
| Tick (ms, Debug) | 0,545 | 0,652 |
| Golems nach 2 Tagen | 0 | 6 |
| Wesen am Ende | 37 | 43 |

**Bewertung:** Die Bewohner rufen nach Schlaf und Arbeit von selbst Golems (der erste am ersten Mittag beim Treffen an der Glocke), danach einen je Ecke des Dorfs, in der kein Golem näher als 16 Blöcke steht. Die Mehrkosten (+0,1 ms je Tick im Debug-Build, +20 %) sind die sechs Golems selbst (Gehirn mit Blick über alle Wesen alle 10 Ticks, Wege, Physik eines breiten Körpers), etwa 0,018 ms je Golem; die Prüfung des Rufens alle 100 Ticks fällt darin nicht auf. Debug-relativ, keine Release-Messung (paralleler Betrieb); bei vielen Golems ist der Blick über alle Wesen (`QcGolemBrain::prey`) der erste Kandidat für einen räumlichen Index.

## 52.6 (Eisengolem, mit 52.3 zusammengeführt) gerendert und headless (2026-10-18)

Release-Builds, `dev` `506cab9` (52.3) gegen `feature/52-6-iron-golem` nach Nachbesserung und Zusammenführung, verschränkt, Mediane.

**Headless** (`@server horde village village-night`, 3 Runden, 60 Spielsekunden): crowd, horde, village, village-night, explore, farm −5 bis +4 % bei gleicher Wesenzahl; Signal-Stationen +20 bis +26 % (0,040 → 0,049 ms) bei 22 statt 16 Wesen: Dorfbewohner, Zombie und Golems der neuen Stationen `golem_farm` und `golem_workshop`, etwa 1,5 µs je Wesen; Speicher 0 bis +3 %.

**Fenster** (`@core village`, 8 Chunks, 6 Runden): GPU −2 bis +4 %, Render-CPU −6 bis +6 %, GUI-CPU −7 bis +0 %; sprint Render-CPU +14 % und GUI-CPU +20 % mit überlappenden Spannen und gleicher Entity-Zahl (wie in der Messung von 52.3: das Szenario streut); showcase 16 statt 12 Entities im Blick bei GPU +4 %; p99 10,3–10,4 ms, Speicher +0–1 %. Keine Regression.

## 52.2 (Spinnen klettern, Vergleich mit Zombies) headless, Debug (2026-10-18)

`QTCRAFT_SPIDER_MEASURE=full build/debug/tests/test_spiders climbingSpidersMeasure`, **Debug** (Release-Zahlen kommen zentral), Ticks von Hand so schnell der Server kann. Ein Spieler auf einem 3 × 3 Steinturm, vier Blöcke hoch, bei Nacht; 50 Wesen im Ring 6–10 m um ihn, 400 Ticks (20 s Spielzeit), der Spieler wird jeden Tick geheilt. Spinnen klettern den Turm hinauf und jagen ihn (alle 50 oben), Zombies können nicht klettern und sammeln sich am Fuß (keiner oben). Drei Läufe, Rechner stark belastet (Last 55–87 durch andere Sitzungen und ein volles Verify), daher nur das Verhältnis aussagekräftig:

| Art | Tick Ø ms | Tick max ms | `mobs` Ø ms |
|---|---|---|---|
| Spinne (klettert, jagt) | 5,8–6,4 | 51–77 | 5,5–6,0 |
| Zombie (erreicht ihn nicht) | 8,7–9,0 | 70–120 | 8,4–8,6 |

**Bewertung:** Kletternde Spinnen kosten in der Phase der Wesen rund 30 % weniger als Zombies, die vor dem Turm keinen Weg finden und ihn immer wieder suchen; das Klettern (Fühl-Kasten an der Wand je Tick, Wege mit `climbsWalls`) ist also kein neuer Engpass. Absolut in Debug unter Last etwa 0,12 ms je Spinne und Tick.

## 52.2 (Status-Effekte, Spinnen, Hühner, Silberfische; mit dev zusammengeführt) gerendert und headless (2026-10-18)

Release-Builds, `dev` `4063175` (52.6) gegen `feature/52-2-spiders` nach dem Zusammenführen, verschränkt, Mediane.

**Headless** (`@server horde village`, 3 Runden, 60 Spielsekunden): crowd, horde, village, explore −5 bis 0 %, gravel-collapse und Felder ±0 %; Signal-Stationen +26 bis +33 % (0,046 → 0,058 ms) bei 36 statt 22 Wesen: Spinnenhöhle, Silberfisch-Wand und Hühnerstall der Showcase, etwa 0,9 µs je Wesen; Speicher +1 bis +2 %.

**Fenster** (`@core horde`, 8 Chunks, 6 Runden): GPU −8 bis +2 %, Render-CPU −10 bis +2 %, GUI-CPU −4 bis +5 %, Speicher ±0 %; showcase 18 statt 16 Entities im Blick (neue Gehege) bei GPU −3 %. p99 bei beiden Ständen 15,8–16,8 ms (Desktop am Morgen belebt, verpasste VSyncs von außen wie früher). Keine Regression.

## 52.5 (Zähmen: Wölfe, Hunde, Katzen, Ozelots) headless, Debug-relativ (2026-10-18)

`test_pet_rules`, Fall `twentyDogsFollowAMovingPlayer` (E-480): ebene Arena, ein Spieler läuft ein Quadrat von 16 m mit 4 m/s ab, Ticks von Hand (400 Ticks je Teil), erst allein, dann mit 20 Hunden, die folgen (Wegsuche zum Besitzer, Teleport ab 12 m). Debug-Build, drei Läufe, andere Pakete bauten zeitweise.

| Messung (Debug) | Tick Ø ms allein | Tick Ø ms mit 20 Hunden | je Hund und Tick µs |
|---|---|---|---|
| 3 Läufe | 0,145–0,148 | 0,486–0,493 | 17 |

**Bewertung:** Ein folgender Hund kostet im Debug-Build etwa 17 µs je Tick, ein Drittel eines Endermans oder Skeletts (45–70 µs, 52.3); alle 20 bleiben in der Teleport-Entfernung. Kein Handlungsbedarf; die Release-Messung (`@server`) gehört zum Abschluss der Etappe.

## 52.5 (Zähmen, mit dev zusammengeführt) gerendert und headless (2026-10-18)

Release-Builds, `dev` `3599eb9` (52.2) gegen `feature/52-5-taming` nach dem Zusammenführen, verschränkt, Mediane.

**Headless** (`@server horde village`, 3 Runden, 60 Spielsekunden): crowd, horde, village, gravel-collapse, Felder −2 bis +6 %; explore unverändert bei 65 statt 57 Wesen (Wölfe in Wäldern); Signal-Stationen +11 bis +18 % (0,063 → 0,071 ms) bei 45 statt 36 Wesen: Haustier-Ecke der Showcase, etwa 0,9 µs je Wesen; Speicher −1 bis +3 %.

**Fenster** (`@core village`, 8 Chunks, 6 Runden): GPU −3 bis +2 %, Render-CPU −5 bis +5 %, GUI-CPU −1 bis +3 %, Bild Ø 8,38–8,49 ms, Speicher +0–2 %; p99 16,0–16,7 ms bei beiden Ständen (Desktop am Tag belebt), showcase 10,4 ms. Keine Regression.

## 52.7 (Dorf-Spielweisen, Wegbudget-Anteil) headless, Debug-relativ (2026-10-18)

Verschränkte A/B-Messung im Debug-Build, je zwei Runden, `nice -n 10`, Ticks von Hand: Basis `dev` `3599eb9` (Haupt-Checkout) gegen `feature/52-7-village-gameplay`. Keine Release-Messung (paralleles Paket).

**Langzeittest über 2 Spieltage** (`QTCRAFT_VILLAGE_SOAK_DAYS=2 test_village_soak`: vier Testdörfer, 39 Bewohner, 23 996 Ticks, 4 Kinder, alle 39 im Bett, Invarianten grün): Tick Ø Basis 0,612 / 0,608 ms, 52.7 0,617 / 0,610 ms; Wanduhr 14,6–14,8 s auf beiden Seiten. Umzug und Abgleich alle 100 Ticks kosten nichts Messbares.

**Großes Dorfgebiet** (`QTCRAFT_VILLAGE_LIMITS_MEASURE=1 test_village_limits_measure`: 32 Testdörfer, 1024 Bewohner, rund 280 andere Wesen, 4 Spieler, 1123 Chunks; Abend, Nacht und Morgen bis Tagesanteil 1,10):

| Abschnitt | Tick Ø ms Basis → 52.7 | Wartezeit andere Ø (max) Ticks Basis → 52.7 | Wartezeit Bewohner Ø (max) Ticks Basis → 52.7 | Budget ausgeschöpft |
|---|---|---|---|---|
| Abend (0,55–0,65) | 7,72 / 7,93 → 7,98 / 7,70 | 1,02 (10) → 1,00 (2) | 10,81 (20) → 8,55 (20) | 1,7 % → 1,4 % |
| Nacht (0,65–0,80) | 7,55 / 7,71 → 7,73 / 7,26 | 1,00 (2) → 1,00 (2) | 1,71 (2) → 1,79 (2) | 0 % → 0 % |
| Morgen (0,80–1,10) | 15,47 / 15,11 → 16,12 / 14,81 | 1,38 (40) → 1,00 (2) | 2,90 (48) → 2,57 (39) | 1,8 % → 0,5 % |

**Bewertung:** Mit einem Viertel des Wegbudgets für andere Wesen (E-490) warten Feinde und Tiere auch im Morgengedränge höchstens 2 Ticks statt 40; die Bewohner selbst warten nicht länger (im Mittel sogar kürzer, weil das Budget seltener ganz ausgeschöpft ist). Tick-Zeiten gleich im Rahmen der Streuung (zwei Runden je Seite).

## 52.7 (Dorf-Spielweisen, mit dev zusammengeführt) gerendert und headless (2026-10-18)

Release-Builds, `dev` `f88fbd7` (52.5) gegen `feature/52-7-village-gameplay` nach dem Zusammenführen, verschränkt, Mediane.

**Headless** (`@server horde village village-night village-swamp`, 3 Runden, 60 Spielsekunden): Tick −2 bis +2 % in allen Szenarien bei gleicher Wesenzahl, p99 −7 bis +6 %, Speicher −2 bis +4 %. Siedlungen, Umzug, Ernte auf beliebigen Feldern und Aufsammeln von Essen kosten nichts Messbares.

**Fenster** (`@core village`, 8 Chunks, 6 Runden): GPU −3 bis +4 %, Render-CPU −8 bis +4 %, GUI-CPU −4 bis +4 %, Bild Ø 8,39–8,47 ms, Speicher ±1 %; p99 10,4–16,2 ms bei beiden Ständen (Desktop am Tag). Keine Regression.
