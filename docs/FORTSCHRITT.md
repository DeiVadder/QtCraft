# Fortschritt QtCraft

Wiederaufnahme-Dokument: Ein frischer Agent ohne Chatverlauf liest zuerst dieses Dokument, dann `QtCraft-Agent-Auftrag.md`, `docs/Entscheidungen.md` und `docs/FRAGEN.md`.

## Aktueller Stand

- **Aktuelle Etappe:** 3 – Weltgenerierung
- **Branch:** `feature/03-worldgen`
- **Nächster Schritt:** Himmelslicht im Mesher fertigstellen und testen, Generator-Tests, Biom-Screenshots.
- **Offene Befunde (aus der Verifikation von Etappe 2, zuerst in Etappe 3 beheben):**
  - S1: Skriptmodus der Headless-CLI beendet mit `quit` immer mit 0, auch nach fehlgeschlagenen Anfragen (`QcHeadlessGame.cpp`, `quitRequested` im Skriptmodus auf `finishScript` legen; Szenariotest).
  - S2: `qUncompress` entpackt bei gefälschtem Größen-Header vollständig (Speicherangriff durch bösartigen Server). Entpacken mit harter Ausgabegrenze oder eigene begrenzte Kodierung; Test mit lügendem Header.

## Vom Menschen getroffene Festlegungen (2026-10-07)

- Squash-Merge nach `dev` durch den Agenten nach grüner unabhängiger Verifikation ist erlaubt. Nie nach `main`/`master`.
- Commit-Messages ohne jede Attribution (auch kein `Co-Authored-By`).
- Texturen ausschließlich prozedural/selbst erstellt, keine Downloads.
- Wiederaufnahme nach Token-Limit: Der Mensch startet `/loop` mit „weiter mit QtCraft laut docs/FORTSCHRITT.md".

## Etappen

| # | Etappe | Status | Datum | Notiz |
|---|--------|--------|-------|-------|
| 0 | Repo-Grundgerüst | erledigt | 2026-10-07 | `.gitignore`, Doku-Skelett |
| 1 | Gerüst und Spike | erledigt | 2026-10-07 | QRhi-Renderer gewählt, Verifikation PASS |
| 2 | Durchstich Client/Server | erledigt | 2026-10-07 | Verifikation nach Nachbesserung PASS (zwei offene Befunde S1/S2) |
| 3 | Weltgenerierung | in Arbeit | | |
| 4 | Interaktion und Persistenz | offen | | |
| 5 | Gegner und Gesundheit | offen | | |
| 6 | Crafting und Items | offen | | |
| 7 | Modding | offen | | |
| 8 | Politur und Abschluss | offen | | |

## Bekannte Mängel

- Steuerschnittstelle nur im Headless-Prozess; Anbindung an das grafische Spiel per lokalem Socket folgt.
- Maus-Fangen per Cursor-Zentrierung (`QCursor::setPos`) ist auf macOS nur per Screenshot, nicht interaktiv geprüft.
- Siehe offene Befunde S1/S2 oben.
