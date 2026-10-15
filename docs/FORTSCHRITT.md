# Fortschritt QtCraft

Wiederaufnahme-Dokument: Ein frischer Agent ohne Chatverlauf liest zuerst dieses Dokument, dann `QtCraft-Agent-Auftrag.md`, `docs/Entscheidungen.md` und `docs/FRAGEN.md`.

## Aktueller Stand

- **Aktuelle Etappe:** Etappe 31.2 (Entities) ist in `dev` (`789e2d9`, 2026-10-14), davor 31.1 (`d6568ba`) und 30 (`fe0a8c9`); dazu der Nebenstrang GitHub (CI-, Release- und Pages-Workflows, GPL-3.0, Web-Handbuch; `79b76ff`, E-950). Nach dem Projektzeugnis vom 2026-10-14 hat der Mensch den Plan angepasst (E-370): Etappe 43 (Speicher der Übersetzungen) ist beauftragt und kommt vor 31.3; der Vorschlag 44 (Client-Tick-Thread) ist als 46.2 in Etappe 46 aufgegangen, die Nummer 44 ist stillgelegt; neue Etappe 47 (Feinschliff Darstellung); 39 (Veröffentlichung) rückt ans Ende; neuer Vorschlag 48 (TLS im Mehrspieler). **Reihenfolge ab jetzt:** 43, 31.3, 49, 32, 33, 34, 35, 36, 37 (37.1, 37.2), 52, 38, 51, 46 (46.1, 46.2), 40, 41, 47, 45, 50, 39; Vorschläge 42 und 48. Nach `main`/`master` merged nur der Mensch.
- **Nächster Schritt:** Etappe 43 (Speicher der Übersetzungen) auf `feature/43-translation-memory` (Branch noch nicht angelegt), von der `dev`-Spitze. Erst headless messen (Speicher und Ladezeit der Übersetzungen, Spitzenspeicher eines headless Laufs), dann die Namen der Teile-Familien zur Laufzeit aus den `%1`-Vorlagen zusammensetzen, Handbuch-, Creative- und Web-Handbuch-Suche finden dieselben Namen, dieselbe Messung nachher. Danach 31.3 (Chunks entladen) auf `feature/31-3-chunk-unloading`; deren Pflicht-Benchmark misst den Speicher bei 16 Chunks gegen das Ziel ≤ 800 MB (offener Befund in `docs/Performance.md`). **Reihenfolge am Etappen-Ende:** unabhängige Prüfung und Fixes → Benchmark (GUI-Messungen derzeit ausgesetzt, siehe Offene Benchmarks; jede Messung prüft auch die absoluten Ziele) → einmal voller `verify.sh` → Merge. `FORTSCHRITT.md` bei jedem Zwischenschritt aktualisieren und committen (Abbrüche durch Limits). Projektname: „QuaderCraft“ vorgemerkt, vor der Umsetzung (Etappe 39) fragen.
- **Laufender Stand (Stand 2026-10-15):** Etappe 43 per Squash-Merge in `dev` (Gesamtgrenze `maxTranslations`, E-371; Prämisse korrigiert: das Basisspiel hat nur ~1 300 Übersetzungen je Sprache; Prüfung, Benchmark ohne Regression, voller `verify.sh` grün: 156/153/153 Tests, 70 min). Nächster Schritt: Etappe 31.3 (Chunks entladen) mit Pflicht-Benchmark Speicher sprint bei 8 Chunks (≤ 600 MB, zuletzt 604–612 MB) und 16 Chunks (≤ 800 MB, zuletzt 1 386 MB) sowie dem Folgepunkt `QcEntities::settle` nur bei Änderungen (aus der Messung von 31.2).
- **GUI-Benchmarks wieder erlaubt (Mensch 2026-10-14 abends):** Die Aussetzung vom 2026-10-13 ist aufgehoben. Benchmarks und Läufe mit echtem Fenster (`bench.sh`, `bench-ab.sh`, Showcase-Rundgang, `release.sh`) dürfen wieder laufen, immer mit der Benchmark-Sperre und nie parallel zu Builds oder Tests; Etappen-Ende wieder: Prüfung und Fixes → Benchmark (inkl. absoluter Ziele) → voller `verify.sh` → Merge. Sagt der Mensch erneut, dass er den Rechner braucht, wieder aussetzen und unter „Offene Benchmarks“ sammeln.
- **Benchmark-Sperre:** `~/.cache/qtcraft/bench.lock` (Verzeichnis mit `owner`). `scripts/bench-ab.sh` legt sie selbst an und entfernt sie am Ende; andere Messreihen von Hand genauso. Solange sie existiert, startet keine Session Builds, Tests oder Verify-Läufe (laufende dürfen enden). Messungen sind wieder erlaubt (die Benchmark-Session hat am 2026-10-12 abends gemessen); `bench-ab.sh` rotiert die Reihenfolge der Builds je Runde (Reihenfolge-Effekt, E-331).
- **Offene Benchmarks** (Stand 2026-10-14): **47 GUI** (nach der Etappe: Bildzeiten und GPU mit anisotroper Filterung und den übrigen Darstellungsänderungen, verschränkt gegen den Stand davor, showcase und sprint). **Absolute Ziele (E-370):** Jede Messung prüft neben alt gegen neu auch die Ziele aus `docs/Performance.md` („Ziele“); offen ist dort der Befund „Spitzenspeicher sprint, 16 Chunks 1 384 MB statt ≤ 800 MB“ (nach 43 und 31.3 neu messen). Gemessen: 31.2 gerendert (2026-10-14, Branch `feature/bench-31-2`, `d6568ba` gegen `789e2d9`, 6 + 6 + 6 rotierte Runden: keine Regression; Tick-Abstände in drei Szenarien sind eine Lage des Server-Threads, dritte Reihe und headless zeigen 31.2 schneller; echte Mehrkosten nur `settle` in der Phase Chunk-Laden, 0,01–0,03 ms je Tick, Folgeaufgabe für 31.3; Autosave mit vielen Entities nur headless, kein Fenster-Szenario), 29.1, 29.2, 30 und 31.1 gerendert (2026-10-14), 26 bis 28 und die Leere-Zone (2026-10-12/13; Einzelheiten im Archiv). Für Vergleiche: crowd nur vergleichbar, wenn `entities` und `server_mobs` übereinstimmen, gegen Stände vor 30 nur über `flat-crowd`; p99/CPU auf diesem Rechner zweigipflig, mindestens 6 rotierte Runden.
- **Umgesetzt (Etappe 30):** E-351 bis E-355. `/summon` zählt nur gerufene wache Wesen (`summonLimit`, Standard 64, 0 = aus) – die „verschwindenden“ crowd-Kreaturen waren abgelehnte Summons; kein Despawn ohne Spieler im Spiel; `QcBodyFit` setzt gerufene Wesen frei (auch neben Zäunen/Mauern, nie aufs Dach, nie unter y 0, nicht in Lava/Magma/Feuer) und schiebt Wesen aus Blöcken (sonst Ersticken); Physik: Rundungs-Überlappungen bis 1e-7 m zählen als Berührung (E-355, auch Spieler); farm-seeds zählt nur bis 32 m (kein Bug); crowd gegen Stände vor 30 nur über `flat-crowd` vergleichen; `QcMobManager` nach Aufgaben geteilt (`QcMobUpkeep`, `QcMobSteering`). Prüfung ohne Blocker, alle Befunde behoben; voller `verify.sh` grün (3908 s); `test_survival_scenario` unter TSan 5/5. Offen: Flag „gerufen“ wird nicht gespeichert; GUI-Benchmark von crowd mit den neuen Zahlen; Körper breiter als 1 m (nur Mods) an Zaunpfosten nicht geprüft.
- **Umgesetzt (Etappe 31.1, Block-Entities):** E-356 bis E-362. Register `QcBlockEntities` (Tabellen je Art mit Chunk-Index) für Container inkl. Trichter, Maschinen, Setzlinge, Schaltungs-Speicher und geplante Ticks; ein Weg für Setzen (`placed()`), Ankommen, Neu-Generieren und Kolben (`relocate`, auch geplante Ticks aller Blöcke); zentrale Prüfung `QcBlockEntityChecks` (in `/worldinfo` und BENCH-Zeile); Seitendateien `blockentities/r.x.z.qcb` je Region über Zwischennamen; Weltformat 3, ältere Welten werden übersetzt abgelehnt (auch im Hauptmenü); `QcSectionMarks` mit 5 Eigenschaften; 24 weitere Grenzen in `QcServerLimits`; Neugliederung (Items/Plants, BlockScreens/Machines, `QcMachineRules`, `QcHopperQueue`); Goldendatei alt/neu als Test. Headless: Tick flach, Speichern +4/+8 %, Öffnen +7 %; gerendert keine Regression außer Signal-Phase an zwei Stationen (+0,013/+0,027 ms, E-362). Prüfung mit 1 Blocker und 7 Soll-Befunden, alle behoben; voller `verify.sh` grün (3915 s). Offen für 31.2: Neugliederungsliste E-361, alte Lese-Pfade (`QcPlayerStorage::migratedPath`, `QcAccessList` Format 1); für 31.3: Chunk-Index/Lesen bei Bedarf, ganzes Speichern konsistent (Manifest/Journal), ausstehende Schaltungsarbeit und Trichter-Weckliste je Chunk.
- **Umgesetzt (Etappe 31.2, Entities):** E-363 bis E-369. Gemeinsame Basis `QcEntity` mit Art und Register `QcEntities`: eine Synchronisierung, eine Ruheregel (`settle`, jeden Tick nach dem Chunk-Laden), `displace` für Kolben (jetzt auch fallende Blöcke), `overlapsLiving` nur über Lebewesen; Spieler nur für Id, Box, Schub und Synchronisierung im Modell (Vorhersage unverändert); Protokoll 32 mit Art-Byte; Entities je Region `entities/r.x.z.qen` (Version 2, Sortierschlüssel = Id), Weltformat 4, alte Welten und alte Lese-Pfade entfernt; übersetzte Meldung mit Ausweg bei beschädigten Dateien; zentrale `QcEntityChecks` (Tests, `/worldinfo`, BENCH); Neugliederung der ganzen E-361-Liste, `QcServerState` als Klasse (Uhr und Client-Liste für Teile nur lesbar, Rest offen benannt in E-367); `Architektur.md` gekürzt, Protokoll-Verlauf in E-368; untätige kalte Maschinen übersprungen (E-369). Headless: Tick flach, Öffnen +6 %, erstes Speichern +7 %, zweites Speichern +35 % (2,66 ms, Dateien je Region; Prüfung in 31.3). Prüfung mit 1 Blocker und 6 Soll-Befunden, alle behoben; voller `verify.sh` grün (4056 s). Gerendert gemessen am 2026-10-14 (keine Regression, `docs/Performance-Verlauf.md`, „Etappe 31.2: gerenderte Messung“). Offen für 31.3: `QcEntities::settle` nur bei Änderung der geladenen Chunks statt jeden Tick (0,01–0,03 ms), Nehmen/Zurücklegen je Chunk in `QcEntities`, Chunk-Index und Lesen bei Bedarf, Speichern über alle Dateien konsistent, engere Schnittstellen statt `QcServerState &`.
- **Release:** `scripts/release.sh` erzeugt `deploy/QtCraft.app` (≈ 194 MB, universell arm64/x86_64, ab macOS 13.3), `deploy/QtCraft-0.1.0-macOS.zip` (≈ 80 MB) und `.dmg` (≈ 85 MB); nur ad hoc signiert, Erststart auf anderen Macs siehe README. Zwei Builds in derselben Minute bekommen dieselbe Build-Nummer (harmlos).
- **Arbeitsweise ab Etappe 21 (Showcase-Welt als Definition of Done):** Jede neue Funktion erweitert die Showcase-Welt (vom Menschen am 2026-10-09 festgelegt, `QtCraft-Agent-Auftrag.md` Abschnitt 3 „Vorgehen“): neue Blöcke, Zustände, Wesen, Items und Baum-Features erscheinen dort automatisch, sichtbares Verhalten bekommt eine Demo als Struktur-Datei (`structures/*.json` mit `showcase`). Zur Abnahme jeder Etappe gehören der Screenshot-Rundgang `scripts/showcase-tour.sh` (betroffene Stationen, Belege in `docs/screenshots/`) und das Benchmark-Szenario `showcase`.
- **Arbeitsweise für Skript-Läufe:** Spiel-Läufe brauchen ein sichtbares Fenster: `scripts/screen-guard.sh` hält den Bildschirm wach und wartet bei gesperrtem Bildschirm (Benchmark, Profil, Showcase-Rundgang, Verify-Smoke). Immer `--screen builtin --background` (eingebautes Display, kein Fokus, kein Maus-Fang), Ton stumm über `settings.ini`, temporäre Daten in einem Temp-Ordner und am Ende löschen (Festplattenplatz). **Verifikation gezielt (vom Menschen am 2026-10-09 angemerkt, `verify.sh` dauert inzwischen sehr lange):** während der Arbeit nur Debug-Build, betroffene Tests und statische Analyse der geänderten Dateien (`scripts/verify.sh --quick [--tests <regex>]`, seit Etappe 19; Debug-Build, Debug-Tests, Analyse der gegen `dev` geänderten C++/QML-Dateien, rund 3 min; `--sanitizers asan|tsan` ergänzt eine Sanitizer-Konfiguration; Zeiten pro Schritt am Ende); der volle Lauf mit ASan/TSan, Release und Smoke (`scripts/verify.sh` = `--full`) einmal vor dem Merge; unabhängige Prüfer bauen nur Debug neu und verlassen sich für Sanitizer auf das Log des vollen Laufs, außer bei Threading-Änderungen. **Zwei Stufen für TSan (Mensch 2026-10-14, E-370):** Während einer Etappe läuft TSan nur auf den nebenläufigen Tests: `scripts/verify.sh --tsan-concurrent [--tests <regex>]` (= `--quick` plus TSan-Build mit `ctest -L concurrency`; Label-Liste `qc_concurrency_tests` am Ende von `tests/CMakeLists.txt`: Server-Host und Generierungs-Pool, Client-Session, Mesh-Scheduler, Texturatlas, TCP und Mehrspieler, Headless samt Signal-Watcher, Audio, Offscreen-Spiel; neue Tests mit Threads, Server-Host oder Prozessen dort eintragen). Der volle Lauf vor dem Merge bleibt unverändert und prüft alle Tests unter TSan.
- **Frühere Etappen:** Ausführliche Umsetzungsnotizen der Etappen 9 bis 29.2 (samt Nachträgen, Hörtest und kleinen Befunden aus Etappe 11) stehen in `docs/FORTSCHRITT-Archiv.md`.

## Zusätzliche Festlegungen des Menschen (2026-10-09)

- Neue Etappen 20 bis 30 beauftragt (Feinschliff, Showcase-Welt, Charakter-Vorschau, Rüstung, Server-Befehle, Multiplayer, Creative, Landwirtschaft, HD-Texturpaket, Signal Dust, Dörfer; alte Planung, Dörfer heute Etappe 37); Details in `docs/Naechste-Schritte.md`.
- **Texturen:** Die Festlegung „keine Downloads" von 2026-10-07 wird **nur für Etappe 28 (HD-Texturpaket)** gelockert: freie Quellen (CC0/Public Domain oder freizügige Lizenz, keine Minecraft-Texturen oder abgeleitete Packs) und selbst Erstelltes sind erlaubt, Nachweis in `docs/ASSETS.md` und `CREDITS.md`.
- **Multiplayer:** früh, LAN und Internet, minimale Authentifizierung (Name, optionales Passwort, Installationsschlüssel), maximale Spielerzahl beim Server einstellbar. Server-Befehle und Wartungswerkzeuge gelten auch im Einzelspieler.
- **Schaltmaterial heißt „Signal Dust"** (nicht „Redstone"). Dörfer ohne Raids. Dorf-Handel, Berufe und Wegfindung im Dorf entscheidet der Agent.
- Jede neue Funktion erweitert ab Etappe 21 die Showcase-Welt und wird dort per Screenshot abgenommen.

## Vom Menschen getroffene Festlegungen (2026-10-07)

- Squash-Merge nach `dev` durch den Agenten nach grüner unabhängiger Verifikation ist erlaubt. Nie nach `main`/`master`.
- Commit-Messages ohne jede Attribution (auch kein `Co-Authored-By`).
- Texturen ausschließlich prozedural/selbst erstellt, keine Downloads.
- Ab Etappe 11: Performance-Check pro Etappe (`scripts/bench.sh`, Baseline und Verlauf in `docs/Performance-Verlauf.md`, Schwelle 10 %), siehe `QtCraft-Agent-Auftrag.md` Abschnitt 3. Baseline-Messung zu Beginn von Etappe 11 auf `dev`.
- Ab 2026-10-09: Größen von Funktionen, Klassen und Dateien begrenzt (Funktionen Ziel 40/Grenze 80 Zeilen, ≤ 5 Parameter, Verschachtelung ≤ 4, kognitive Komplexität ≤ 25; Dateien Ziel 500/Grenze 800 Zeilen, Header ≤ 300; Klassen mit einer Verantwortung), siehe `QtCraft-Agent-Auftrag.md` Code-Stil. Bestand bei Festlegung: 6 Dateien über 800 Zeilen (`QcGameController.cpp` 1786, `QcServerGameplay.cpp` 1680, `QcContentParser.cpp` 1608, `QcHandbookBuilder.cpp` 1456, `QcContentRegistry.cpp` 1117, `QcControlInterface.cpp` 885) – Aufteilen und Durchsetzen in der statischen Analyse ist ein eigener Schritt nach Etappe 18.
- Wiederaufnahme nach Token-Limit: Der Mensch startet `/loop` mit „weiter mit QtCraft laut docs/FORTSCHRITT.md".

## Etappen

| # | Etappe | Status | Datum | Notiz |
|---|--------|--------|-------|-------|
| 0 | Repo-Grundgerüst | erledigt | 2026-10-07 | `.gitignore`, Doku-Skelett |
| 1 | Gerüst und Spike | erledigt | 2026-10-07 | QRhi-Renderer gewählt, Verifikation PASS |
| 2 | Durchstich Client/Server | erledigt | 2026-10-07 | Verifikation nach Nachbesserung PASS (zwei offene Befunde S1/S2) |
| 3 | Weltgenerierung | erledigt | 2026-10-07 | Verifikation PASS, Befunde nach Etappe 4 übernommen |
| 4 | Interaktion und Persistenz | erledigt | 2026-10-07 | Verifikation nach Nachbesserung PASS |
| 5 | Gegner und Gesundheit | erledigt | 2026-10-08 | Verifikation nach Nachbesserung PASS |
| 6 | Crafting und Items | erledigt | 2026-10-08 | Verifikation nach Nachbesserung PASS |
| 7 | Modding | erledigt | 2026-10-08 | Verifikation nach drei Nachbesserungsrunden PASS (bösartige Mod-Dateien, Grenzen des Clients) |
| 9 | Item-Entities und Setzlinge | erledigt | 2026-10-08 | Verifikation nach zwei Nachbesserungsrunden PASS (Raster beim Stopp, Grenzen nach dem Laden, ruhende Items) |
| 8 | Politur und Abschluss | erledigt | 2026-10-08 | Einstellungen, deutsche Übersetzung, Benchmark (≈ 120 FPS bei 8 Chunks), Durchspiel-Test, README, Abschlussbericht; Mod-Lader nach mehreren Prüfrunden vereinfacht und gehärtet |
| 10 | Charaktermodell und Ansichten | erledigt | 2026-10-08 | Verifikation nach einer Nachbesserungsrunde PASS (Geist-Spieler nach Aussehenswechsel, Mod-Outfits für Spieler, Kamera an Pflanzen) |
| 11 | Ofen, Schmelzen und Fortschritt | erledigt | 2026-10-08 | Verifikation PASS, vier kleine Befunde behoben (schnelle Maschinen, spätes Öffnen, Screenshot-Hook, verdeckte Rezepte); Performance ohne Regression (A/B) |
| 12 | Sound und Musik | erledigt | 2026-10-08 | Verifikation PASS, sechs kleine Befunde behoben; Hörtest durch den Menschen; Performance verschränkt gemessen ohne Regression (Speicher +5–7 %) |
| 13 | Bauteile und Blocklicht | erledigt | 2026-10-08 | Verifikation nach einer Nachbesserungsrunde PASS (u. a. inkrementelles Blocklicht, Updates an Chunkgrenzen, Fackel in der Hand); Regressionen bei Speicher und Tick per Profiling behoben (E-051) |
| 14 | Lampe, Flüssigkeiten und Weltdetails | erledigt | 2026-10-09 | Verifikation PASS nach zwei Nachprüfungen (Lava im normalen Spiel über Höhlen-Pools, Ofen mit leerem Eimer, versiegelte Pools); Regressionen per Profiling behoben (E-062) |
| 15 | Spielhandbuch | erledigt | 2026-10-09 | Verifikation PASS, acht Befunde behoben (Lichtangabe, „?“-Knopf beim Ablegen, Platzhalter statt fester Zahlen), Nachprüfung PASS; Performance ohne Regression (+2 % Speicher) |
| 16 | Profiling und Optimierung | erledigt | 2026-10-09 | Verifikation PASS; Frame-Kosten pro Thread und GPU messbar, kompakte Eckpunkte (−7 bis −13 % Speicher), Entity-Meshing −35 bis −50 %, Ziele für 8/12/16 Chunks erreicht (E-072), `perf-check.sh`; Fenster-Hooks `--screen`/`--background` |
| 17 | Release-Pipeline und App-Bundle | erledigt | 2026-10-09 | Verifikation PASS, sechs kleine Befunde behoben, Nachprüfung PASS; Ordnerrechte der installierten Mods nachgezogen; `release.sh` mit Selbstprüfung inkl. Rosetta-Start |
| 18 | Eingabe und Overlays (Fehlerbehebung) | erledigt | 2026-10-09 | Mac-taugliche Tasten (V/H/I, Sprinten Option, keine Cmd-/Control-Standards, Cmd-Kürzel behalten Systembedeutung), Neubelegung in den Einstellungen, Unterfenster schließen mit ihrem Menü, erzwungenes Dunkelschema (heller Modus lesbar); Verifikation FAIL → behoben, zwei Nachprüfungen PASS. Offen: Option-Sprinten und `/`-Chat an echter Tastatur prüfen; Dunkelschema unter Windows/Linux ungeprüft |
| 19 | Aufteilen zu großer Dateien, Größengrenzen durchsetzen | erledigt | 2026-10-09 | `verify.sh --quick/--full`; alle Dateien/Funktionen innerhalb der Grenzen, durchgesetzt per clang-tidy und Dateilängen-Prüfung (E-088); Verhalten per Vergleichsläufen gleich; voller Lauf grün; unabhängige Prüfung bestanden (Build/Tests und Code-Review; Befunde behoben: Farbfelder im Charakter-Screen nach dem Laden, Event-Filter beim Beenden) |
| 19b | Ordnerstruktur und Lesbarkeits-Durchgang | erledigt | 2026-10-09 | Ordner nach Aufgaben (E-089); Lesbarkeits-Durchgang über alle Dateien; voller Verify ALL GREEN (1301 s); unabhängige Prüfung PASS |
| 20 | Feinschliff und Multiplayer-Voraussetzungen | erledigt | 2026-10-10 | drei parallele Teile (Server, Client, Welt), E-090 bis E-104; voller Verify ALL GREEN; unabhängige Prüfung (Befund Ratenbegrenzung Shift-Klick behoben); Performance innerhalb der Schwelle nach Korrektur von Takt und Pflanzen-Tick |
| 21 | Showcase-Welt | erledigt | 2026-10-10 | dazu flache und leere Welt (vom Menschen gewünscht), E-219 bis E-224; unabhängige Prüfung (Beschriftungslänge u. a. behoben); Performance innerhalb der Schwelle; voller Verify ALL GREEN |
| 21b | Debug-Anzeige und Laufzeit-Diagnose | erledigt | 2026-10-10 | dazu Offscreen-Screenshots (E-225, E-229, E-230); unabhängige Prüfung (Debug-Abo, Multiplayer-Leck, Tastenkonflikt u. a. behoben); Performance ohne messbare Kosten; voller Verify ALL GREEN |
| 22 | Charakter-Vorschau im UI | erledigt | 2026-10-10 | E-231 bis E-233; unabhängige Prüfung (Namen/Surrogate, Vorlagen-Sprache, Vorschau-Aktivität, Atlas-Speicher u. a. behoben); Vorschau-Kosten akzeptiert; voller Verify ALL GREEN |
| 23 | Rüstung | erledigt | 2026-10-10 | dazu Rüstungsständer und Umhang (vom Menschen gewünscht), E-234 bis E-242; unabhängige Prüfung (Lese-Grenze, verlorene Rüstung, Ständer-Schläge u. a. behoben); Performance innerhalb der Schwelle; voller Verify ALL GREEN |
| 24 | Server-Befehle und Wartungswerkzeuge | erledigt | 2026-10-11 | dazu `/tick` (vom Menschen gewünscht), E-243 bis E-250; unabhängige Prüfung (Koordinaten-Überlauf, Tempo bei langsamer Rate, Bestätigung nach /deop u. a. behoben); Performance unverändert; voller Verify ALL GREEN |
| 25 | Multiplayer | erledigt | 2026-10-11 | Punkt 25; in `dev` d28dd89 |
| 26 | Creative-Modus | erledigt | 2026-10-12 | Punkt 26; E-282 bis E-287 |
| 27 | Landwirtschaft und Nahrung | erledigt | 2026-10-12 | Punkt 27; E-289 bis E-296 |
| 28 | HD-Texturpaket | erledigt | 2026-10-12 | Punkt 28; freie Quellen erlaubt; vorher Block- und Rezept-Erweiterung in `dev`; Benchmark 2026-10-13 (E-332) |
| 29 | Signal Dust (Schaltungen) | erledigt (29.1, 29.2) | 2026-10-13 | Punkt 29; 29.1 Leitung, Bauteile, Kolben (E-333 bis E-341); 29.2 Trichter, Komparator, Beobachter, Spender/Werfer, `QcServerLimits` (E-342 bis E-349); der offene Teil 29.3 (Schienen und Loren) ist heute Etappe 32 |
| 30 | Fehler im Benchmark `crowd` (und fehlende Saat in `farm-seeds`) | erledigt | 2026-10-13 | vom Menschen 2026-10-13; Punkt 30; headless untersuchen, Test rot vor dem Fix; Branch `feature/30-crowd-creatures` |
| 31 | Umbau: Block-Entities, Entities, Chunks entladen | 31.1, 31.2 erledigt; 31.3 nach 43 | 2026-10-14 | vom Menschen 2026-10-13; Punkt 31; 31.1 Block-Entities (`d6568ba`), 31.2 Entities (`789e2d9`); 31.3 eigene Zeile unten |
| 43 | Speicher der Übersetzungen | erledigt | 2026-10-15 | vom Menschen 2026-10-14 beauftragt (E-370); Befund: Basisspiel nur ~1 300 Einträge je Sprache, die 700–870 MB betrafen eine Mod an den Lader-Grenzen; umgesetzt Gesamtgrenze `maxTranslations` (E-371), Laufzeit-Zusammensetzung der Namen vom Menschen gestrichen; Branch `feature/43-translation-memory` |
| 31.3 | Chunks auf dem Server entladen | geplant | | Punkt 31 (früher 32); Benchmarks Pflicht inkl. Speicher bei 16 Chunks gegen das Ziel ≤ 800 MB; Schaltungs-Gruppen ganz oder gar nicht geladen, lange Leitungen laden ihr Ziel vorübergehend nach, `/forceload`, Spawn-Chunks, Chunk-Lader; Grenzwerte in `QcServerLimits` abschließen; dazu (E-370) die `static_cast`-Downcasts nach Art entscheiden und dokumentieren |
| 49 | Code-Stil Qt/std: Besitz, Zustände, `[[nodiscard]]` | geplant | | vom Menschen 2026-10-15 nach der Stil-Analyse beauftragt (E-379); Punkt 49; direkt nach 31.3; Hand-`delete` durch Besitz im Typ, String-Zustände als `enum class`/`Q_ENUM`, `[[nodiscard]]` auf `QcResult`/`QcStatus` |
| 32 | Schienen und Loren | geplant | | früher 29.3; Punkt 32; auf dem Entity-Modell aus 31.2 und den Block-Entities aus 31.1 |
| 33 | Licht auf dem Server | geplant | | vom Menschen 2026-10-13; Punkt 33; Block- und Himmelslicht pro Section auf dem Server, gemeinsamer Code mit dem Client oder Übertragung (entscheiden), Benchmark Tick-/Ladezeiten; Vorsorge für 45 |
| 34 | Feuer | geplant | | früher 29b; vom Menschen 2026-10-12 eingefügt; Punkt 34; Garantien gegen Weltbrand, Benchmark Waldbrand Pflicht |
| 35 | Wetter | geplant | | früher 29c; vom Menschen 2026-10-12 eingefügt; Punkt 35; Regen/Gewitter/Schnee, Blitz nur innerhalb der Feuer-Garantien |
| 36 | Erfahrung und Lapislazuli | geplant | | früher 29d; vom Menschen 2026-10-13 vor die Dörfer gezogen; Punkt 36; Grundlage für Handel (37) und Verzauberung (41) |
| 37 | Wegfindung, Dörfer und Dorfbewohner | geplant | | früher 30; Punkt 37; 37.1 Wegfindung (vom Menschen 2026-10-13: A* mit Budget, Abgründe/Wasser meiden, Ausweichen, Mob-Nahkampf mit Sichtlinie), 37.2 Dörfer und Dorfbewohner; 37.2 mit Dorf-Stilen je Biom (Pflicht Ebene und Wüste wie Vanilla, Baumhaus-Dorf im Dschungel prüfen, vom Menschen 2026-10-15), Vermehrung der Dorfbewohner und Eisengolem im Design berücksichtigen |
| 52 | Mehr Wesen | geplant | | vom Menschen 2026-10-15 eingeplant; Punkt 52; nach 37, vor 38; Skelett-Bogenschütze (Projektile), Spinnen und Höhlenspinnen, Enderman, Kühe, Silberfische, Wölfe und Hunde, Ozelots und Katzen (Zähmen), Eisengolem |
| 38 | Monster-Spawner und Minenschächte | geplant | | früher 30b; vom Menschen 2026-10-12 eingefügt; Punkt 38; nach 32 (Schienen), Oberflächen-Zugänge, Treppenhäuser, Spawner mit Gängen zu Höhlen |
| 51 | Strukturen an der Oberfläche | geplant | | vom Menschen 2026-10-15 eingeplant; Punkt 51; nach 38; Wüstentempel (mit Chance als Mittelpunkt eines Wüstendorfs, im Keller statt TNT ein Sandgolem, der bei Kontakt mit einem Wasser-Quellblock zu Sand und Sandstein-Kopf zerfällt und im Keller unter Bedingungen neu spawnt – erneuerbarer Sand, Sandfarm), Dschungeltempel, Ruine um kaputten Portalrahmen, Türme an Küsten (dort die Sprengfalle; Sprengstoff/Explosion fehlen noch), Brunnen/Oasen, Hexenhütte |
| 46 | Client nach Zuständigkeit gliedern, Session-Tick auf eigenem Thread | geplant | | vom Menschen 2026-10-13 eingeplant, am 2026-10-14 mit dem früheren Vorschlag 44 zusammengelegt (E-370); Punkt 46; 46.1 Client nach Zuständigkeit neu gliedern (auch Lebensdauer der Teile von `QcGameController`), 46.2 Session-Tick auf eigenem Thread; vor 40 (Gamepad) |
| 40 | Gamepad-Unterstützung | geplant | | früher 33; vom Menschen 2026-10-12 hinten angehängt; Punkt 40; SDL3 als Submodule (einmal gebaut, gemeinsamer Cache), Hotbar plus Radialmenü; nach 46.2 |
| 41 | Verzauberung und Amboss | geplant | | früher 34; vom Menschen 2026-10-13 hinten angehängt; Punkt 41; geteilt in 41.1 Zaubertisch und 41.2 Amboss; Erfahrung kommt aus 36; Vorschau je nach Bücherregalen; BUD-Varianten als eigene Blöcke, Umwandlung am Amboss (bis 64 auf einmal) |
| 47 | Feinschliff Darstellung | geplant | | vom Menschen 2026-10-14 eingeplant (E-370, Projektzeugnis); Punkt 47; Beschriftungen nahe der HUD, anisotrope Filterung, Arm, Mindesthelligkeit, ruhigeres Laub; Screenshots vorher/nachher, GUI-Benchmark offen |
| 45 | Realistischer Render-Modus (Wasser und Licht) | geplant | | vom Menschen 2026-10-13 eingeplant, seit 2026-10-14 vor 39 (E-370); 45.1 HDR/Nachbearbeitung, 45.2 Schatten, 45.3 Wasser, 45.4 Material-Kanäle; Vorsorge in 33; GUI-Benchmarks Pflicht |
| 50 | Code-Stil Qt/std, Rest | geplant | | vom Menschen 2026-10-15 festgehalten (E-379); Punkt 50; ganz am Ende vor 39; Logging-Kategorien zentral, mehr `Q_ASSERT`, Detach-freie Lesezugriffe, Sprache der Registry-Fehler, verworfene Warnungen |
| 39 | Veröffentlichungs-Vorbereitung (GitHub, GPLv3) | geplant, teils vorgezogen | | früher 31; seit 2026-10-14 am Ende der Reihenfolge (E-370); Punkt 39; CI-/Release-/Pages-Workflows, Lizenz und Web-Handbuch schon auf `dev` (`79b76ff`); offen: Name (fragen), Marken, Hörtest, Windows (VM) und Linux (CI), Notarisierung, erster CI-Lauf auf GitHub, Commit-E-Mail umschreiben |
| 42 | Zustandsbudget | Vorschlag | | aus dem Umbau-Auftrag (Teil 3), nicht beauftragt; Darstellungs-Verbindungen beim Meshen ableiten, mindestens Budget-Meldung beim Laden; `docs/Naechste-Schritte.md` F |
| 48 | TLS-Verschlüsselung im Mehrspieler | Vorschlag | | aus dem Projektzeugnis 2026-10-14 (E-370), nicht beauftragt; `docs/Naechste-Schritte.md` F |
| 44 | (stillgelegt) | zusammengelegt in 46.2 | 2026-10-14 | früher Vorschlag „Client-Tick auf eigenem Session-Thread“; die Nummer wird nicht wieder vergeben (E-350, E-370) |

Die Etappentabelle ist nach der Reihenfolge der Arbeit sortiert; die Nummern sind feste Kennungen (E-350, `QtCraft-Agent-Auftrag.md` Abschnitt 3 „Vorgehen“), ein späterer Einschub bekommt die nächste freie Nummer.

## Alte und neue Etappen-Nummern

Umstellung am 2026-10-13 (Auftrag des Menschen „Architektur-Umbau und neue Etappen-Nummerierung“, E-350). Abgeschlossene Etappen behalten ihre Namen; Git-Historie, Branches und Commits sind nicht umgeschrieben. Nur die nie begonnenen geplanten Nummern wurden neu vergeben.

**Bisherige Etappen (unverändert, mit Branch bzw. Commit auf `dev`):**

| Etappe | Name | Branch / Commit |
|---|---|---|
| 0 | Repo-Grundgerüst | `8225c3e` |
| 1 | Gerüst und Spike | `feature/01-scaffold-spike`, `17dbc69` |
| 2 | Durchstich Client/Server | `feature/02-client-server`, `f2f1968` |
| 3 | Weltgenerierung | `feature/03-worldgen`, `57bfc0b` |
| 4 | Interaktion und Persistenz | `feature/04-interaction`, `66baf19` |
| 5 | Gegner und Gesundheit | `feature/05-mobs`, `3cf9bcb` |
| 6 | Crafting und Items | `feature/06-crafting`, `c3d0904` |
| 7 | Modding | `feature/07-modding`, `2417bbb` |
| 8 | Politur und Abschluss | `feature/08-polish`, `f013918` |
| 9 | Item-Entities und Setzlinge | `feature/09-item-entities`, `7738aa3` |
| 10 | Charaktermodell und Ansichten | `feature/10-character`, `f641052` |
| 11 | Ofen, Schmelzen und Hunger | `feature/11-furnace`, `412cd7c` |
| 12 | Sound und Musik | `feature/12-sound`, `3e5ff8d` |
| 12b | Mausblick über Systemdeltas (macOS) | `feature/12b-mouse-look`, `a361a5b` |
| 13 | Bauteile und Blocklicht | `feature/13-parts-light`, `f6b3baf` |
| 14 | Lampe, Flüssigkeiten und Weltdetails | `feature/14-liquids-details`, `0b4679e` |
| 15 | Spielhandbuch | `feature/15-handbook`, `9d0e81c` |
| 16 | Profiling und Optimierung | `feature/16-profiling`, `49840a3` |
| 17 | Release-Pipeline und App-Bundle | `feature/17-release`, `0416cd2` |
| 18 | Eingabe und Overlays (Fehlerbehebung) | `feature/18-input-overlays`, `fb888e4` |
| – | Weltgenerierungs-Politur | `feature/worldgen-polish`, `a5c1657` (E-085 bis E-087) |
| 19 | Aufteilen zu großer Dateien, Größengrenzen | `a5b0217` |
| 19b | Ordnerstruktur und Lesbarkeits-Durchgang | `25c9fd3` |
| 20 | Feinschliff und Multiplayer-Voraussetzungen | `94064cc` |
| – | Block- und Rezept-Erweiterung | `feature/blocks-recipes`, `0b6d0b4` (E-200 bis E-217), Benchmark `dfe840a` |
| 21 | Showcase-Welt (dazu flache und leere Welt) | `61d56c7` |
| 21b | Debug-Anzeige und Laufzeit-Diagnose | `78618f4` |
| 22 | Charakter-Vorschau im UI | `c8b3f27` |
| 23 | Rüstung (Rüstungsständer, Umhang) | `2333efd` |
| 24 | Server-Befehle und Wartungswerkzeuge | `545237f` |
| 25 | Multiplayer | `feature/25-multiplayer`, `d28dd89` |
| 25b | Bot-Clients über TCP und Mehrspieler-Menü (in 25 enthalten) | `feature/25b-bots` |
| 25c | Feinschliff: Tooltips und Steuerelemente-Palette, Sprint-Sichtfeld, dedizierter Server im Bundle | `feature/25c-polish`, `9075fec` |
| 26 | Creative-Modus | `feature/26-creative`, `48249aa` |
| 26b / Leere-Zone | Leere-Zone unter der Welt (Nachtrag nach 26) | `feature/26-void-zone` bzw. `feature/26b-void-zone`, `9666133` (E-288) |
| 27 | Landwirtschaft und Nahrung | `feature/27-farming`, `7c7c924` |
| 28 | HD-Texturpaket | `feature/28-texture-pack`, `91e78b6`; Benchmark `feature/bench-28`, `234529c` |
| – | Nachgeholte Benchmarks 26 bis 27 | `feature/bench-2026-10`/`feature/bench-merge`, `d0c905c` |
| 29 | Signal Dust (Schaltungen) | Teil-Etappen 29.1 und 29.2 |
| 29.1 (in älteren Texten 29a) | Signal Dust Teil 1: Leitung, Bauteile, Kolben | `feature/29a-signal-dust`, `393c1e7` |
| 29.2 | Signal Dust Teil 2: Trichter, Komparator, Beobachter, Spender/Werfer | `feature/29-2-signal-machines`, `3f29c94` |
| – | Agenten-Orientierung (CLAUDE.md, Code-Karte, clangd) | `feature/code-map`, `f6930b8` |

**Offene Etappen: alt → neu:**

| Alt (geplant) | Inhalt | Neu |
|---|---|---|
| – | Fehler im Benchmark `crowd` (und fehlende Saat in `farm-seeds`) | 30 |
| – | Umbau Block-Entities / Entities (Befunde A, B, C, E) | 31.1, 31.2 |
| 32 | Chunks auf dem Server entladen | 31.3 |
| 29.3 | Schienen und Loren | 32 |
| – | Licht auf dem Server | 33 |
| 29b | Feuer | 34 |
| 29c | Wetter | 35 |
| 29d | Erfahrung und Lapislazuli | 36 |
| – | Wegfindung | 37.1 |
| 30 | Dörfer und Dorfbewohner | 37.2 |
| 30b | Monster-Spawner und Minenschächte | 38 |
| 31 | Veröffentlichungs-Vorbereitung (GitHub, GPLv3) | 39 |
| 33 | Gamepad-Unterstützung | 40 |
| 34 (34.1, 34.2) | Verzauberung und Amboss (Zaubertisch, Amboss) | 41 (41.1, 41.2) |
| – | Vorschläge: Zustandsbudget, Speicher der Übersetzungen, Client-Tick-Thread | 42, 43, 44 |

**Anpassung am 2026-10-14 (Projektzeugnis, E-370):** keine neuen Nummern für Bestehendes, nur Status und Reihenfolge. 43 (Speicher der Übersetzungen) ist beauftragt und kommt vor 31.3; 44 (Client-Tick-Thread) ist als Teil-Etappe 46.2 in 46 aufgegangen, die Nummer 44 bleibt stillgelegt; neu sind 47 (Feinschliff Darstellung, vor 45) und der Vorschlag 48 (TLS im Mehrspieler); 39 steht am Ende der Reihenfolge.

Achtung: In älteren Texten (Entscheidungen, Performance-Verlauf, Git-Historie) bedeuten „Etappe 30“ bis „Etappe 34“ die alte Planung (Dörfer, Release, Chunks entladen, Gamepad, Verzauberung); solche Stellen tragen den Hinweis „(alte Planung; heute Etappe N)“.

## Bekannte Mängel

Stand 2026-10-14 (nach Etappe 31.2 und dem Projektzeugnis vom selben Tag); jeder Punkt am Code geprüft. Seit dem Stand vom 2026-10-10 erledigt und entfernt: Trennungsgründe übersetzt (Meldungsschlüssel in `QcMsg::Disconnect`, `QcChatText::disconnect`), Namen an den Spieler-Schlüssel gebunden und Bans nach Identität (Etappe 25), `/backup` im Hintergrund (`QcBackupRunner`, E-256), doppelte Namen beim Anmelden abgelehnt und Spielerdateien mit Hash des genauen Namens (`QcServerLogin`, `QcPlayerStorage::fileName`, E-093), Lizenzdatei (GPL-3.0, `79b76ff`). Entfallen: Kanten zwischen alten gespeicherten und neu erzeugten Chunks (ältere Weltformate werden seit 31.1 abgelehnt).

**Übersetzung und Bedienung**
- Meldungen des Mod-Loaders (Gründe im Mod-Bildschirm, Warnungen beim Laden) sind nur Englisch (`QcModLoader`).
- Eine Sprache aus `--language`/`QTCRAFT_LANGUAGE` zeigt die Sprachauswahl nicht an (sie zeigt die gespeicherte Wahl).
- Maus-Fangen auf anderen Plattformen als macOS per Cursor-Zentrierung (`QCursor::setPos` in `QcGameInput`), dort nie geprüft; auf macOS Systemdeltas, vom Menschen am Touchpad bestätigt (2026-10-08).
- Kleine UI-Befunde aus Etappe 11 (Ofen-Bildschirm, Kohle auf dunklem Grund, Schwert in dritter Person als dünner Stab; `docs/FORTSCHRITT-Archiv.md`) seitdem nicht erneut geprüft.

**Darstellung** (Projektzeugnis, Etappe 47 bzw. 45)
- Schwebende Beschriftungen der Showcase-Welt liegen unter Herzen und Hotbar („Signal Pistons“, „Creatures“) oder sind am oberen Bildrand abgeschnitten („Armour sets“, „Flowing water and lava“) (47).
- Moiré-Ringe auf entfernten Grasflächen: der Atlas wird ohne anisotrope Filterung abgetastet (`QcAtlasTexture`, Sampler Nearest mit linearen Mip-Stufen) (47).
- Der Arm in der Ich-Perspektive belegt ohne Item etwa ein Fünftel des Bildes (47).
- Unter Bäumen und nachts wird es fast schwarz (`QcRenderPipelines::minimumLight` = 0,08); Laub wirkt flächig und unruhig (47).
- Kein bewegtes Licht: gehaltene Fackel, Glühwürmchen und leuchtende Items erhellen die Umgebung nicht; fallende Blöcke werden wie Wesen grob beleuchtet; keine Schatten, Wasser einfach (45).
- Die animierte Charakter-Vorschau kostet ≈ 0,085 ms GUI-CPU pro Frame (E-233; eigener Timer in `QcCharacterPreview`, mögliche Verbesserung: im Takt der Welt zeichnen).
- Der Offscreen-Weg für Screenshots nutzt auf der Plattform `offscreen` den Fusion-Stil (andere Optik der Bedienelemente); die Skripte nehmen deshalb `QTCRAFT_OFFSCREEN=1` mit der normalen Plattform.

**Leistung, Speicher und Prüfdauer**
- **Speicherziel verfehlt:** Spitzenspeicher 1 384 MB bei „sprint, 16 Chunks“ (gerenderte Messung von 31.1, 2026-10-14) gegen das Ziel ≤ 800 MB; Befund in `docs/Performance.md`. Naheliegende Ursachen: Übersetzungen (43) und kein Entladen auf dem Server (31.3); nach beiden neu messen.
- Eine Mod mit sehr vielen kurzen Übersetzungseinträgen kostete beim Laden bis 870 MB; seit Etappe 43 begrenzt `maxTranslations` (262 144 Einträge über alle Sprachen) das auf rund 140 MB Spitze (E-371). Abgelehnte große Sprachdateien werden noch ganz gelesen (rund 300 MB Spitze bei zwei 32-MiB-Mods, danach frei).
- Der Server lädt beim Öffnen alle gespeicherten Chunks und entlädt geänderte nie (31.3).
- `QcClientSession` tickt mit einem `QTimer` auf dem GUI-Thread; ein langer Frame verzögert Vorhersage und Netz (46.2).
- Der Mesher berechnet für jede Sektion das Licht der 3 × 3 Chunks mit Rand neu, benachbarte Sektionen wiederholen diese Arbeit (`QcChunkMesher`; mit Licht auf dem Server, 33, prüfen).
- Der volle `verify.sh` dauert rund 65–68 min (3908–4056 s in 30 bis 31.2), TSan allein rund 40 min; während einer Etappe deshalb `--tsan-concurrent` (E-370), der volle Lauf einmal vor dem Merge.

**Welt und Wesen**
- Mobs: einfache Wegfindung (direkt auf den Spieler zu, Sprung über Stufen); sie meiden keine Abgründe oder Wasser; kein Mob-Mob-Ausweichen, gerufene Mobs können ineinander stehen; Ziel ist der nächste Spieler in Sichtweite ohne Sichtlinie (Wesen in den Showcase-Gehegen jagen den Spieler durch das Glas, ohne Schaden) (37.1). Licht auf Mobs grob angenähert.
- Nahkampf der Mobs ohne Sichtlinie (`QcMobSteering`: nur waagerechter Abstand und Höhenunterschied), während Spieler-Angriffe eine Sichtlinie brauchen (37.1).
- Treppen ohne Innen- und Außenecken; Zauntore verbinden Zäune auf allen Seiten (Verbindung nur über die Gruppe, ohne Blickrichtung); nur Einzeltruhen.
- Verdeckung zwischen Teilformen nur, wenn eine einzelne Nachbarfläche die Fläche ganz enthält (seltene Mod-Formen zeichnen dann eine verdeckte Fläche mehr).
- Zuckerrohr bricht nach dem Entfernen des Wassers erst bei der nächsten Änderung an seinem Boden (Prüfung nur bei Nachbar-Updates des Rohrs); Pilze haben keine Lichtregel.
- Wasser schiebt keine Spieler und Wesen (nur Items, E-296) (32).
- Showcase-Welt: Hinzufügen eines Mods verschiebt das Layout unveränderter Chunks einer bestehenden Showcase-Welt (Vorschlag in `docs/Naechste-Schritte.md` F).

**Mehrspieler und Sicherheit**
- Keine Verschlüsselung: Chat und Spieldaten gehen im Klartext, die erste Anmeldung schickt die Identität im Klartext (`docs/Multiplayer.md`); Vorschlag 48 (TLS).
- IPv6: Ein Server oder eine fürs LAN geöffnete Welt auf allen Schnittstellen (Standard) ist mit IPv6 ohne Firewall aus dem Internet erreichbar; in `docs/Multiplayer.md` beschrieben, aber leicht zu übersehen (in 39 sichtbarer machen, mit 48 entscheiden).
- PBKDF2 mit 20 000 Runden (`QcLoginCrypto::passwordRounds`) ist nach heutigen Empfehlungen wenig.
- Steuerschnittstelle nur im Headless-Prozess, keine Anbindung an das grafische Spiel (das grafische Spiel hat Statistik-Zeilen `--stats` und Offscreen-Screenshots, aber keine Befehlsschnittstelle).

**Code** (Stichprobe des Projektzeugnisses)
- Entities werden per `static_cast` nach dem Art-Feld unterschieden: 17 Stellen (13 in `QcEntitySync.cpp`, 4 in `QcEntities.cpp`); der Compiler prüft die Zuordnung nicht (in 31.3 entscheiden: behalten oder geprüfte Helfer).
- Der Destruktor von `QcGameController` löscht fünf Teile von Hand in fester Reihenfolge, weil sie Referenzen auf Member halten, die vor den QObject-Kindern sterben (46.1).

**Mods**
- Mods ersetzen Definitionen nur vollständig (keine Teil-Patches); Mods gelten global, nicht pro Welt (Vorschlag in `docs/Naechste-Schritte.md` F); höchstens 64 Mods pro Start (`QcModLoader::maxLoadedMods`); nur ein Beispiel-Mod (`rubies`).

**Klang**
- Klänge nur per Rechnung geprüft (Längen, Pegel, Frequenzen, WAV-Export); der Mensch hat am 2026-10-08 nur bestätigt, dass Ton zu hören ist. Klangfarben einfach, keine Verdeckung, kein Doppler, keine Hall-Räume. Echter Hörtest in 39.

**Veröffentlichung** (39)
- Release-Bundle nur ad hoc signiert, nicht notarisiert (Gatekeeper-Meldung auf anderen Macs, siehe README); Intel-Teil nur unter Rosetta gestartet; alle Quick-Controls-Stile werden mitgeliefert (rund 15 MB).
- Projektname enthält „Qt“ (Markenrichtlinien von The Qt Company): Umbenennung vorgemerkt („QuaderCraft“, vor der Umsetzung fragen).
- Lizenz: `LICENSE` (GPL-3.0) und Hinweis im README vorhanden; es fehlen SPDX-Kennungen in den Quelldateien, die Markenzeile zu Qt und die LGPL-Hinweise für Qt und FFmpeg im Bundle.
- Windows und Linux nie gebaut, die CI (`.github/workflows`, Matrix macOS/Ubuntu/Windows) lief noch nie auf GitHub; die Commit-E-Mail muss vor dem ersten Push umgeschrieben werden.
