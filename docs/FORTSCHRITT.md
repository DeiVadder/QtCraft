# Fortschritt QtCraft

Wiederaufnahme-Dokument: Ein frischer Agent ohne Chatverlauf liest zuerst dieses Dokument, dann `QtCraft-Agent-Auftrag.md`, `docs/Entscheidungen.md` und `docs/FRAGEN.md`.

## Aktueller Stand

- **Aktuelle Etappe:** 2 – Durchstich Client/Server
- **Branch:** `feature/02-client-server`
- **Nächster Schritt:** Protokoll, In-Process-Transport, Server mit Testwelt, Client-Session, Mesher, Steuerschnittstelle.
- **Offene Befunde:** keine

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
| 2 | Durchstich Client/Server | in Arbeit | | |
| 3 | Weltgenerierung | offen | | |
| 4 | Interaktion und Persistenz | offen | | |
| 5 | Gegner und Gesundheit | offen | | |
| 6 | Crafting und Items | offen | | |
| 7 | Modding | offen | | |
| 8 | Politur und Abschluss | offen | | |

## Bekannte Mängel

- Spike-Szene (`QcSpikeScene`) erzeugt alle Würfelflächen, auch zwischen Wasserblöcken; sichtbar als Sägezahnmuster im Wasser. Wird mit dem echten Mesher (Etappe 2) ersetzt, der Flächen zwischen gleichartigen transluzenten Blöcken auslässt.
