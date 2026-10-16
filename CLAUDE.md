# QtCraft – Einstieg für Agenten

Voxel-Sandbox-Spiel mit Qt 6.8 (C++23, CMake/Ninja, QRhi/Metal, Qt Quick). Client und Server sind getrennt, auch im Einzelspieler (`QcLocalServerHost` im selben Prozess, TCP im Mehrspieler). Diese Datei wird in jede Sitzung geladen: Sie sagt, wo was steht, damit niemand den Code zum Orientieren lesen muss.

## Lesereihenfolge

1. `docs/FORTSCHRITT.md` – aktueller Stand, Branch, nächster Schritt (bei Wiederaufnahme immer zuerst).
2. `QtCraft-Agent-Auftrag.md` – Auftrag und verbindliche Regeln (Abschnitt 3); nur die Abschnitte lesen, die die Aufgabe betreffen.
3. Für Code: **erst die Karte, dann gezielt lesen** (siehe unten). Nicht ganze Verzeichnisse oder große Dateien in den Kontext laden.
4. `docs/Entscheidungen.md` nie ganz lesen: erst `docs/Entscheidungen-Index.md` (eine Zeile pro E-Nummer mit Klassen), dann nur den passenden Abschnitt (`Read` mit `offset` aus dem Index).

## Code finden

- **Code-Karte:** `docs/Code-Karte.md` – jede Klasse von `src/` mit erstem Kommentarsatz und `Datei:Zeile`, Überschrift je Verzeichnis; dazu QML, Tests, Skripte. Per `grep` suchen (`grep -n "Plant" docs/Code-Karte.md`), nicht ganz lesen. Erzeugt von `scripts/code-map.py` (etwa 1 s, nicht in git). **Fehlt die Datei oder ist sie älter als der letzte Merge, zuerst `python3 scripts/code-map.py` ausführen** – das gilt auch für jeden neuen Worktree.
- **LSP-Werkzeug (clangd):** Plugin `qtcraft-clangd` aus `tools/claude-plugins`, in `.claude/settings.json` eingeschaltet. Definition, Verwendungen, Aufrufer, Typen und Doku-Kommentar (Hover), Symbole einer Datei oder des ganzen Projekts – präziser und billiger als grep, besonders bei überladenen Namen und Qt-Makros. Voraussetzung: `build/debug/compile_commands.json` (`cmake --preset debug`, einmal bauen für die moc-Dateien; `.clangd` zeigt dorthin). Der Hintergrund-Index (`build/debug/.cache/clangd`) braucht nach dem ersten Start rund 2 min; bis dahin findet „Verwendungen“ nur Treffer in geöffneten Dateien.
- **Architektur:** `docs/Architektur.md` – Module und Abhängigkeiten (Abschnitt „Modules“), Ordner mit Inhalt („Source layout“), Threads, Protokoll, Startoptionen und Test-Hooks („Build options and test hooks“).
- Weitere Themen: `docs/Multiplayer.md`, `docs/Modding.md`, `docs/Steuerschnittstelle.md`, `docs/Texturpakete.md`, `docs/Performance.md` (Werkzeuge und Ziele), `docs/Performance-Verlauf.md` (Messungen, Methode „verschränkte A/B-Messung“).

## Aufbau in Kürze

| Ordner | Rolle | Einstieg |
|--------|-------|----------|
| `src/common` | gemeinsam: Koordinaten, Chunks, Inhalts-Registry, Physik, Protokoll, Handbuch | `QcContentRegistry`, `QcPhysics`, `QcProtocol` |
| `src/server` | autoritativer Server: Tick, Welt, Wesen, Befehle, Speichern, Weltgenerierung | `QcServer::tick`/`simulate` (Tick-Phasen), `QcServerState`, `QcServerWorld`, `QcCommandRegistry` |
| `src/clientcore` | Client ohne GUI: Sitzung, Vorhersage, Meshing, Licht, Kamera, Texturen | `QcClientSession`, `QcMeshScheduler`, `QcChunkMesher` |
| `src/app` | Qt-Quick-Client: Controller für QML, Rendering, Eingabe, Test-Hooks | `QcGameController`, `QcRenderBridge`, `QcWorldRenderer`, `QcStartupHooks` |
| `src/audio`, `src/network`, `src/headless` | Ton, TCP, `qtcraft-headless` (Server, Bots, Steuerschnittstelle) | |
| `content/` | Spielinhalt als JSON (Blöcke, Items, Rezepte, Wesen, Strukturen, Handbuch, Sprachen) | `docs/Modding.md` |
| `tests/` | QtTest/CTest nach Themen | Liste in der Code-Karte, Abschnitt „Tests“ |

Client-Code bindet nie Server-Header ein, außer in den beiden Kompositionswurzeln `QcGameController` und `QcHeadlessGame`.

## Bauen, testen, prüfen

- Debug: `cmake --preset debug && cmake --build --preset debug`; Tests: `ctest --preset debug -j 12` (oder `-R <regex>`).
- **Tests laufen parallel** (E-380): `verify.sh` ruft jeden ctest-Schritt mit `QC_TEST_JOBS` Jobs auf (Standard: Kerne minus 2). Jeder Test hat sein eigenes Datenverzeichnis (`build/<preset>/test-data/<name>`). Neue Tests: Ports immer `0`, Dateien nur im `QTemporaryDir` oder im eigenen Datenverzeichnis, Screenshots mit eigenem Namen; Tests mit mehreren Prozessen bekommen `PROCESSORS`, Tests, die Zeitgenauigkeit messen, `RUN_SERIAL` (Ende von `tests/CMakeLists.txt`). Auf Bedingungen pollen (`QTRY_*`, `QTest::qWaitFor`, auf die genaue Antwortzeile warten) statt fester Wartezeiten.
- Inhalt oder Handbuch geändert: `scripts/export-handbook.sh` erzeugt `docs/handbook` und `web/handbook/data` neu (`test_handbook` und `test_web_handbook` vergleichen sie).
- **Texte und Übersetzungen** (nicht suchen, so geht es):
  - *Inhalt* (Blöcke, Items, Wesen, Biome, Strukturen, Handbuch-Kapitel): Schlüssel in `content/qtcraft/lang/en.json` und `de.json` (Handbuch-Texte in `en.handbook.json`/`de.handbook.json`), Schlüsselnamen siehe `docs/Modding.md` („Texts“, „Chapters“); danach `scripts/export-handbook.sh`.
  - *Oberfläche und Server-Meldungen* (`tr()`/`qsTr()` in C++/QML, Quelltext englisch): `cmake --build --preset debug --target update_translations` (lupdate schreibt `translations/qtcraft_de.ts`; dass `qtcraft_en.ts` unverändert bleibt, ist normal), deutsche Texte als `"englischer Quelltext": "deutsch"` in `translations/de.json` eintragen (Plural mit `%n`: Liste je Form), dann `python3 scripts/apply-translations.py translations/qtcraft_de.ts translations/de.json` (listet, was noch fehlt). Beides committen.
- Während der Arbeit standardmäßig `scripts/verify.sh --quick --changed [--tests <regex>]` (Debug-Build, nur die Tests, deren Binary neu gelinkt wurde oder deren Eingaben sich seit ihrem letzten grünen Lauf geändert haben, statische Analyse der gegen `dev` geänderten Dateien; Inhalt, Übersetzungen, Shader, Testdaten oder CMake-Dateien geändert: alle Tests; E-405). Bei Änderungen an Threads, Server-Host, Netz, Meshing oder Texturen zusätzlich TSan nur auf den nebenläufigen Tests: `scripts/verify.sh --tsan-concurrent --changed` (= `--quick` plus `ctest -L concurrency` im TSan-Build, mit derselben Auswahl; Liste `qc_concurrency_tests` am Ende von `tests/CMakeLists.txt`, neue Tests mit Threads oder Prozessen dort eintragen; E-370). Liest ein Test zur Laufzeit Quellen oder startet er ein anderes Programm, gehört das in sein `REQUIRED_FILES` (Ende von `tests/CMakeLists.txt`). Ohne `--changed` (`--quick`, rund 3–6 min) im Zweifel und bevor ein Arbeitspaket zurückgegeben wird. Vor dem Merge einmal `scripts/verify.sh` (voll, alle Tests unter ASan und TSan, Release, Smoke; rund 15 min).
- **Arbeiten während des vollen Verify** (E-380): Der volle Lauf läuft im Worktree der fertigen Etappe mit normaler Priorität. Die nächste Etappe darf derweil in ihrem eigenen Worktree beginnen, abgezweigt vom Kandidaten (dem geprüften Stand der fertigen Etappe, nicht von `dev`): lesen, planen, Code schreiben und nur mit `nice -n 10` bauen (`nice -n 10 cmake --build --preset debug`). Schmale Tests sind erlaubt, damit nicht alles ungeprüft bis zum Ende wartet: nur Debug, nur mit niedrigster Priorität und 2 Jobs (`QC_TEST_JOBS=2 nice -n 19 scripts/verify.sh --quick --changed`, gern mit `--tests <regex>`); kein TSan-Schritt, kein voller Lauf, keine Benchmarks, bis das Verify fertig ist. Scheitert das Verify, kommt der Fix auf den alten Branch und wird danach in den neuen gemergt.
- **Nach jedem Merge nach `dev`:** im Haupt-Checkout den Debug-Build auffrischen (`nice -n 10 cmake --preset debug && nice -n 10 cmake --build --preset debug`), damit `build/debug/compile_commands.json` und damit der clangd-Index (LSP-Werkzeug) die neuen Dateien kennen; danach `python3 scripts/code-map.py`.
- Benchmarks und Fensterläufe (`scripts/bench.sh`, `scripts/bench-ab.sh`, `scripts/profile.sh`, `scripts/showcase-tour.sh` im Fenstermodus) nur nach Freigabe des Menschen und mit der Sperre `~/.cache/qtcraft/bench.lock` (Regeln in `docs/FORTSCHRITT.md`, „Arbeitsweise für Skript-Läufe“). Server-Werte (Tick, Phasen, Server-Speicher) misst `scripts/bench-headless.sh` ohne Fenster und ohne Echtzeit (Standard `@server`, braucht keine Freigabe, nimmt die Sperre selbst); im Fenster nur Bildzeiten, GPU, Client-CPU und Gesamtspeicher: `bench-ab.sh` misst standardmäßig `@core` mit 10 s je Lauf und 3 Runden, plus 3 nur für Szenarien nahe der 10-%-Schwelle oder mit großer Streuung (E-388). Gruppen nach Art der Änderung (`@signal`, `@liquids`, …) und Methode: `docs/Performance.md`, „Messmethode“. Solange die Sperre existiert: keine Builds, Tests oder Verify-Läufe starten (`verify.sh` bricht dann selbst ab).
- **Headless-Messungen und Test-Szenarien nicht in Echtzeit** (Mensch 2026-10-15): den Server so schnell ticken lassen, wie er kann (`/tick sprint`, in Tests und Messprogrammen Ticks von Hand weiterschalten) und Dauer je Tick aufzeichnen; Ziel ≤ 1–2 min Wanduhr je Messlauf. Echtzeit nur, wenn das Timing selbst geprüft wird, dann kurz und begründet. Nicht auf lange Läufe warten, sondern sie aufteilen oder beschleunigen.
- **Warteschleifen** (auf ein Verify, einen Build, eine Datei): immer mit Zeitgrenze, und abbrechen, sobald die erwartete Datei oder das Verzeichnis (z. B. der Worktree) verschwindet. Nie `until ! pgrep -f <muster>`: das Muster steht in der eigenen Shell-Kommandozeile und findet sich selbst, die Schleife endet nie.

## Regeln, die am häufigsten verletzt werden

- Nie direkt auf `dev`/`main` committen; eigener Feature-Branch, bei parallelen Sitzungen eigener Worktree unter `.claude/worktrees/`. Nie nacktes `git stash`.
- Commit-Messages englisch, ohne externe Links und ohne Tool-Attribution.
- Keine Exceptions (`-fno-exceptions`), Fehler über `std::optional`/`std::expected`/`QcResult`; Warnungen sind Fehler.
- Größen: Funktionen ≤ 80 Zeilen (Ziel 40), ≤ 5 Parameter; `.cpp`/`.qml` ≤ 800 Zeilen (Ziel 500), Header ≤ 300. Absätze in Funktionen durch eine Leerzeile trennen. **Ist eine Grenze erreicht, nach Zuständigkeit teilen** (eine benennbare Gruppe in eine eigene Klasse/Datei auslagern) – **nie** Zeilen sparen durch Kürzen oder Zusammenziehen von Kommentaren, Zusammenlegen von Anweisungen oder Entfernen der Absatz-Leerzeilen.
- Kommentare beschreiben den Ist-Zustand, nicht die Historie. Neue Entscheidungen als `## E-<Nummer> <Titel>` in `docs/Entscheidungen.md` (nächste freie Nummer im Index prüfen; parallele Sitzungen sprechen Nummernbereiche ab).
- Neue Funktionen erweitern die Showcase-Welt (Definition of Done ab Etappe 21).
