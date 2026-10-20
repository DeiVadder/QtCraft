# Performance

Auswertung der Etappe 16 (Profiling und Optimierung). Messrechner: MacBook Pro M4 Max, macOS 26, eingebautes Display 120 Hz (ab der Schlussmessung zusätzlich zwei externe Displays mit 60 Hz angeschlossen, siehe unten). Alle Werte aus RelWithDebInfo-Builds. Der Verlauf pro Etappe steht in [Performance-Verlauf.md](Performance-Verlauf.md), die Entscheidungen in E-068 bis E-072.

## Messaufbau

| Werkzeug | Aufruf | Liefert |
|----------|--------|---------|
| Benchmark-Pfad | `QTCRAFT_BENCHMARK=<s>`, `QTCRAFT_BENCHMARK_SCENARIO=…` | `BENCH`-Zeile: Frame-Abstand Ø/p99, Server-Tick Ø/max, Spitzenspeicher, **Frame-Kosten unabhängig von VSync** (E-068): CPU-Zeit pro Frame von GUI- und Render-Thread (Thread-CPU-Uhr), Welt-Renderer, Upload (Zeit, MB), Entity-Vernetzung, GPU-Zeit (RHI-Zeitstempel, `QSG_RHI_PROFILE=1`), Meshing pro Sektion (Worker), Draw-Calls, sichtbare Sektionen, Dreiecke, Sektionen und ihre Pufferbytes; am Ende `server_mobs`, `server_items` (Kreaturen und Item-Stapel auf dem Server), `inventory_items` (Items im Inventar), E-332, `server_block_entities` (Behälter, Maschinen, Setzlinge) und `block_entity_problems` (verletzte Regeln, `QcBlockEntityChecks`, E-357), `entity_problems` (verletzte Regeln des Entity-Registers, `QcEntityChecks`, E-364) |
| Paketwechsel im Benchmark | `QTCRAFT_BENCHMARK_PACK_SWITCH=<ids>` (zusätzlich zu `QTCRAFT_BENCHMARK`; in den Skripten `PACK_SWITCH=<ids>`) | Schaltet die Texturpakete nach einem Drittel des Laufs ein (vor die Pakete des Laufs, als eine Auswahl, ein Atlasbau) und nach zwei Dritteln wieder zurück; die gespeicherte Auswahl des Spielers bleibt unberührt (`selectForRun`, nichts wird in `settings.ini` geschrieben); Logzeilen `PACKSWITCH on` und `off`: Atlas fertig nach ms (`atlas_ms`: vom Wechsel bis nach `atlasBuilt`, also Auflisten, Laden und Bauen im Hintergrund und das Übernehmen in `applyAtlas` im GUI-Thread, das vor dem Messhaken an `atlasBuilt` hängt; dessen Anteil steht getrennt in `Texture atlas applied in … ms`), längster Frame, Frames ab 50 ms, längste GUI-/Render-/Weltrenderzeit pro Frame bis 1 s nach dem Atlas, dazu `Texture atlas applied in … ms` (E-332) |
| `scripts/bench.sh` | `[runs] [seconds]` (Standard 3 × 10 s), `SCENARIOS=…` (auch Gruppen wie `@core`), `VIEW=…`, `KEYS=…` | Median über mehrere Läufe (Etappen-Check) |
| `scripts/bench-ab.sh` | `<Ergebnisdatei> [<Runden>] [<Sekunden>] <Name>=<App>[:<VAR>=<Wert>…] …` (Standard: `auto` = adaptiv 3 bis 6 Runden, 10 s, `SCENARIOS=@core`), `--duration-check <Ergebnisdatei> <App>`, `SCENARIOS=…`, `VIEW=…`, `KEYS=…` | Verschränkte A/B-Messung beliebig vieler Builds (pro Runde und Szenario jeder Build einmal, Reihenfolge rotiert pro Runde, E-331); `:<VAR>=<Wert>` hinter der App setzt `TEXTURE_PACKS`, `PACK_SWITCH`, `WORLD_TYPE`, `DEBUG_DISPLAY`, `PREVIEW_ACTIVE`, `EXPLORE_STEP` oder `RUN_SECONDS` nur für diesen Namen (steht als `settings=` in jeder Ergebniszeile; `--check` prüft nur die Argumente) (z. B. ein Build unter mehreren Namen mit verschiedenen Texturpaketen: `hd64=<App>:TEXTURE_PACKS=builtin:qtcraft_hd_64`), wartet erst auf einen `bench`-Slot des Slot-Brokers (`scripts/qc-slot.py`, E-781: exklusiv, Last < 4), hält die Sperre `~/.cache/qtcraft/bench.lock`, wartet vor jedem Lauf bis zu 3 min auf einen ruhigen Rechner, schreibt Kopfzeile pro Reihe sowie Position, Reihenfolge, Last und fremde Prozesse vor und nach jedem Lauf mit |
| `scripts/bench-headless.sh` | wie `bench-ab.sh`, aber `<Name>=<Build-Verzeichnis>` und `<Spielsekunden>` (Standard 60, `SCENARIOS=@server`) | Serverseite der Szenarien ohne Fenster und ohne Echtzeit (E-388): `tests/test_scenario_measure` jedes Builds spielt dieselbe Szene und tickt so schnell es geht; Tick Ø/p99/max, Phasen, Wesen, Items, fallende Blöcke, Block-Entities, Chunks, Prozessspeicher; gleiche Ergebniszeilen, Sperre, Rotation und adaptive Runden |
| `scripts/bench-adaptive.sh` | `[--report] <Ergebnisdatei>` | Entscheidung der adaptiven Runden für die letzte Reihe einer Ergebnisdatei; `--report` listet Mittel, Spanne und Abstand zum ersten Namen je Szenario und Schlüssel |
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

Szenarien (`scripts/bench-common.sh`): **stand** (Stehen am Spawn), **walk** (Gehen geradeaus), **sprint** (Sprinten und Springen, Chunk-Streaming), **crowd** (56 Mobs und 24 Items), **inventory**, **furnace** (gefüllter Ofen, Blick nach unten), **handbook** (Handbuchseite Stein), **light** (Fackeln erscheinen/verschwinden), **liquids** (Wasser- und Lavaquellen fluten die Fläche), **glass** (nachts auf geräumtem Holzboden zwischen drei Wänden aus Scheiben, Glas, Laub und Eis mit acht Lampen; zweimal pro Sekunde erscheinen oder verschwinden Lichtquellen-Reihen und 3 × 3-Flächen durchsichtiger Blöcke: Sortierung durchscheinender Flächen, Cutout-Meshing, Blocklicht, Neuvernetzung; nur Blöcke von vor der Block-Erweiterung, also auch auf älteren Ständen messbar), **glass-full** (dasselbe mit den Blöcken der Block-Erweiterung: gefärbtes Glas und Scheiben, Eisengitter, Glühwürmchenglas, Laternen, kleine Fackeln, Netze; nur neuere Stände, ältere lehnen unbekannte Blöcke mit einer Hilfezeile ab), **showcase** (seit Etappe 21: Stehen in der Showcase-Welt, `--showcase --spawn 40,-19 --look 345,-8`, zwischen der ersten und zweiten Reihe mit Blick über die Glasgehege und die Podeste der Flüssigkeiten: ein dauerhaftes Wesen jeder Art (im Grundspiel sechs), Hunderte Beschriftungen in Reichweite (bis 48 pro Frame gezeichnet, bis 48 Sichtstrahlen), viel Glas und Teilformen; die Welt hängt nicht vom Seed ab; nur Stände ab Etappe 21), **armour-crowd** (seit Etappe 23: flat-crowd in der flachen Welt plus 40 Rüstungsständer mit vollem Diamantsatz und Umhang und 8 Gliederpuppen mit Umhang: Anbauteile, Umhänge, Abgleich der Ständer; E-242), **armour-walk** (seit Etappe 23: walk mit dem eigenen Spieler in Diamantrüstung mit Umhang, Kamera hinter dem Spieler), **frozen** (seit Etappe 33: Stehen am Tag auf dem gefrorenen Meer der Tundra nordwestlich des Spawns, `--spawn -325,-300 --look 270,-15`, Eis über Wasser bis zum Horizont: jeder Abschnitt mit Wasser unter Eis zeichnet die Wasser- und die durchsichtige Liste, E-390; ältere Stände stehen am selben Ort), **village**, **village-night**, **village-swamp** (seit Etappe 37: Tag bzw. Nacht auf einer Säule von 12 Blöcken 45 m südlich des Ebenen-Dorfs bei 964, −634 bzw. des Sumpf-Dorfs bei 555, −214, Blick nach Norden über das Dorf; ältere Stände sehen dort Gelände ohne Dorf), **minecarts** (seit Etappe 32: 64 Loren auf zwei Schleifen aus Antriebsschienen in der flachen Welt, Blick von einer Säule). Ein `SCENE_SPAWN` gilt im Fenster seitdem in jeder Welt, nicht nur in der Showcase-Welt. **Welttypen (E-223):** Jedes Szenario läuft mit `flat-` davor (z. B. `flat-crowd`, `flat-liquids`, `flat-light`, `flat-glass`) in einer flachen Welt (Gras auf y 63, keine Bäume, Höhlen oder Wesen von selbst) bzw. mit `void-` in der leeren Welt (5 × 5-Plattform, Mittag; nur für Szenarien, die ihren Boden selbst bauen); `WORLD_TYPE=flat|void` gilt für alle Szenarien eines Laufs. So verschieben Änderungen der Weltgenerierung (wie E-101, E-214, siehe E-218) die Vergleiche nicht mehr; `bench.sh` misst `flat-crowd` und `flat-liquids` standardmäßig mit. Die bisherigen Szenarien bleiben im normalen Gelände, damit die Reihe in `Performance-Verlauf.md` weiter vergleichbar ist. Chat-Befehle waren in Etappe 20 auch für den lokalen Spieler auf 2 pro Sekunde (Stoß 20) begrenzt (light verlor rund 19 von 90 Fackel-Befehlen, crowd einen Startbefehl); seit dem Nachtrag zu E-091 ist der lokale Spieler davon ausgenommen. Die glass-Szenarien bleiben unter der alten Grenze. Seed 42, Blick 45°/−10°, Ton gemischt, aber stumm.

Erkenntnisse zu den Werkzeugen:

- VSync lässt sich auf macOS nicht abschalten: `QSG_NO_VSYNC=1` (auch mit `QSG_RENDER_LOOP=basic`) meldet „attempting to disable vsync“, die Frames bleiben aber bei 120 Hz, weil der Display-Link die Update-Anforderungen taktet. Deshalb misst der Benchmark CPU- und GPU-Zeit pro Frame (E-068). Das Szenengraph-Log zeigt im Render-Schritt 5–9 ms: Das ist überwiegend Warten auf das nächste Drawable, keine Arbeit.
- `xctrace record --launch` startete über die Bundle-Kennung eine andere Build-Variante desselben Bundles (ASan-Build) und ohne die Umgebungsvariablen; `profile.sh` startet das Spiel selbst und hängt `xctrace --attach` an. Das Template „Allocations“ beendet auf diesem Rechner weder angehängt noch gestartet (nach 400 s abgebrochen); `profile.sh alloc` nutzt `heap` und `vmmap`.
- Qt-Frameworks werden im Time Profiler nicht symbolisiert (nur Adressen); die eigenen Funktionen und die Systembibliotheken schon.
- Der Frame-Abstand (p99) hängt am Display und an der Tageszeit der Messung (Zweiteilung 9,1/10,3 ms seit Etappe 12); er wird nur verschränkt (A/B abwechselnd) verglichen.

## Messmethode: headless und Fenster (E-388)

Seit E-388 misst jede Etappe ihre Werte dort, wo sie entstehen:

- **Headless, ohne Echtzeit** (`scripts/bench-headless.sh`): alles, was der Server tut: Tick Ø, p99 und Maximum, die Tick-Phasen, Wesen, Items, fallende Blöcke, Block-Entities, geladene Chunks und den Speicher des Messprozesses (Server plus ein roher Client). `test_scenario_measure` startet die Szene aus `scripts/bench-common.sh` (`scene_of`: dieselben Chat-Zeilen, Welt, Startpunkt und Blickrichtung wie im Fenster), wartet die Chunks um den Spieler ab, misst nach 60 Aufwärm-Ticks die angegebenen Spielsekunden und tickt dabei so schnell der Server kann (Chunks im Hintergrund wie im Spiel; sind welche unterwegs, bekommt der Hintergrund 3 ms vor dem Tick). `/tick sprint` läuft sofort, seine Ticks zählen mit wie im Fenster (`farm`). Szenarien, die den Spieler bewegen oder die eigenen Aktionen des Spiel-Benchmarks brauchen (`walk`, `sprint`, `light`, `liquids`) oder einen Bildschirm (`inventory`, `furnace`, `handbook`), haben keine headless Form und werden mit Hinweis übersprungen. Ein Lauf dauert Sekunden (Release, Rauchtest am 2026-10-15: `explore` 3 Spielsekunden in 0,06 s, `farm` 1860 Ticks in 0,17 s, eine ganze Reihe `@server` mit 3 Spielsekunden, 2 Namen und 1 + 1 adaptiven Runden in 8 s; mit 60 Spielsekunden je Lauf rund 1 bis 4 s), braucht keinen Bildschirm und keine Freigabe für Fensterläufe, wohl aber die Sperre (das Skript nimmt sie selbst). Ein Build von vor E-388 bekommt `tests/gameplay/test_scenario_measure.cpp` und seine Zeile in `tests/CMakeLists.txt` in seinen Checkout kopiert (wie die Messtests von 31.3). `STATS=<ms>` schreibt `SAMPLE`-Zeilen (Speicher, Chunks, Wesen, Items, fallende Blöcke) alle `<ms>/1000` Spielsekunden ins Log jedes Laufs, z. B. die Speicherkurve des Erkundungsflugs.
- **Absolute Tick-Werte headless sind kleiner als im Fenster** (Rauchtest: crowd 0,12 statt rund 1,1 ms, Phase Wesen 0,10 statt 0,80 ms; Signal-Stationen 0,02 statt 0,35 ms): Ticks direkt hintereinander laufen mit warmen Caches und, vermutlich, auf einem Leistungskern mit vollem Takt, während der Server-Thread im Spiel zwischen den Ticks 50 ms schläft. Headless-Werte werden darum nur mit headless gemessenen Werten verglichen (A/B im selben Lauf); für den Abgleich mit der Reihe in `Performance-Verlauf.md` und den Zielen bleibt der Tick der Fenster-BENCH-Zeile. Was headless gut zeigt, ist die Arbeit je Tick und ihre Verteilung auf die Phasen, mit kleiner Streuung.
- **Im Fenster** (`scripts/bench-ab.sh`): nur, was das Fenster braucht: Bildzeiten (Ø, p99), GPU, GUI- und Render-CPU, Vernetzung, Gesamtspeicher des Spiels. Die Server-Werte der BENCH-Zeile bleiben zur Kontrolle (z. B. `entities`, `server_mobs` für die Vergleichbarkeit von `crowd`), bewertet werden Server-Kosten aber headless.

**Standards:** 10 s je Fensterlauf (vorher 20 s; `<Sekunden>` = 20 bleibt möglich), `SCENARIOS=@core`, Runden `auto`. Der Erkundungsflug `explore` fliegt standardmäßig 10 m je Schritt (40 m/s, `EXPLORE_STEP`) und läuft im Fenster 75 s mit `STATS=5000` (15 Punkte der Speicherkurve, 3 km neues Gelände statt 6 km in 300 s); `EXPLORE_STEP=5` mit 300 s ist der Flug der Messungen von 31.3. Seine Server-Werte (Tick, Chunks, Server-Speicher) kommen headless (`SCENARIOS=explore`, 300 Spielsekunden in wenigen Sekunden).

**Adaptive Runden:** Alle Szenarien laufen `ROUNDS_MIN` Runden (3). Dann entscheidet `scripts/bench-adaptive.sh` je Szenario über die Schlüssel `KEYS` (Fenster: `frame_p99_ms gpu_avg_ms render_cpu_avg_ms gui_cpu_avg_ms tick_avg_ms peak_rss_mb`, headless: `tick_avg_ms tick_p99_ms peak_rss_mb`): weitere Runden bis `ROUNDS_MAX` (6) nur, wenn bei einem Schlüssel der Abstand eines Namens zum ersten Namen im Band `THRESHOLD ± BAND` liegt (10 ± 5 %, also 5 bis 15 %: liegt er über oder unter der Schwelle?) oder die Spanne eines Namens (größter minus kleinster Wert durch Mittel) `SPREAD` (25 %) übersteigt. Ein klarer Abstand (unter 5 % oder über 15 %) bei ruhiger Streuung braucht keine weiteren Runden. Schlüssel, die ein Szenario nicht meldet oder die überall 0 sind, zählen nicht. Die Ergebnisdatei hält die Entscheidung fest (`# adaptive after round 3: more=…; done=…` und je Szenario die Gründe), jede Zeile einer weiteren Runde trägt `adaptive=1`. Eine Zahl als `<Runden>` misst fest so viele Runden (ohne Entscheidung), `<min>-<max>` setzt die Grenzen. Weil `frame_p99_ms` auf diesem Rechner zweigipflig ist (Spanne oft über 25 %), bekommen Szenarien mit Bildzeiten in der Regel 6 Runden, wie bisher für p99 verlangt; Server-Szenarien headless meist 3.

**Szenario-Gruppen** (`scripts/bench-groups.sh`, `SCENARIOS=@core,@signal flat-crowd`, Gruppen und Namen gemischt, jedes Szenario einmal):

| Gruppe | Szenarien | Wählen bei |
|--------|-----------|------------|
| `@core` | stand sprint showcase crowd | jeder Etappe (Standard von `bench-ab.sh`) |
| `@world` | walk sprint flight flat-crowd | Weltgenerierung, Streaming, Chunk-Ankunft |
| `@light` | light glass glass-full void-light | Blocklicht, Durchsichtiges, Neuvernetzung, Shader |
| `@liquids` | liquids flat-liquids liquid-fill | Flüssigkeiten (Fluss headless, Vernetzung im Fenster) |
| `@entities` | crowd flat-crowd armour-crowd armour-walk | Wesen, Entity-Vernetzung, Anbauteile, Umhänge |
| `@signal` | signal-stress und die drei Signal-Stationen der Showcase | Signaltechnik, Maschinen, Kolben |
| `@ui` | inventory furnace handbook | QML-Bildschirme |
| `@memory` | explore showcase-flight | Speicher und Entladen (mit `STATS=5000`) |
| `@server` | crowd gravel-collapse farm farm-seeds explore und die drei Signal-Stationen | Standard von `bench-headless.sh`: Server-Tick jeder Etappe |
| `@farm` | farm farm-seeds | Pflanzen, Items, Selbstpflanzen |
| `@fire` | forest-fire | Feuer: Schritte, abbrennende Blöcke, Licht und Neuvernetzung eines Waldbrands (headless und im Fenster, E-393) |
| `@explosions` | explosive-chain | Sprengstoff: Kettenreaktion von 2048 Sprengstoffen unter den Grenzen, headless die Tick-Phase Explosionen, im Fenster Block-Stapel, Neubeleuchtung und Rauchwolken (E-602, E-604) |
| `@weather` | rain snow thunderstorm forest-fire-storm | Wetter: Regen- und Schneepartikel, bedeckter Himmel, Nebel, Blitze im Fenster; headless die Tick-Phase Wetter mit Schneefall und Blitzen, Regen über einem Waldbrand (E-395, E-396) |

Typischer Etappen-Plan: headless `@server` plus die Gruppe der Änderung (z. B. `@signal`), im Fenster `@core` plus die Gruppe der Änderung, wenn sie das Bild betrifft; Texturpakete wie bisher über `:TEXTURE_PACKS=` auf `@core`.

**Laufdauer prüfen (geplant, Fensterlauf nach Freigabe des Menschen):** `scripts/bench-ab.sh --duration-check <Ergebnisdatei> <App>` misst denselben Build mit 10 s und 20 s je Lauf verschränkt (Namen `s10` und `s20`, `RUN_SECONDS`), 6 Runden über `@core`, und gibt am Ende `bench-adaptive.sh --report` aus. Erwartung: Mittel von frame Ø, GPU, Render- und GUI-CPU, Tick Ø und Spitzenspeicher liegen bei 10 s innerhalb weniger Prozent der 20-s-Werte; die Spanne von p99 und CPU darf größer werden, aber nicht über die der 20-s-Läufe plus 10 Prozentpunkte. Trifft das zu, bleiben 10 s der Standard; sonst wird die Dauer je Schlüssel festgelegt (z. B. 20 s nur für p99) und hier eingetragen. Ebenso einmalig: `explore` 75 s mit `EXPLORE_STEP=10` gegen 300 s mit `EXPLORE_STEP=5` (Speicherkurve flacht in beiden auf derselben Höhe ab, Server-Chunks bleiben bei der Sicht).

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
| 2026-10-14 | Spitzenspeicher ≤ 800 MB bei 16 Chunks | **1 384 MB** (31.1: 1 385 MB; Etappe 43: 1 386 MB) im Szenario „sprint, 16 Chunks“ | Etappe 31.1, gerendert (`fe0a8c9` gegen `8aa4346`, `bench-ab.sh`, 4 Runden; `docs/Performance-Verlauf.md`, „Etappe 31.1: gerenderte Messung“) | nicht gemessen, nur naheliegend: kein Entladen geänderter Chunks auf dem Server; seit Etappe 16 außerdem mehr Inhalt (Blöcke, Zustände, Texturen). **Übersetzungen ausgeschlossen** (Etappe 43, E-371: das Grundspiel hält 0,7 MB; die „700–870 MB“ waren der Grenzfall eines Mods, jetzt begrenzt) | 31.3 (Entladen); danach gezielt neu messen. Der Verlauf hat nur alt gegen neu verglichen (1 384 → 1 385) und die Zielverletzung nicht gemeldet (Projektzeugnis 2026-10-14). **Etappe 31.3, headless:** der Server-Anteil ist klein: im Erkundungsflug kosten 669 statt 306 gehaltene Chunks samt Entities rund 16 MB (Prozess 96 gegen 80 MB nach 500 s), ein 20-s-Sprint hält noch weniger; das Entladen beseitigt ihn. Der Großteil liegt beim Client (Sektionspuffer allein 349 MB); das ist nicht Teil von 31.3 und gehört zur Neugliederung des Clients bzw. zum Feinschliff der Darstellung (46/47). **Etappe 31.3, gerendert (2026-10-15): 1 389 → 1 390 MB, durch 31.3 nicht verändert**; der 20-s-Sprint hält auf dem Server ohnehin kaum mehr Chunks als die Sicht, der Rest ist Client (Sektionspuffer 349 MB bei 5356 Sektionen). Bleibt offen. |
| 2026-10-14 | Spitzenspeicher ≤ 600 MB bei 8 Chunks (Perf-Check) | **604–612 MB** im Szenario „sprint, 8 Chunks“ (20-s-Läufe, `dev` `eac19ad` und Etappe 43 gleich; Etappe 28: 598 MB) | Etappe 43, gerendert (`bench-ab.sh`, 6 Runden; `docs/Performance-Verlauf.md`, „Etappe 43: gerenderte Messung“) | wie bei 16 Chunks: kein Entladen auf dem Server, gewachsener Inhalt; `perf-check.sh` läuft standardmäßig nur 10 s (kürzerer Sprint), ob er die Grenze dort reißt, ist nicht geprüft | 31.3 (Entladen); danach mit 20 s neu messen. **Etappe 31.3, gerendert (2026-10-15): sprint 607–613 MB (12 Runden, `dev` 608), durch 31.3 nicht verändert**; im langen Erkundungsflug (300 s) Spitze 601 MB statt 615 MB, der Server hält dabei 380 statt bis zu 742 Chunks. Der Rest ist Client (Sektionen, Atlas, Puffer); bleibt offen für die Neugliederung des Clients bzw. den Feinschliff der Darstellung (46/47). |

Zielprüfung Etappe 31.2 (gerendert, 2026-10-14, 8 Chunks): Bildrate, Frame-Kosten (Mittel höchstens 2,2 ms) und Speicher (höchstens 454 MB) erfüllt. p99 ≤ 12,5 ms im Median aller Szenarien erfüllt, aber 21 % der Läufe (eine spätere Reihe 10 von 12) lagen bei 15–17 ms, bei beiden Ständen gleich und mit Frame-Kosten-p99 ≤ 1,4 ms: verpasste VSyncs von außerhalb des Spiels, der bekannte Vorbehalt „nur auf ruhigem Desktop“, kein Befund. Referenzwerte unverändert.

Zielprüfung Etappe 43 (gerendert, 2026-10-14, 8 und 16 Chunks): Bildrate, Frame-Kosten (8 Chunks höchstens 2,2 ms, 16 Chunks 3,0 ms) und p99 im Median (11,0 / 10,1 ms) erfüllt. Spitzenspeicher verfehlt: sprint 604–612 MB bei 8 Chunks (neuer Befund oben) und 1 386 MB bei 16 Chunks (offener Befund, unverändert); beide Stände gleich, also nicht durch Etappe 43. Referenzwerte unverändert.

Zielprüfung Etappe 31.3 (gerendert, 2026-10-15, 8 und 16 Chunks): Bildrate (117–119 FPS) und Frame-Kosten (8 Chunks höchstens 2,2 ms GPU in crowd, 16 Chunks 2,9 ms) erfüllt; p99 ≤ 12,5 ms diesmal auch im Median verfehlt: 14,5 ms über die 72 Läufe der Hauptreihe (48 über 12,5 ms, bei beiden Ständen gleich verteilt, schon ab dem ersten Lauf und vor dem Anschluss eines externen Monitors), Frame-Kosten-p99 höchstens 3,3 ms in jedem Lauf: verpasste VSyncs von außerhalb des Spiels, der bekannte Vorbehalt „nur auf ruhigem Desktop“; Etappe 43 lag am Vortag bei 11,0 ms. Kein Befund des Spiels, bei der nächsten Messung beobachten. Spitzenspeicher: stand, showcase, crowd und die Signal-Stationen 318–453 MB erfüllt; sprint 8 Chunks 607–613 MB und sprint 16 Chunks 1 390 MB weiter verfehlt (Befunde oben, durch 31.3 nicht verändert, Anteil des Clients). Tick-Ziele: kein Tick über 50 ms außer einem einzelnen Stillstand von 86 ms in einem von sechs Erkundungsflügen (ganzer Prozess, Bild 505 ms; Folgeaufgabe in `docs/Performance-Verlauf.md`), keine übersprungenen Ticks. Referenzwerte unverändert.

Zielprüfung Etappe 33 (gerendert, 2026-10-15, 8 und 16 Chunks): Bildrate (116–119 FPS) und Frame-Kosten (8 Chunks höchstens 2,25 ms GPU in crowd und 1,94 ms Render-CPU im neuen Szenario frozen, 16 Chunks 2,85 ms) erfüllt; p99 im Median 15,0 ms bei beiden Ständen (externer Monitor angeschlossen, Vorbehalt wie in 31.3). Spitzenspeicher: sprint 8 Chunks (20 s) 607–610 MB und sprint 16 Chunks 1 390–1 394 MB weiter verfehlt (Befunde oben, durch 33 nicht verändert), alle übrigen Szenarien höchstens 555 MB. Kein Tick über 50 ms, keine übersprungenen Ticks. Referenzwerte unverändert.

Zielprüfung Etappe 35 (gerendert, 2026-10-16, 8 Chunks, `@core` und `@weather`): Bildrate (120 FPS), Frame-Kosten (höchstens 2,65 ms GPU in crowd; in Regen, Schnee, Gewitter und Waldbrand im Gewitter höchstens 1,85 ms GPU und 1,83 ms Render-CPU) und p99 ≤ 12,5 ms (alle 96 Läufe 9,2–10,5 ms, externer Monitor angeschlossen) erfüllt. Spitzenspeicher stand, showcase, crowd und die Wetter-Szenen 332–457 MB, sprint (10 s) 560 MB erfüllt; sprint 20 s (8 Chunks) und 16 Chunks nicht neu gemessen (Befunde oben, durch 35 nicht berührt). Kein Tick über 50 ms, keine übersprungenen Ticks. Referenzwerte unverändert.

Zielprüfung Etappe 37.1 (gerendert, 2026-10-16, 8 Chunks, `@core` und das neue Szenario `horde`): Bildrate (120 FPS), Frame-Kosten (höchstens 2,69 ms GPU in crowd) und p99 ≤ 12,5 ms im Median (10,3 ms über 60 Läufe, 4 Ausreißer bei Last auf beiden Ständen) erfüllt. Spitzenspeicher stand, showcase, crowd, horde 331–459 MB, sprint (10 s) 561 MB erfüllt; sprint 20 s (8 Chunks) und 16 Chunks nicht neu gemessen (Befunde oben, durch 37.1 nicht berührt). Kein Tick über 50 ms (höchstens 5,0 ms), keine übersprungenen Ticks. Referenzwerte unverändert.

Zielprüfung Dorf-Sektion (37), gerendert (2026-10-17, `f406734` gegen `8d714f0`, 8 und 16 Chunks; `@core`, die neuen Szenarien `village`, `village-night`, `village-swamp` und `minecarts`, `horde`, `explore` 75 s; `docs/Performance-Verlauf.md`, „Dorf-Sektion (37) gerendert“): Bildrate (116–120 FPS, Ø 8,33–8,62 ms) und Frame-Kosten (8 Chunks höchstens 2,21 ms GPU in crowd, im Dorf höchstens 1,75 ms GPU und 1,72 ms Render-CPU; 16 Chunks 3,41 ms GPU in sprint) erfüllt. p99 ≤ 12,5 ms im Median verfehlt: 16,0 ms (`f406734`) und 16,1 ms (`8d714f0`) über je 54 Läufe, 43 bzw. 44 davon über 12,5 ms, Frame-Kosten-p99 höchstens 2,9 ms, ein fremder Prozess zeitweise bei 80 % CPU: verpasste VSyncs von außerhalb des Spiels wie in 31.3, bei beiden Ständen gleich, kein Befund des Spiels; 16 Chunks 9,3 ms im Median. Spitzenspeicher 8 Chunks: alle Szenarien 325–470 MB, sprint (10 s) 570 MB, Erkundungsflug 568 MB (höchstens 583) erfüllt. 16 Chunks: sprint (10 s) 1 244 MB (`8d714f0` 1 232) weiter verfehlt (Befund oben), **village bei 16 Chunks 837 MB** (`8d714f0` am selben Ort ohne Dorf 827 MB) ebenfalls über 800 MB, bei beiden Ständen, also derselbe Befund des Clients und nicht durch 37 (+10 MB). Kein Tick über 50 ms (höchstens 5,9 ms im Erkundungsflug), keine übersprungenen Ticks. Referenzwerte unverändert.

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

## Headless messen ohne Echtzeit (Etappe 31.3)

Messungen von Server-Kosten laufen nicht in Echtzeit: Die Kosten eines Ticks hängen nicht von der Uhr ab. Die Steuerschnittstelle (`qtcraft-headless --script`) eignet sich dafür nicht, weil ihr Bot in Client-Ticks wartet, also mit 20 Hz; `/tick sprint` oder `/tick rate` beschleunigen nur den Server, der Bot bewegt sich dann je Server-Tick langsamer als im Spiel. Darum:

- **Erkundungsflug:** `QTCRAFT_EXPLORE_MEASURE=300 build/release/tests/test_explore_measure` fliegt dieselbe Strecke wie das Szenario `explore` (5 m alle 5 Ticks in 200 m Höhe, Glasplattform alle 20 Ticks, Saatwert 42, Sicht 8, Speichern im Hintergrund alle 1200 Ticks) und tickt den Server so schnell er kann; Chunks werden wie im Spiel im Hintergrund gelesen, erzeugt und geschrieben (sind welche unterwegs, bekommt der Hintergrund 3 ms vor dem nächsten Tick, damit der Flug sie nicht überholt). Ausgabe: Tick Ø, p99, Maximum und die Phasen der fünf langsamsten Ticks. 6000 Ticks in rund 3 s. Ohne die Variable läuft eine kurze Fassung in jedem Testlauf.
- `scripts/explore-headless.sh` bleibt für den Flug mit Client (Speicher des Prozesses, Wesen im Client) und läuft in Echtzeit (300 s Flug, rund 9,5 min mit Start); für Tick-Kosten nicht verwenden.
- **Alle Server-Szenarien** (seit E-388): `scripts/bench-headless.sh` verschränkt über Builds, mit denselben Szenen wie im Fenster (siehe „Messmethode: headless und Fenster“); `test_scenario_measure` ist dabei die Verallgemeinerung von `test_explore_measure` auf jede Szene aus `bench-common.sh`.
- **Weltöffnung:** `QTCRAFT_WORLD_OPEN_MEASURE=full build/release/tests/test_world_open_measure` (unter einer Sekunde).

## Mehrspieler-Last (Etappe 25)

Referenzwerte für den Server-Tick mit Spielern über TCP (Release, `qtcraft-headless --server` in der flachen Welt, Seed 42, 8 Chunks, nur auf 127.0.0.1; Bots mit `qtcraft-headless --join`, die geradeaus sprinten und springen, alle 4 s einen Block abbauen und eine Chatzeile schreiben; ein Bot als Operator liest alle 4 s die Server-Debugdaten über den Steuerbefehl `debug`). Je 3 Läufe, Mittel über rund 45 s nach dem Beitritt; Einzelwerte und Methode in `docs/Performance-Verlauf.md` (Etappe 25).

| Bots | Tick Ø ms | Tick max ms (Autosave) | Nachrichten Ø ms | Streaming Ø ms | Client-Abgleich Ø ms | Server-CPU | Server-Speicher MB |
|-----:|----------:|-----------------------:|-----------------:|---------------:|---------------------:|-----------:|-------------------:|
| 1 | 0,25 | 6,8 | 0,017 | 0,14 | 0,038 | ≈ 1,2 % eines Kerns | 42 |
| 4 | 0,45 | 9,8 | 0,042 | 0,31 | 0,051 | ≈ 2,4 % eines Kerns | 72 |

Der Mehraufwand pro zusätzlichem laufenden Spieler liegt bei rund 0,07 ms Tick, vor allem Streaming in neue Gebiete und die Eingaben mit 20 Hz. Der schlechteste Tick ist in beiden Fällen der Autosave, nicht das Netz. Für eine spätere Messung mit mehr Spielern lässt sich derselbe Aufbau mit mehr Bots wiederholen; eine Steigerung über diese Werte hinaus ist mit der Phase Nachrichten und Streaming einzugrenzen.
