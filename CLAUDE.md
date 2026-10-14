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

- Debug: `cmake --preset debug && cmake --build --preset debug`; Tests: `ctest --preset debug` (oder `-R <regex>`).
- Inhalt oder Handbuch geändert: `scripts/export-handbook.sh` erzeugt `docs/handbook` und `web/handbook/data` neu (`test_handbook` und `test_web_handbook` vergleichen sie).
- Während der Arbeit: `scripts/verify.sh --quick [--tests <regex>]` (Debug-Build, Tests, statische Analyse der gegen `dev` geänderten Dateien, rund 3–6 min). Bei Änderungen an Threads, Server-Host, Netz, Meshing oder Texturen zusätzlich TSan nur auf den nebenläufigen Tests: `scripts/verify.sh --tsan-concurrent` (= `--quick` plus `ctest -L concurrency` im TSan-Build; Liste `qc_concurrency_tests` am Ende von `tests/CMakeLists.txt`, neue Tests mit Threads oder Prozessen dort eintragen; E-370). Vor dem Merge einmal `scripts/verify.sh` (voll, alle Tests unter ASan und TSan, Release, Smoke; rund 65 min).
- Benchmarks und Fensterläufe (`scripts/bench.sh`, `scripts/bench-ab.sh`, `scripts/profile.sh`, `scripts/showcase-tour.sh` im Fenstermodus) nur nach Freigabe des Menschen und mit der Sperre `~/.cache/qtcraft/bench.lock` (Regeln in `docs/FORTSCHRITT.md`, „Arbeitsweise für Skript-Läufe“). Solange die Sperre existiert: keine Builds, Tests oder Verify-Läufe starten.

## Regeln, die am häufigsten verletzt werden

- Nie direkt auf `dev`/`main` committen; eigener Feature-Branch, bei parallelen Sitzungen eigener Worktree unter `.claude/worktrees/`. Nie nacktes `git stash`.
- Commit-Messages englisch, ohne externe Links und ohne Tool-Attribution.
- Keine Exceptions (`-fno-exceptions`), Fehler über `std::optional`/`std::expected`/`QcResult`; Warnungen sind Fehler.
- Größen: Funktionen ≤ 80 Zeilen (Ziel 40), ≤ 5 Parameter; `.cpp`/`.qml` ≤ 800 Zeilen (Ziel 500), Header ≤ 300. Absätze in Funktionen durch eine Leerzeile trennen.
- Kommentare beschreiben den Ist-Zustand, nicht die Historie. Neue Entscheidungen als `## E-<Nummer> <Titel>` in `docs/Entscheidungen.md` (nächste freie Nummer im Index prüfen; parallele Sitzungen sprechen Nummernbereiche ab).
- Neue Funktionen erweitern die Showcase-Welt (Definition of Done ab Etappe 21).
