# Performance

Auswertung der Etappe 16 (Profiling und Optimierung). Messrechner: MacBook Pro M4 Max, macOS 26, eingebautes Display 120 Hz (ab der Schlussmessung zusätzlich zwei externe Displays mit 60 Hz angeschlossen, siehe unten). Alle Werte aus RelWithDebInfo-Builds. Der Verlauf pro Etappe steht in [Performance-Verlauf.md](Performance-Verlauf.md), die Entscheidungen in E-068 bis E-072.

## Messaufbau

| Werkzeug | Aufruf | Liefert |
|----------|--------|---------|
| Benchmark-Pfad | `QTCRAFT_BENCHMARK=<s>`, `QTCRAFT_BENCHMARK_SCENARIO=…` | `BENCH`-Zeile: Frame-Abstand Ø/p99, Server-Tick Ø/max, Spitzenspeicher, **Frame-Kosten unabhängig von VSync** (E-068): CPU-Zeit pro Frame von GUI- und Render-Thread (Thread-CPU-Uhr), Welt-Renderer, Upload (Zeit, MB), Entity-Vernetzung, GPU-Zeit (RHI-Zeitstempel, `QSG_RHI_PROFILE=1`), Meshing pro Sektion (Worker), Draw-Calls, sichtbare Sektionen, Dreiecke, Sektionen und ihre Pufferbytes; am Ende `server_mobs`, `server_items` (Kreaturen und Item-Stapel auf dem Server), `inventory_items` (Items im Inventar), E-332, `server_block_entities` (Behälter, Maschinen, Setzlinge) und `block_entity_problems` (verletzte Regeln, `QcBlockEntityChecks`, E-357), `entity_problems` (verletzte Regeln des Entity-Registers, `QcEntityChecks`, E-364) |
| Paketwechsel im Benchmark | `QTCRAFT_BENCHMARK_PACK_SWITCH=<ids>` (zusätzlich zu `QTCRAFT_BENCHMARK`; in den Skripten `PACK_SWITCH=<ids>`) | Schaltet die Texturpakete nach einem Drittel des Laufs ein (vor die Pakete des Laufs, als eine Auswahl, ein Atlasbau) und nach zwei Dritteln wieder zurück; die gespeicherte Auswahl des Spielers bleibt unberührt (`selectForRun`, nichts wird in `settings.ini` geschrieben); Logzeilen `PACKSWITCH on` und `off`: Atlas fertig nach ms (`atlas_ms`: vom Wechsel bis nach `atlasBuilt`, also Auflisten, Laden und Bauen im Hintergrund und das Übernehmen in `applyAtlas` im GUI-Thread, das vor dem Messhaken an `atlasBuilt` hängt; dessen Anteil steht getrennt in `Texture atlas applied in … ms`), längster Frame, Frames ab 50 ms, längste GUI-/Render-/Weltrenderzeit pro Frame bis 1 s nach dem Atlas, dazu `Texture atlas applied in … ms` (E-332) |
| `scripts/bench.sh` | `[runs] [seconds]`, `SCENARIOS=…`, `VIEW=…`, `KEYS=…` | Median über mehrere Läufe (Etappen-Check) |
| `scripts/bench-ab.sh` | `<Ergebnisdatei> <Runden> <Sekunden> <Name>=<App>[:<VAR>=<Wert>…] …`, `SCENARIOS=…`, `VIEW=…` | Verschränkte A/B-Messung beliebig vieler Builds (pro Runde und Szenario jeder Build einmal, Reihenfolge rotiert pro Runde, E-331); `:<VAR>=<Wert>` hinter der App setzt `TEXTURE_PACKS`, `PACK_SWITCH`, `WORLD_TYPE`, `DEBUG_DISPLAY` oder `PREVIEW_ACTIVE` nur für diesen Namen (steht als `settings=` in jeder Ergebniszeile; `--check` prüft nur die Argumente) (z. B. ein Build unter mehreren Namen mit verschiedenen Texturpaketen: `hd64=<App>:TEXTURE_PACKS=builtin:qtcraft_hd_64`), hält die Sperre `~/.cache/qtcraft/bench.lock`, wartet vor jedem Lauf bis zu 3 min auf einen ruhigen Rechner, schreibt Kopfzeile pro Reihe sowie Position, Reihenfolge, Last und fremde Prozesse vor und nach jedem Lauf mit |
| `scripts/profile.sh` | `<tool> [scenario] [seconds] [view]`, `PROFILE_APP=…` | Preset `profile` (`build/profile`, RelWithDebInfo, QML-Debugging) oder ein vorhandener Build (`PROFILE_APP`, z. B. ein älterer Stand zum Vergleich); Rohdaten in einem Temp-Verzeichnis außerhalb des Repos |
| `profile.sh breakdown` | alle Szenarien | Frame-Kosten-Tabelle wie unten |
| `profile.sh time` | `xctrace` Time Profiler (angehängt) | `scripts/profile-report.py time`: heißeste Funktionen pro Thread, selbst und inklusive |
| `profile.sh metal` | `xctrace` Metal System Trace | GPU-Intervalle pro Frame (Instruments) |
| `profile.sh alloc` | `heap`, `vmmap --summary` nach 10 s | Heap nach Typ, Speicherregionen, physischer Footprint |
| `profile.sh leaks` | `leaks --atExit` mit `MallocStackLogging` | Lecks beim Beenden |
| `profile.sh sample` | `sample` 10 s | Aufrufbäume aller Threads |
| `profile.sh qml` | `qmlprofiler` | `profile-report.py qml`: teuerste Bindungen, Signale, Erzeugungen |
| `profile.sh sg` | `QSG_RENDER_TIMING=1`, `QT_LOGGING_RULES=qt.scenegraph.time.*=true` | Szenengraph-Zeiten pro Frame |
| `profile.sh visualize` | `QSG_VISUALIZE=overdraw/batches/clip/changes` | Screenshots mit geöffnetem Inventar |
| `scripts/perf-check.sh` | ohne Argumente | Regressionsschutz mit festen Grenzen (siehe „Ziele“) |

Szenarien (`scripts/bench-common.sh`): **stand** (Stehen am Spawn), **walk** (Gehen geradeaus), **sprint** (Sprinten und Springen, Chunk-Streaming), **crowd** (56 Mobs und 24 Items), **inventory**, **furnace** (gefüllter Ofen, Blick nach unten), **handbook** (Handbuchseite Stein), **light** (Fackeln erscheinen/verschwinden), **liquids** (Wasser- und Lavaquellen fluten die Fläche), **glass** (nachts auf geräumtem Holzboden zwischen drei Wänden aus Scheiben, Glas, Laub und Eis mit acht Lampen; zweimal pro Sekunde erscheinen oder verschwinden Lichtquellen-Reihen und 3 × 3-Flächen durchsichtiger Blöcke: Sortierung durchscheinender Flächen, Cutout-Meshing, Blocklicht, Neuvernetzung; nur Blöcke von vor der Block-Erweiterung, also auch auf älteren Ständen messbar), **glass-full** (dasselbe mit den Blöcken der Block-Erweiterung: gefärbtes Glas und Scheiben, Eisengitter, Glühwürmchenglas, Laternen, kleine Fackeln, Netze; nur neuere Stände, ältere lehnen unbekannte Blöcke mit einer Hilfezeile ab), **showcase** (seit Etappe 21: Stehen in der Showcase-Welt, `--showcase --spawn 40,-19 --look 345,-8`, zwischen der ersten und zweiten Reihe mit Blick über die Glasgehege und die Podeste der Flüssigkeiten: ein dauerhaftes Wesen jeder Art (im Grundspiel sechs), Hunderte Beschriftungen in Reichweite (bis 48 pro Frame gezeichnet, bis 48 Sichtstrahlen), viel Glas und Teilformen; die Welt hängt nicht vom Seed ab; nur Stände ab Etappe 21), **armour-crowd** (seit Etappe 23: flat-crowd in der flachen Welt plus 40 Rüstungsständer mit vollem Diamantsatz und Umhang und 8 Gliederpuppen mit Umhang: Anbauteile, Umhänge, Abgleich der Ständer; E-242), **armour-walk** (seit Etappe 23: walk mit dem eigenen Spieler in Diamantrüstung mit Umhang, Kamera hinter dem Spieler). **Welttypen (E-223):** Jedes Szenario läuft mit `flat-` davor (z. B. `flat-crowd`, `flat-liquids`, `flat-light`, `flat-glass`) in einer flachen Welt (Gras auf y 63, keine Bäume, Höhlen oder Wesen von selbst) bzw. mit `void-` in der leeren Welt (5 × 5-Plattform, Mittag; nur für Szenarien, die ihren Boden selbst bauen); `WORLD_TYPE=flat|void` gilt für alle Szenarien eines Laufs. So verschieben Änderungen der Weltgenerierung (wie E-101, E-214, siehe E-218) die Vergleiche nicht mehr; `bench.sh` misst `flat-crowd` und `flat-liquids` standardmäßig mit. Die bisherigen Szenarien bleiben im normalen Gelände, damit die Reihe in `Performance-Verlauf.md` weiter vergleichbar ist. Chat-Befehle waren in Etappe 20 auch für den lokalen Spieler auf 2 pro Sekunde (Stoß 20) begrenzt (light verlor rund 19 von 90 Fackel-Befehlen, crowd einen Startbefehl); seit dem Nachtrag zu E-091 ist der lokale Spieler davon ausgenommen. Die glass-Szenarien bleiben unter der alten Grenze. Seed 42, Blick 45°/−10°, Ton gemischt, aber stumm.

Erkenntnisse zu den Werkzeugen:

- VSync lässt sich auf macOS nicht abschalten: `QSG_NO_VSYNC=1` (auch mit `QSG_RENDER_LOOP=basic`) meldet „attempting to disable vsync“, die Frames bleiben aber bei 120 Hz, weil der Display-Link die Update-Anforderungen taktet. Deshalb misst der Benchmark CPU- und GPU-Zeit pro Frame (E-068). Das Szenengraph-Log zeigt im Render-Schritt 5–9 ms: Das ist überwiegend Warten auf das nächste Drawable, keine Arbeit.
- `xctrace record --launch` startete über die Bundle-Kennung eine andere Build-Variante desselben Bundles (ASan-Build) und ohne die Umgebungsvariablen; `profile.sh` startet das Spiel selbst und hängt `xctrace --attach` an. Das Template „Allocations“ beendet auf diesem Rechner weder angehängt noch gestartet (nach 400 s abgebrochen); `profile.sh alloc` nutzt `heap` und `vmmap`.
- Qt-Frameworks werden im Time Profiler nicht symbolisiert (nur Adressen); die eigenen Funktionen und die Systembibliotheken schon.
- Der Frame-Abstand (p99) hängt am Display und an der Tageszeit der Messung (Zweiteilung 9,1/10,3 ms seit Etappe 12); er wird nur verschränkt (A/B abwechselnd) verglichen.

## Erste Messung: Frame-Kosten (vor den Optimierungen)

Stand 562ecc8 (Etappe 15 plus Messpunkte), je ein Lauf à 15 s, 120-Hz-Display. Frame-Abstand in allen Szenarien Ø 8,33–8,35 ms (VSync), p99 10,0–10,3 ms. Zeiten in ms pro Frame (Mittel), Speicher in MB.

| Szenario | Chunks | GUI-CPU | Render-CPU | davon Welt-Renderer | GPU | Entity-Vernetzung | Meshing pro Sektion (Jobs) | Tick Ø / max | Spitzenspeicher | Sektionspuffer | Draw-Calls | Dreiecke |
|----------|-------:|--------:|-----------:|--------------------:|----:|------------------:|---------------------------:|-------------:|----------------:|---------------:|-----------:|---------:|
| stand | 8 | 0,56 | 1,74 | 0,90 | 0,87 | 0,20 | – | 0,28 / 2,6 | 317 | 61,3 | 274 | 227 000 |
| walk | 8 | 0,55 | 1,73 | 0,82 | 0,98 | 0,16 | 1,24 (85) | 0,25 / 1,5 | 382 | 64,8 | 303 | 263 000 |
| sprint | 8 | 0,60 | 1,83 | 0,87 | 0,95 | 0,15 | 1,27 (620) | 0,31 / 4,8 | 373 | 79,2 | 295 | 238 000 |
| crowd | 8 | 1,31 | 1,42 | 1,08 | 1,30 | 1,00 | – | 0,53 / 0,9 | 321 | 61,3 | 273 | 231 000 |
| inventory | 8 | 0,49 | 1,78 | 0,76 | 0,97 | 0,18 | – | 0,26 / 1,7 | 319 | 61,3 | 274 | 227 000 |
| furnace | 8 | 0,64 (p99 2,5) | 1,38 | 0,39 | 0,60 | 0,19 | – | 0,33 / 2,5 | 306 | 61,3 | 88 | 64 000 |
| handbook | 8 | 0,43 | 2,10 | 0,81 | 1,00 | 0,15 | – | 0,26 / 1,6 | 339 | 61,3 | 274 | 227 000 |
| stand | 12 | 0,50 | 2,20 | 1,48 | 1,33 | 0,18 | – | 0,33 / 1,3 | 477 | 136,1 | 608 | 511 000 |
| sprint | 12 | 0,61 | 2,22 | 1,39 | 1,45 | 0,13 | 1,26 (945) | 0,34 / 1,7 | 560 | 162,9 | 635 | 542 000 |
| crowd | 12 | 1,14 | 2,02 | 1,70 | 1,70 | 0,83 | – | 0,52 / 0,9 | 478 | 136,1 | 607 | 514 000 |
| stand | 16 | 0,32 | 2,46 | 1,88 | 1,89 | 0,12 | – | 0,34 / 1,8 | 698 | 237,1 | 1077 | 911 000 |
| sprint | 16 | 0,39 | 2,41 | 1,72 | 2,05 | 0,08 | 1,03 (1278) | 0,31 / 1,7 | 810 | 276,1 | 1088 | 926 000 |
| crowd | 16 | 0,75 | 2,42 | 2,12 | 2,34 | 0,52 | – | 0,44 / 0,7 | 699 | 237,1 | 1076 | 915 000 |

Aufteilung eines Frames (8 Chunks, stand): Die Threads arbeiten verschachtelt (GUI-Thread bereitet Frame n+1 vor, während der Render-Thread Frame n zeichnet); die Kosten eines Frames sind daher das Maximum, nicht die Summe.

| Anteil | ms pro Frame | Quelle |
|--------|-------------:|--------|
| Meshing | 0 im Stehen; 1,0–1,3 ms pro Sektion auf 4 bis 11 Workern, nicht im Frame | `mesh_job_avg_ms` |
| Mesh-Upload | 0,01 (Spitzen 0,3–1,4 beim Streaming) | `upload_avg_ms`/`upload_max_ms` |
| Server-Tick | 0,28 (eigener Thread, 20 Ticks/s) | `tick_avg_ms` |
| Takt-Genauigkeit des Servers | Verspätung der Ticks gegen ihr 50-ms-Raster (Mittel, p99, Maximum) und verworfene Ticks (E-090) | `tick_late_avg_ms`/`tick_late_p99_ms`/`tick_late_max_ms`, `ticks_skipped` |
| Entities/Charaktere | 0,20 (crowd: 1,00) im GUI-Thread | `entity_mesh_avg_ms` |
| Charakter-Vorschau (Etappe 22, E-231) | 0,008 im GUI-Thread (Inventar offen, animiert; höchstens 0,11–0,14 pro Vorschau-Bild; im Skriptlauf nur mit `PREVIEW_ACTIVE=1` animiert) | `preview_mesh_avg_ms`/`preview_mesh_max_ms` |
| QML/HUD (GUI-Thread ohne Entities) | 0,36 (crowd: 0,31) | `gui_cpu_avg_ms` − Entities |
| Szenengraph (Render-Thread ohne Welt) | 0,84 | `render_cpu_avg_ms` − `world_render_avg_ms` |
| Welt-Renderer (Culling, Draw-Calls, Uploads) | 0,90 | `world_render_avg_ms` |
| GPU (ganzes Fenster) | 0,87 | `gpu_avg_ms` |

Bewertung: Bei 8 Chunks liegt jeder Frame bei unter 2 ms Arbeit pro Thread; die 8,33 ms sind VSync. Teuerster Thread ist der Render-Thread; er und die GPU wachsen mit der Sichtweite (Draw-Calls 274 → 1077, Dreiecke 0,23 → 0,91 Mio.), der GUI-Thread mit der Zahl der Wesen. Der größte mit der Sichtweite wachsende Posten ist der Speicher (317 → 698 MB, Sprint 810 MB).

## Hotspots

**C++ (Time Profiler, `profile.sh time`):**

- **GUI-Thread, crowd:** 92 % von `QcGameController::onFrame` in `updateEntities` (1576 von 1709 ms im Profil); davon 637 ms in `QcEntityMesher::turn` und 259 ms in `rotate`, fast nur `sin`/`cos` aus libm (unsymbolisiert, 814 ms selbst), dazu Vergrößern der Vertexliste (`detachAndGrow`, `realloc`, 110 ms). → E-070.
- **Render-Thread, sprint/stand:** 70–80 % von `QQuickRhiItemNode::render` sind die Draw-Calls der Sektionen im Metal-Treiber (`drawIndexed` 475 ms, `setVertexInput` 298 ms, Ressourcen-Verfolgung `ioGPUResourceListAddHashValue` 185 ms); `isVisible` (Culling) 150 ms; `renderSceneGraph` (HUD und Komposition des Welt-Items) rund 25 %. Die Kosten wachsen mit der Zahl der Draw-Calls (eine pro sichtbarer Sektion und Pass).
- **Worker:** Generierung (`QcTerrainGenerator::carveCaves`, Rauschen) und Meshing (`Neighbourhood::block`, `Lighting`-Aufbau) verteilen sich auf die Worker, nicht im Frame.
- **Server-Thread:** unauffällig (`streamChunks` und Mob-Physik, siehe E-062).

**QML (`profile.sh qml`, sprint 15 s):** Bindungen 380 ms, davon **318 ms die Debug-Zeile `Hud.qml:22`** (2097 Auswertungen, 0,15 ms je Auswertung: Textlayout in jedem Frame). Erzeugen 207 ms und Kompilieren 113 ms fallen beim Start an (Menüs, Bildschirme), nicht im Spiel. Inventar, Ofen und Handbuch kosten im Stehen keine messbare zusätzliche GUI-Zeit (0,43–0,64 ms gegen 0,56 ms); der Ofen hat wegen der Flammen-Animation p99 2,5 ms. Das Handbuch braucht beim Öffnen 50 ms für den ersten Frame (bekannt, E-067).

**Szenengraph (`profile.sh sg`, `visualize`):** Synchronisation unter 1 ms, Render-Schritt durch das Warten auf das Drawable bestimmt; GPU-Frame 0,8–0,9 ms. `QSG_VISUALIZE=overdraw` mit geöffnetem Inventar: die Welt als ein opaker Vollbild-Layer (das Textur-Item des `QQuickRhiItem`), darüber eine halbtransparente Abdunklung, das Inventarfeld und die Felder, Icons und Texte – keine gestapelten Vollbildschichten über die Abdunklung hinaus; `batches` zeigt wenige Batches (Rechtecke, Texte, Icons). Kein Handlungsbedarf.

## Speicher und Chunk-/Mesh-Statistik

`profile.sh alloc` (8 Chunks, stand, nach 10 s): physischer Footprint 520 MB (Spitze 657 MB), davon Heap 143 MB in 217 000 Blöcken, `IOAccelerator (graphics)` 41 MB (Sektions- und Entity-Puffer, Atlas), `IOSurface` 42 MB (Drawables), `owned unmapped (graphics)` 250 MB. Letzterer Posten wächst nicht mit der Sichtweite (Sichtweite 2: 386 MB, 16: 250 MB) und ändert sich nicht mit dem Puffertyp (`Static` statt `Immutable` probiert); er gehört zum Grafiktreiber und ist im Spitzenwert `peak_rss_mb` des Benchmarks nicht enthalten. Bei 16 Chunks: Footprint 908 MB, `IOAccelerator` 143 MB, Heap (`MALLOC_SMALL`) 379 MB – vor allem Chunks im Client und im Server (In-Process-Server), 4-KiB-Paletten-Indizes pro Sektion.

Lecks (`profile.sh leaks`): 288 Lecks mit 18,8 KB, alle in Systemframeworks (XPC-Verbindung von AppIntents), keines in QtCraft.

Sektionen bei 8/12/16 Chunks: 972 / 2160 / 3744 (Sprint am Ende 1250 / 2587 / 4373), davon sichtbar 272 / 606 / 1075; im Mittel rund 230 Dreiecke pro sichtbarer Sektion.

## Optimierungen (Vorher-Nachher, verschränkt gemessen)

Jede Optimierung wurde gegen den Stand direkt davor verschränkt gemessen (je 3 Runden à 15 s, Mittel [Spanne]). Tests, Determinismus der Weltgenerierung und Dateiformate sind unverändert; die Mesher-Tests lesen Licht und Schatten über Getter und erlauben eine halbe Byte-Stufe Toleranz (E-069).

| # | Änderung | Szenario | Vorher | Nachher |
|---|----------|----------|--------|---------|
| 1 | Entity-Vernetzung ohne Winkelfunktionen pro Eckpunkt, Ecken einmal, Liste vorab reserviert (E-070) | crowd 8 | Entity 0,93 [0,85–1,01] ms, GUI-CPU 1,30 [1,25–1,34] ms, p99 GUI 1,89 ms | Entity 0,51 [0,44–0,57] ms, GUI-CPU 0,84 [0,77–0,91] ms, p99 GUI 1,33 ms |
| 2 | Block-Vertex 24 statt 32 Byte, 16-Bit-Indizes (E-069) | stand 8 | Sektionspuffer 61,3 MB, Speicher 318 MB, GPU 0,866 ms | 43,6 MB (−29 %), 293 MB (−8 %), 0,833 ms |
| 2 | | sprint 8 | 79,2 MB, 375 MB, GPU 0,929 ms | 56,2 MB, 338 MB (−10 %), 0,899 ms |
| 2 | | stand 16 | 237,1 MB, 697 MB, GPU 1,910 ms | 168,5 MB, 605 MB (−13 %), 1,882 ms |
| 2 | | sprint 16 | 276,1 MB, 803 MB, GPU 2,042 ms | 196,1 MB, 691 MB (−14 %), 1,962 ms |
| 3 | Debug-Zeile höchstens viermal pro Sekunde, nächster Frame ausdrücklich angefordert, Culling über Ebenen (E-071) | stand 8 | GUI-CPU 0,452 ms, Render-CPU 1,705 ms | 0,411 ms, 1,635 ms |
| 3 | | sprint 8 | GUI-CPU 0,573 ms, Render-CPU 1,772 ms | 0,414 ms (−28 %), 1,674 ms |
| 3 | | stand 16 | GUI-CPU 0,304 ms, Welt-Renderer 1,938 ms | 0,261 ms, 1,867 ms |
| 3 | | sprint 16 | GUI-CPU 0,375 ms, Render-CPU 2,336 ms | 0,292 ms, 2,241 ms |

Sichtbare Sektionen sind vor und nach dem Culling-Umbau gleich (272, 293, 1075, 1085/1086). Geprüft und verworfen: `Static`- statt `Immutable`-Puffer (kein Speicherunterschied), gemeinsamer Quad-Indexpuffer und Instancing der Wesen (Aufwand gegenüber dem Restgewinn, E-069/E-070).

Hinweis zu Optimierung 3: Bis dahin löste das Setzen der Debug-Zeile in jedem Frame unbeabsichtigt den nächsten Frame aus; mit der Drosselung stand das Bild im Stehen still (Benchmark: „no frames rendered“). Seitdem fordert `onFrame` den nächsten Frame ausdrücklich an.

## Schlussmessung

Vorher (562ecc8: Etappe 15 plus Messpunkte) und nachher (Endstand Etappe 16, Release) verschränkt, je 2 Runden à 15 s, Fenster auf dem eingebauten Display im Hintergrund (`QTCRAFT_SCREEN=builtin`, `QTCRAFT_BACKGROUND=1`, beide Stände mit denselben Haken gebaut), Mittelwerte. Zeiten in ms pro Frame, Speicher in MB.

| Szenario | Chunks | GUI-CPU vorher → nachher | Render-CPU | GPU | Entity-Vernetzung | Tick Ø | Spitzenspeicher | Sektionspuffer | Frame Ø / p99 nachher |
|----------|-------:|-------------------------:|-----------:|----:|------------------:|-------:|----------------:|---------------:|----------------------:|
| stand | 8 | 0,35 → 0,28 | 1,04 → 1,08 | 0,83 → 0,84 | 0,11 → 0,07 | 0,17 → 0,16 | 318 → 295 | 61,3 → 43,6 | 8,43 / 13,6 |
| walk | 8 | 0,40 → 0,26 | 1,27 → 1,05 | 0,95 → 0,95 | 0,09 → 0,05 | 0,19 → 0,15 | 387 → 345 | 68,3 → 48,5 | 8,49 / 12,9 |
| sprint | 8 | 0,44 → 0,29 | 1,21 → 1,13 | 0,91 → 0,90 | 0,08 → 0,06 | 0,19 → 0,21 | 376 → 343 | 79,2 → 56,2 | 8,56 / 13,8 |
| crowd | 8 | 0,65 → 0,49 | 0,77 → 0,83 | 1,43 → 1,29 | 0,41 → 0,25 | 0,24 → 0,27 | 321 → 299 | 61,3 → 43,6 | 8,40 / 12,5 |
| inventory | 8 | 0,34 → 0,32 | 1,17 → 1,36 | 0,93 → 0,92 | 0,11 → 0,08 | 0,15 → 0,18 | 321 → 299 | 61,3 → 43,6 | 8,48 / 14,2 |
| furnace | 8 | 0,46 → 0,36 | 1,08 → 0,96 | 0,65 → 0,65 | 0,12 → 0,07 | 0,18 → 0,18 | 309 → 288 | 61,3 → 43,6 | 8,59 / 16,0 |
| handbook | 8 | 0,34 → 0,26 | 1,30 → 1,29 | 0,93 → 0,96 | 0,11 → 0,06 | 0,15 → 0,16 | 340 → 316 | 61,3 → 43,6 | 8,45 / 13,9 |
| light | 8 | 0,46 → 0,30 | 1,25 → 1,07 | 0,83 → 0,82 | 0,14 → 0,07 | 0,19 → 0,15 | 327 → 301 | 61,3 → 43,5 | 8,46 / 14,2 |
| liquids | 8 | 0,36 → 0,31 | 1,09 → 1,15 | 0,82 → 0,84 | 0,11 → 0,07 | 0,15 → 0,17 | 326 → 300 | 61,1 → 43,4 | 8,45 / 13,9 |
| stand | 12 | 0,46 → 0,29 | 2,06 → 1,55 | 1,32 → 1,36 | 0,14 → 0,07 | 0,29 → 0,22 | 483 → 431 | 136,1 → 96,7 | 8,43 / 13,1 |
| sprint | 12 | 0,49 → 0,36 | 1,94 → 1,90 | 1,46 → 1,47 | 0,11 → 0,08 | 0,28 → 0,32 | 566 → 490 | 162,9 → 115,7 | 8,54 / 15,6 |
| crowd | 12 | 0,67 → 0,61 | 1,26 → 1,69 | 1,77 → 1,79 | 0,44 → 0,33 | 0,31 → 0,41 | 481 → 428 | 136,1 → 96,7 | 8,52 / 14,3 |
| stand | 16 | 0,34 → 0,26 | 2,36 → 2,18 | 1,90 → 2,03 | 0,11 → 0,07 | 0,36 → 0,33 | 700 → 609 | 237,1 → 168,5 | 8,45 / 14,4 |
| sprint | 16 | 0,41 → 0,29 | 2,26 → 2,18 | 2,08 → 1,62 | 0,08 → 0,06 | 0,41 → 0,41 | 780 → 692 | 270,2 → 196,1 | 8,42 / 12,4 |
| crowd | 16 | 0,72 → 0,54 | 2,07 → 2,37 | 2,37 → 2,33 | 0,47 → 0,28 | 0,49 → 0,48 | 693 → 602 | 237,1 → 168,5 | 8,41 / 13,0 |

Draw-Calls, sichtbare Sektionen und Dreiecke sind vorher und nachher gleich (z. B. 274 / 272 / 226 700 bei stand 8, 1077 / 1075 / 911 000 bei stand 16). Meshing pro Sektion unverändert 1,0–1,5 ms (sprint 8: 1,12 → 1,14 ms, 620 Jobs), Änderung bis Bild (light) 8,7 → 8,8 ms.

Bewertung: GUI-CPU sinkt in allen Szenarien um 7–35 % (Entity-Vernetzung −35 bis −50 %, Debug-Zeile), der Spitzenspeicher um 7–13 % (Sektionspuffer −29 %). Render-CPU und GPU liegen in der Streuung (±15 % zwischen den zwei Runden; die Render-CPU schwankt mit dem Takt, den das System dem Render-Thread gibt). Der Server-Code ist zwischen beiden Ständen unverändert; die Tick-Zeiten streuen in beide Richtungen (−24 % bis +34 %), wie in den Etappen 11–15 beobachtet.

Hinweise: Die Messungen der einzelnen Optimierungen (Tabelle oben) liefen, wie sich später zeigte, neben zwei versehentlich weiterlaufenden Spielinstanzen (ein ASan-Build, den `xctrace --launch` über die Bundle-Kennung gestartet hatte); beide Seiten jedes A/B waren gleich betroffen, die absoluten Werte (z. B. Entity-Vernetzung 0,93 ms) liegen daher höher als in dieser Schlussmessung (0,41 ms). Die Frame-Abstände dieser Schlussmessung (p99 12,4–16,0 ms) entstanden, während der Mensch am Rechner arbeitete und das Spielfenster im Hintergrund lag; auf ruhigem Desktop liegt p99 bei 9,9–10,3 ms (Perf-Check unten, erste Messung). Die Frame-Kosten-p99 bleiben in allen Läufen unter 3,6 ms, die längeren Abstände kommen also nicht aus dem Spiel.

## Ziele

Festgelegt nach der ersten Messung (E-072). „Frame-Kosten“ ist der größte Mittelwert aus GUI-CPU, Render-CPU und GPU pro Frame – die Grenze, unter der ein Frame ohne VSync möglich wäre.

| Sichtweite | Bildrate | p99 Frame-Abstand | Frame-Kosten | Spitzenspeicher | Begründung |
|-----------:|----------|------------------:|-------------:|----------------:|------------|
| 8 Chunks | ≥ 60 FPS (hart) | ≤ 12,5 ms | ≤ 4 ms | (≤ 600 MB, Perf-Check) | 12,5 ms = 1,5 Refresh-Intervalle bei 120 Hz: höchstens 1 % der Frames verpassen einen Refresh. 4 ms lässt einem viermal langsameren Mac noch 60 FPS. Gemessen 1,7–2,1 ms. |
| 12 Chunks | ≥ 60 FPS | ≤ 16,7 ms | ≤ 6 ms | ≤ 600 MB | Render-Thread und GPU wachsen mit den Draw-Calls (rund 2,2× gegenüber 8 Chunks); Speicher vorher 477–560 MB. |
| 16 Chunks | ≥ 60 FPS | ≤ 16,7 ms | ≤ 8,3 ms | ≤ 800 MB | 8,3 ms = 120 FPS auf diesem Rechner. Speicher war vor den Optimierungen mit 810 MB (sprint) über dem Ziel. |

Erfüllung (Endstand, Werte aus der Schlussmessung und dem Perf-Check):

| Sichtweite | Bildrate | p99 Frame-Abstand | Frame-Kosten | Spitzenspeicher | Ergebnis |
|-----------:|----------|------------------:|-------------:|----------------:|----------|
| 8 Chunks | 117–119 FPS (Ø 8,40–8,59 ms) | 9,9–10,3 ms ruhig; 12,5–16,0 ms bei Arbeit am Rechner | 0,9–1,4 ms (max. Mittel aus GUI, Render, GPU) | 288–345 MB | erfüllt; p99 nur auf ruhigem Desktop |
| 12 Chunks | 117–119 FPS | 13,1–15,6 ms | 1,4–1,9 ms | 428–490 MB | erfüllt (vorher Speicher bis 566 MB) |
| 16 Chunks | 118–119 FPS | 12,4–14,4 ms | 2,2–2,4 ms | 602–692 MB | erfüllt (vorher sprint 780–810 MB, über dem Ziel) |

Die Erfüllung oben ist der Endstand von Etappe 16. Seit 2026-10-14 (E-370) prüft jede Messung neben alt gegen neu auch diese Ziele; ein verfehltes Ziel steht unten als Befund, bis eine Messung es wieder erfüllt.

### Befunde: verfehlte Ziele

| Seit | Ziel | Gemessen | Messung | Vermutete Ursache | Angehen in |
|------|------|----------|---------|-------------------|------------|
| 2026-10-14 | Spitzenspeicher ≤ 800 MB bei 16 Chunks | **1 384 MB** (31.1: 1 385 MB; Etappe 43: 1 386 MB) im Szenario „sprint, 16 Chunks“ | Etappe 31.1, gerendert (`fe0a8c9` gegen `8aa4346`, `bench-ab.sh`, 4 Runden; `docs/Performance-Verlauf.md`, „Etappe 31.1: gerenderte Messung“) | nicht gemessen, nur naheliegend: kein Entladen geänderter Chunks auf dem Server; seit Etappe 16 außerdem mehr Inhalt (Blöcke, Zustände, Texturen). **Übersetzungen ausgeschlossen** (Etappe 43, E-371: das Grundspiel hält 0,7 MB; die „700–870 MB“ waren der Grenzfall eines Mods, jetzt begrenzt) | 31.3 (Entladen); danach gezielt neu messen. Der Verlauf hat nur alt gegen neu verglichen (1 384 → 1 385) und die Zielverletzung nicht gemeldet (Projektzeugnis 2026-10-14). |
| 2026-10-14 | Spitzenspeicher ≤ 600 MB bei 8 Chunks (Perf-Check) | **604–612 MB** im Szenario „sprint, 8 Chunks“ (20-s-Läufe, `dev` `eac19ad` und Etappe 43 gleich; Etappe 28: 598 MB) | Etappe 43, gerendert (`bench-ab.sh`, 6 Runden; `docs/Performance-Verlauf.md`, „Etappe 43: gerenderte Messung“) | wie bei 16 Chunks: kein Entladen auf dem Server, gewachsener Inhalt; `perf-check.sh` läuft standardmäßig nur 10 s (kürzerer Sprint), ob er die Grenze dort reißt, ist nicht geprüft | 31.3 (Entladen); danach mit 20 s neu messen |

Zielprüfung Etappe 31.2 (gerendert, 2026-10-14, 8 Chunks): Bildrate, Frame-Kosten (Mittel höchstens 2,2 ms) und Speicher (höchstens 454 MB) erfüllt. p99 ≤ 12,5 ms im Median aller Szenarien erfüllt, aber 21 % der Läufe (eine spätere Reihe 10 von 12) lagen bei 15–17 ms, bei beiden Ständen gleich und mit Frame-Kosten-p99 ≤ 1,4 ms: verpasste VSyncs von außerhalb des Spiels, der bekannte Vorbehalt „nur auf ruhigem Desktop“, kein Befund. Referenzwerte unverändert.

Zielprüfung Etappe 43 (gerendert, 2026-10-14, 8 und 16 Chunks): Bildrate, Frame-Kosten (8 Chunks höchstens 2,2 ms, 16 Chunks 3,0 ms) und p99 im Median (11,0 / 10,1 ms) erfüllt. Spitzenspeicher verfehlt: sprint 604–612 MB bei 8 Chunks (neuer Befund oben) und 1 386 MB bei 16 Chunks (offener Befund, unverändert); beide Stände gleich, also nicht durch Etappe 43. Referenzwerte unverändert.

## Regressionsschutz

`scripts/perf-check.sh [seconds]` baut das Release-Preset, misst stand, sprint und crowd bei 8 Chunks je einmal und scheitert, wenn ein Wert seine Grenze überschreitet: Frame Ø ≤ 16,7 ms (60 FPS), p99 ≤ 25 ms, GUI-CPU ≤ 4 ms, Render-CPU ≤ 6 ms, GPU ≤ 6 ms, Tick Ø ≤ 5 ms, Spitzenspeicher ≤ 600 MB, Meshing pro Sektion ≤ 5 ms. Die Grenzen liegen beim 3- bis 10-Fachen der Messwerte, damit die Tageslast der Maschine die Prüfung nicht scheitern lässt; nicht Teil von `ctest`.

Lauf am Ende der Etappe (Release, 10 s je Szenario): bestanden. stand: Frame Ø 8,33 ms, p99 10,29 ms, GUI 0,40, Render 1,67, GPU 0,84, Tick 0,22 ms, 293 MB; sprint: 8,33 / 10,24 ms, GUI 0,50, Render 1,87, GPU 0,89, Tick 0,26 ms, 331 MB, Meshing 1,28 ms; crowd: 8,34 / 9,89 ms, GUI 0,81, Render 1,57, GPU 1,28, Tick 0,54 ms, 297 MB.

## Nächste Kandidaten

- Weniger Draw-Calls bei großer Sichtweite (Sektionen einer Chunk-Säule zusammenfassen oder gemeinsame Puffer mit Offsets): größter Posten im Render-Thread bei 16 Chunks.
- Speicher der Chunks (Client und Server halten im Einzelspieler je eine Kopie; Paletten-Indizes mit 4 KiB pro Sektion): größter Heap-Posten bei 16 Chunks.
- Entity-Puffer werden jeden Frame vollständig hochgeladen (crowd rund 380 KB pro Frame); Instancing erst bei deutlich mehr Wesen.

**Bildschirm (seit dem Zusatz nach Etappe 21):** Ein Fenster, das macOS nicht zeigt (Bildschirm aus, gesperrt, verdeckt), rendert keine Frames. `scripts/screen-guard.sh` hält deshalb während Benchmark-, Profil-, Screenshot- und Verify-Läufen den Bildschirm wach (`caffeinate -dimsu`, endet mit dem Skript) und lässt Läufe warten, solange der Bildschirm gesperrt ist (`IOConsoleLocked`); ein Lauf, während dessen gesperrt wurde, wird wiederholt (höchstens zweimal). Eine Sperre von Hand verhindert das nicht; das MacBook während der Läufe offen und entsperrt lassen. **Benchmarks und Profile laufen immer am echten Fenster:** Screenshots (Rundgang, Smoke) rendern seit Etappe 21b offscreen und hängen nicht mehr am Bildschirm (E-229), offscreen gemessene Bildzeiten wären aber wertlos (Takt per Timer, kein Präsentieren über den Compositor, keine Bildschirm-Synchronisation).

## Debug-Anzeige (Etappe 21b)

Seit Etappe 21b sind die Zeitaufteilung des Server-Ticks (Phasen in der BENCH-Zeile: `tick_<phase>_avg_ms`, `tick_<phase>_max_ms` für `players`, `saving`, `chunkLoading`, `blockUpdates`, `liquids`, `mobs`, `items`, `plants`, `fallingBlocks`, `machines`, `streaming`, `clientSync` und `messages`, die Nachrichten zwischen den Ticks) und die Frame-Kosten dauerhaft im Spiel zu sehen (Debug-Taste I: aus, kompakt, voll; E-225) und als JSON-Zeilen abrufbar (`--stats <ms>`, `QTCRAFT_STATS`, Steuerbefehl `debug`). Für eine schnelle Eingrenzung reicht oft ein kurzer Lauf mit `QTCRAFT_STATS=1000`; der volle A/B-Benchmark bestätigt dann. `bench.sh`/`bench-common.sh` nehmen `DEBUG_DISPLAY=off|compact|full` (Standard: die Spielvorgabe kompakt).

Zusatzkosten der Anzeige (Release, 2 verschränkte Runden à 15 s je Stufe, 8 Chunks, ruhiges System: 1-min-Last 2,3–2,9; Mittel der Runden; im Benchmark misst der Frame-Monitor immer voll, die Unterschiede sind also Texte, Graphen, Lichtsonden und das Server-Abo):

| Szenario | Stufe | GUI-CPU Ø / p99 (ms) | Render-CPU Ø | GPU Ø | Frame Ø / p99 | Tick Ø / max | Client-Abgleich im Tick Ø |
|----------|-------|---------------------:|-------------:|------:|--------------:|-------------:|--------------------------:|
| stand | aus | 0,384 / 0,67 | 1,61 | 1,86 | 8,43 / 13,0 | 0,63 / 2,06 | 0,040 |
| stand | kompakt | 0,401 / 0,79 | 1,67 | 1,87 | 8,44 / 15,6 | 0,64 / 1,96 | 0,047 |
| stand | voll | 0,457 / 2,33 | 1,74 | 1,92 | 8,48 / 15,7 | 0,65 / 2,38 | 0,051 |
| crowd | aus | 0,734 / 1,29 | 1,49 | 2,32 | 8,38 / 9,9 | 0,90 / 1,93 | 0,073 |
| crowd | kompakt | 0,743 / 1,36 | 1,43 | 2,30 | 8,39 / 10,0 | 0,71 / 1,88 | 0,057 |
| crowd | voll | 0,789 / 2,82 | 1,42 | 2,31 | 8,37 / 9,8 | 0,72 / 1,83 | 0,060 |

Bewertung: Kompakt kostet 1–4 % GUI-CPU (eine Zeile, Frame-Abstand), voll 7–19 % im Mittel (+0,05–0,07 ms) und hebt das p99 der GUI-CPU auf 2,3–2,8 ms (viermal pro Sekunde: Schnappschuss 0,2 ms, Textlayout rund 1,5 ms); alles weit unter dem Frame-Budget (8,3 ms bei 120 Hz). Render-CPU, GPU, Frame-Abstand und Tick liegen in der Streuung. Die Phasen-Zähler kosten 21 ns pro Runde (rund 0,3 µs pro Tick). Erste Fassung (Lichtsonde flutete immer): GUI-p99 voll 5,4–6,0 ms, Textaktualisierung 3,5–6 ms; seitdem überspringt die Sonde die Flutung ohne Lichtquelle in Reichweite und für eine Zelle unter freiem Himmel (1,6–2,6 ms pro Aktualisierung).


## Tick-Steuerung als Messwerkzeug (Etappe 24)

`/tick` (E-249) hilft bei Messungen und Tests, über `--chat` des Spiels, `chat`/`console` der Steuerschnittstelle oder die Server-Konsole:

- `/tick sprint <n|Ns|Nmin|Nh|Nd>` lässt Spielzeit ohne Takt ablaufen (Wachstum, Öfen, Flüssigkeiten, Tageszeit) und meldet am Ende Ticks, Dauer und Ticks pro Sekunde, also die reine Simulationsleistung ohne die 50-ms-Pausen (Beispiel: `qtcraft-headless --console` mit `tick sprint 1d`). Danach läuft der normale Takt ohne Aufholschuld weiter (keine übersprungenen Ticks).
- `/tick freeze` hält eine Szene an (Wesen, Flüssigkeiten, Tageszeit), damit Screenshots und Frame-Messungen nicht von der Simulation abhängen; Spieler und Kamera bewegen sich weiter, `/tick step <n>` rückt genau n Ticks vor.
- `/tick rate <1..100>` prüft Last bei höherer Tick-Rate (z. B. 40 statt 20), `/tick query` zeigt Soll- und gemessene Rate und die Phasen wie die Debug-Anzeige.

Rate und Einfrieren werden nicht gespeichert; die Benchmark-Szenarien laufen weiter mit 20 Ticks pro Sekunde. Das Szenario `liquids` schickt nach seinem großen `/fill` ein `/confirm` (E-245), sonst bliebe die Fläche ungefüllt.

**Werkzeugwerte (Etappe 24, Release, 8 Chunks, je 3 Läufe à 20 s, Mittel [Spanne], Last 2,9–5,5, kein Lauf belastet):** Nachrichten an den Client gezählt mit einem vorübergehenden Zähler in `QcClientSession::handleMessage` (nicht im Repository), sonst die BENCH-Zeile; Vergleich jeweils mit demselben Build bei 20 Ticks pro Sekunde.

| Szenario | Modus | Ticks/s | Tick Ø ms | Client-Abgleich ms pro Tick | GUI-CPU Ø ms | Nachrichten/s an den Client | Wesen am Ende |
|----------|-------|--------:|----------:|----------------------------:|-------------:|----------------------------:|--------------:|
| crowd | 20 Ticks/s | 20 | 0,494 [0,45–0,57] | 0,043 | 0,486 | 27 (Entity-Bewegung 20, Klänge 4,5, Zeit 1) | 81 |
| crowd | `/tick rate 100` | 99 | 0,448 [0,43–0,48] | 0,033 | 0,452 | 119 (Entity-Bewegung 99, Klänge 17, Zeit 1) | 80 |
| crowd | `/tick sprint 864000` | 10 900 [10 650–11 170] | 0,091 | 0,0001 | 0,155 | 184 (Klänge 105, Entity-Bewegung, Zeit, Erscheinen, Entfernen je ≈ 20) | 12 |
| crowd | `/tick sprint 2000` (volles Gedränge) | 6 800 [6 640–7 120] | – | – | – | – | 83–85 |
| flat-crowd | 20 Ticks/s | 20 | 0,253 | 0,048 | 0,435 | 28 | 80 |
| flat-crowd | `/tick rate 100` | 100 | 0,193 | 0,032 | 0,421 | 123 | 73 |
| flat-crowd | `/tick sprint 864000` | 31 400 [31 300–31 650] | 0,032 | 0 | 0,094 | 123 (Klänge 58, Zeit 20, Bewegung 20, Erscheinen und Entfernen je 13) | 2 |
| flat-crowd | `/tick sprint 2000` (volles Gedränge) | 13 700 [12 900–14 600] | – | – | – | – | 77 |

- **Sprint:** Der Server schafft im Gedränge rund 6 800 Ticks pro Sekunde (340-fache Geschwindigkeit; `Sprint ended: 2000 ticks in 281–301 ms` im Log), in der flachen Welt rund 13 700. Ein langer Sprint räumt die Szene selbst ab (Items verfallen nach 6000 Ticks, Zombies verbrennen am Tag, Wesen verschwinden): nach wenigen Sekunden laufen 11 000 bzw. 31 000 Ticks pro Sekunde mit 12 bzw. 2 Wesen. Der Client bekommt Welt, Entities und Zeit höchstens alle 50 ms (je ≈ 20 pro Sekunde, E-249), dazu aber jeden Klang einzeln: im kurzen Sprint mit vollem Gedränge rund 400 Klänge in 0,3 s, im langen 105 pro Sekunde (normal 4,5). GUI- und Render-CPU sinken im Sprint (weniger Wesen), der Server-Thread ist voll ausgelastet; Frame p99 bleibt bei 9,3–10,3 ms.
- **Rate 100:** Der Tick kostet so viel wie bei Rate 20 (0,45 statt 0,49 ms, 4,5 % des 10-ms-Intervalls), der Client-Abgleich 0,033 ms pro Tick, also 3,3 statt 0,9 ms pro Sekunde; der Client bekommt rund 120 statt 27 Nachrichten pro Sekunde (vor allem Entity-Bewegungen mit jedem Tick, E-249). GUI-CPU, Render-CPU, GPU und Frame p99 bleiben gleich (GUI-CPU −7 %, Streuung): Der Client interpoliert weiter mit seinem 20-Hz-Takt, die zusätzlichen Bewegungen kosten im GUI-Thread nichts Messbares.

## Mehrspieler-Last (Etappe 25)

Referenzwerte für den Server-Tick mit Spielern über TCP (Release, `qtcraft-headless --server` in der flachen Welt, Seed 42, 8 Chunks, nur auf 127.0.0.1; Bots mit `qtcraft-headless --join`, die geradeaus sprinten und springen, alle 4 s einen Block abbauen und eine Chatzeile schreiben; ein Bot als Operator liest alle 4 s die Server-Debugdaten über den Steuerbefehl `debug`). Je 3 Läufe, Mittel über rund 45 s nach dem Beitritt; Einzelwerte und Methode in `docs/Performance-Verlauf.md` (Etappe 25).

| Bots | Tick Ø ms | Tick max ms (Autosave) | Nachrichten Ø ms | Streaming Ø ms | Client-Abgleich Ø ms | Server-CPU | Server-Speicher MB |
|-----:|----------:|-----------------------:|-----------------:|---------------:|---------------------:|-----------:|-------------------:|
| 1 | 0,25 | 6,8 | 0,017 | 0,14 | 0,038 | ≈ 1,2 % eines Kerns | 42 |
| 4 | 0,45 | 9,8 | 0,042 | 0,31 | 0,051 | ≈ 2,4 % eines Kerns | 72 |

Der Mehraufwand pro zusätzlichem laufenden Spieler liegt bei rund 0,07 ms Tick, vor allem Streaming in neue Gebiete und die Eingaben mit 20 Hz. Der schlechteste Tick ist in beiden Fällen der Autosave, nicht das Netz. Für eine spätere Messung mit mehr Spielern lässt sich derselbe Aufbau mit mehr Bots wiederholen; eine Steigerung über diese Werte hinaus ist mit der Phase Nachrichten und Streaming einzugrenzen.
