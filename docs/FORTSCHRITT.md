# Fortschritt QtCraft

Wiederaufnahme-Dokument: Ein frischer Agent ohne Chatverlauf liest zuerst dieses Dokument, dann `QtCraft-Agent-Auftrag.md`, `docs/Entscheidungen.md` und `docs/FRAGEN.md`.

## Aktueller Stand

- **Aktuelle Etappe:** 4 – Interaktion und Persistenz
- **Branch:** `feature/04-interaction`
- **Nächster Schritt:** Persistenz testen, Weltbibliothek und UI (Weltliste, Hotbar, Inventar, Pause-Menü), Zielmarkierung im Renderer.
- **Offene Befunde aus der Verifikation von Etappe 3 (in Etappe 4 beheben):**
  1. Himmelslicht nach Blockänderungen: `QcMeshScheduler::blockChanged` muss alle Sektionen der 3×3-Chunk-Umgebung unterhalb und bis eine Sektion oberhalb neu meshen; E-015 korrigieren; Test.
  2. Testlücken: Parser für Biome/Features/Erze/Terrain, `isCave()` gegen geschnitzten Chunk, parallele Generierung deterministisch, Licht über Chunkgrenze mit exaktem Wert.
  3. Nit: Biome mit identischem Klimapunkt (Gleichstand) warnen.
  4. Nit: Strandblock nur in Wassernähe (`landFactor < 1`).
  5. Nit: Worldgen mit `-ffp-contract=off` übersetzen (plattformgleiche Ergebnisse).
  6. Nit: Default `replaces = qtcraft:stone` im Parser entfernen.

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
| 3 | Weltgenerierung | erledigt | 2026-10-07 | Verifikation PASS, Befunde nach Etappe 4 übernommen |
| 4 | Interaktion und Persistenz | in Arbeit | | |
| 5 | Gegner und Gesundheit | offen | | |
| 6 | Crafting und Items | offen | | |
| 7 | Modding | offen | | |
| 8 | Politur und Abschluss | offen | | |

## Bekannte Mängel

- Steuerschnittstelle nur im Headless-Prozess; Anbindung an das grafische Spiel per lokalem Socket folgt.
- Maus-Fangen per Cursor-Zentrierung (`QCursor::setPos`) ist auf macOS nur per Screenshot, nicht interaktiv geprüft.
- Biome bilden eher rundliche Flecken; Gebirge sind selten (Klimapunkt-Verteilung).
- Spawn kann auf Baumkronen liegen (höchster Nicht-Luft-Block der Säule).
