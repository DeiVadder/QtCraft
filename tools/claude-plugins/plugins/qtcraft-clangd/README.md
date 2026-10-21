# qtcraft-clangd

clangd als LSP-Werkzeug von Claude Code (Definition, Verwendungen, Aufrufer, Hover, Symbole). Der
Server liest die Kompilierdatenbank des Debug-Builds (`.clangd` im Wurzelverzeichnis zeigt auf
`build/debug`) und legt seinen Hintergrund-Index in `build/debug/.cache/clangd/index` ab.

## Wann clangd startet und was es indexiert

- Claude Code startet den Server beim ersten Edit/Write einer `.cpp`/`.h`/`.hpp`/`.mm`-Datei und
  meldet ihm danach jede bearbeitete Datei (Diagnosen nach dem Edit). Es gibt einen Server je
  Claude-Code-Prozess; **Subagenten teilen den Server der Sitzung, die sie gestartet hat.**
- clangd sucht für jede gemeldete Datei die `.clangd`-Dateien ihrer Verzeichnisse. Eine Datei in
  `.claude/worktrees/<name>/` führt so zur Kompilierdatenbank *dieses* Worktrees, und der
  Hintergrund-Index nimmt sich alle ihre rund 1400 Übersetzungseinheiten vor – in einem neuen
  Worktree ohne vorhandenen Index von Grund auf.
- Ein vollständiger Index kostet auf dieser Maschine rund 1–2 CPU-Stunden: 1–3 s je
  Übersetzungseinheit auf einem Leistungskern, die generierten großen Dateien bis 12 s
  (`qrc_qc_base_content.cpp`, 4,4 MB). `--background-index-priority=low` legt die vier Threads
  (`-j=4`) auf die vier Effizienzkerne; dort ist dieselbe Arbeit nach Schätzung 2–3 × teurer; gemessen rund 20 min
  Wanduhr je Worktree bei rund 350–400 % CPU.
- Ein laufender clangd indexiert danach nur Neues: Ein neu geschriebenes, inhaltlich gleiches
  `compile_commands.json` (jedes `cmake --preset debug`) löst nichts aus, eine neue Datei in der
  Datenbank nur diese Datei. Veraltete Shards (geänderte Header) werden beim nächsten Start
  nachgeholt.

## Ursache des Laufs vom 2026-10-21

Der clangd der koordinierenden Sitzung im Haupt-Checkout lief 3 h 40 min mit rund 345 % CPU. Er
hat nicht den Haupt-Checkout immer wieder indexiert (dessen Index wurde in dieser Zeit kaum
geschrieben), sondern die Worktrees der Pakete, deren Subagenten dort Dateien bearbeiteten: Der
Indexbeginn jedes Worktrees fällt auf die Sekunde mit dem ersten Edit eines Subagenten darin
zusammen (66: Edit 15:50:37, erster Shard 15:50:51; 47.3: 16:43:46 / 16:43:59; 68: 16:34:00 /
16:34:00). Im Zeitraum des Laufs kamen vier neue Worktrees hinzu (58, 63, 47.1c, 64), jeder mit
einem vollständigen Index, der nach dem Merge mit dem Worktree wieder gelöscht wird.

## Was `.clangd` deshalb einschränkt

- **Kein Hintergrund-Index unter `.claude/worktrees/`** (`Index: Background: Skip` mit
  `PathMatch`). Geöffnete und bearbeitete Dateien bekommen weiter Diagnosen, Definition und
  Hover; „Verwendungen“ und Projekt-Symbole finden in einem Worktree nur Treffer in den
  geöffneten Dateien – dort `grep` und die Code-Karte nutzen. Das gilt auch für eine eigene
  Sitzung, deren Arbeitsverzeichnis ein Worktree ist, denn die `.clangd` des Haupt-Checkouts
  liegt über jedem Worktree und gilt für seine Dateien mit.
- **Kein Hintergrund-Index für Quellen von rcc und qmlcachegen** (`build/*/.rcc/`,
  `build/*/.qt/rcc/`): Ressourcen und kompiliertes QML, rund 70 große Dateien ohne Symbole, die
  jemand sucht. Die moc-Dateien bleiben im Index (Definitionen der Signale).

Der Haupt-Checkout behält den vollen Index; er kostet nur beim ersten Start und danach nur die
neuen oder geänderten Dateien.

## Prüfen

`clangd --check=<datei>` zeigt Flags und Fehler einer Datei. Was der Hintergrund-Index tut, zeigt
ein Lauf mit `--log=verbose` (Zeilen `Enqueueing N commands for indexing` und `Indexed <datei>`).
