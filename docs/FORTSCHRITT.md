# Fortschritt QtCraft

Wiederaufnahme-Dokument: Ein frischer Agent ohne Chatverlauf liest zuerst dieses Dokument, dann `QtCraft-Agent-Auftrag.md`, `docs/Entscheidungen.md` und `docs/FRAGEN.md`.

## Aktueller Stand

- **Aktuelle Etappe:** 5 – Gegner und Gesundheit
- **Branch:** `feature/05-mobs`
- **Nächster Schritt:** Mobs, Kampf, Gesundheit, Tag/Nacht fertigstellen und testen; danach Verifikation.
- **Offene Befunde aus der Verifikation von Etappe 4 (in Etappe 5 beheben):**
  1. `playersAreSavedOnDisconnectAndChunksByAutosave` prüft das Speichern beim Trennen nicht wirklich (Autosave schreibt die Datei schon vorher).
  2. Unit-Tests für die neuen Grenzprüfungen in `readPlayer`/`readChunks` (Datenzeilen im Damaged-File-Test).
  3. Nits: Abbau-Toleranz enger testen (14 Ticks abgelehnt); Weltwechsel auf dieselbe Welt: speichern → beenden → öffnen; Spieler-Schreibfehler in `save()` melden; doppeltes „damaged“ in der Weltliste; `readInfo` bei `[]`.

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
| 4 | Interaktion und Persistenz | erledigt | 2026-10-07 | Verifikation nach Nachbesserung PASS |
| 5 | Gegner und Gesundheit | in Arbeit | | |
| 6 | Crafting und Items | offen | | |
| 7 | Modding | offen | | |
| 8 | Politur und Abschluss | offen | | |

## Bekannte Mängel

- Steuerschnittstelle nur im Headless-Prozess; Anbindung an das grafische Spiel per lokalem Socket folgt.
- Maus-Fangen per Cursor-Zentrierung (`QCursor::setPos`) ist auf macOS nur per Screenshot, nicht interaktiv geprüft.
- Biome bilden eher rundliche Flecken; Gebirge sind selten (Klimapunkt-Verteilung).
- Spielerdateien: Namen werden auf Dateinamen abgebildet; „Bob“/„bob“ teilen sich auf macOS eine Datei. Keine Sperre gegen zwei Prozesse auf derselben Welt.
- Keine Sichtlinien-Prüfung beim Abbauen/Setzen; keine Ratenbegrenzung für UseItem/Chat.
- Spawn kann auf Baumkronen liegen (höchster Nicht-Luft-Block der Säule).
