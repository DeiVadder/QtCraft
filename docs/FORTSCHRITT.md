# Fortschritt QtCraft

Wiederaufnahme-Dokument: Ein frischer Agent ohne Chatverlauf liest zuerst dieses Dokument, dann `QtCraft-Agent-Auftrag.md`, `docs/Entscheidungen.md` und `docs/FRAGEN.md`.

## Aktueller Stand

- **Aktuelle Etappe:** alle Etappen abgeschlossen (1–8 per Squash-Merge in `dev`)
- **Branch:** `dev`
- **Nächster Schritt:** keiner offen. Release `dev → main` mit finalem Review und Tag entscheidet der Mensch. Optionale Extras (TCP-Mehrspieler, Skript-Hooks, Wasserausbreitung, Sound) wären die nächsten Schritte.
- **Offene Befunde:** keine blockierenden; bekannte Mängel siehe unten und `docs/Abschlussbericht.md`.

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
| 5 | Gegner und Gesundheit | erledigt | 2026-10-08 | Verifikation nach Nachbesserung PASS |
| 6 | Crafting und Items | erledigt | 2026-10-08 | Verifikation nach Nachbesserung PASS |
| 7 | Modding | erledigt | 2026-10-08 | Verifikation nach drei Nachbesserungsrunden PASS (bösartige Mod-Dateien, Grenzen des Clients) |
| 8 | Politur und Abschluss | erledigt | 2026-10-08 | Einstellungen, deutsche Übersetzung, Benchmark (≈ 120 FPS bei 8 Chunks), Durchspiel-Test, README, Abschlussbericht; Mod-Lader nach mehreren Prüfrunden vereinfacht und gehärtet |

## Bekannte Mängel

- Steuerschnittstelle nur im Headless-Prozess; keine Anbindung an das grafische Spiel per lokalem Socket, keine Screenshots über die Schnittstelle.
- Chat-Antworten des Servers nur Englisch (keine übersetzbaren Meldungsschlüssel im Protokoll).
- Maus-Fangen per Cursor-Zentrierung (`QCursor::setPos`) ist auf macOS nur per Screenshot, nicht interaktiv geprüft.
- Biome bilden eher rundliche Flecken; Gebirge sind selten (Klimapunkt-Verteilung).
- Spielerdateien: Namen werden auf Dateinamen abgebildet; „Bob“/„bob“ teilen sich auf macOS eine Datei. Keine Sperre gegen zwei Prozesse auf derselben Welt.
- Keine Sichtlinien-Prüfung beim Abbauen/Setzen/Angreifen; keine Ratenbegrenzung für UseItem/Chat.
- Mobs: einfache Wegfindung (direkt auf den Spieler zu, Sprung über Stufen); sie meiden keine Abgründe oder Wasser. Licht auf Mobs grob angenähert.
- Spawn kann auf Baumkronen liegen (höchster Nicht-Luft-Block der Säule).
- Kein Ofen: Barren per Rezept aus Erz und Kohle (E-024).
- Kein Mob-Mob-Ausweichen; gerufene Mobs können ineinander stehen.
- Mods ersetzen Definitionen nur vollständig (keine Teil-Patches); Mods gelten global, nicht pro Welt; höchstens 64 Mods pro Start.
- Viele kurze Übersetzungseinträge (rund 3 Mio. innerhalb der 32 MiB) kosten beim Laden bis etwa 700–870 MB Speicher.
