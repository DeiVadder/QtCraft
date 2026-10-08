# Fortschritt QtCraft

Wiederaufnahme-Dokument: Ein frischer Agent ohne Chatverlauf liest zuerst dieses Dokument, dann `QtCraft-Agent-Auftrag.md`, `docs/Entscheidungen.md` und `docs/FRAGEN.md`.

## Aktueller Stand

- **Aktuelle Etappe:** 10 – Charaktermodell und Ansichten (noch nicht begonnen). Etappe 9 ist in `dev` gemergt.
- **Branch:** als Nächstes `feature/10-character` von `dev` anlegen
- **Nächster Schritt:** Etappe 10 planen (Beschreibung in `QtCraft-Agent-Auftrag.md` Abschnitt 4 Punkt 10), Plan hier eintragen, umsetzen.
- **Umgesetzt (Etappe 9):** `QcItemEntityManager`, `QcPlantManager`, `QcTreeShape` (Server), Protokoll v10 (`EntitySpawn` mit Item-Stapel, `DropItem`), `entities.qce`, Spielregel `keepInventory` (E-028 bis E-031), Setzlinge für Eiche/Birke/Fichte, Kreuzform im Mesher, Items im Entity-Mesher, Taste Q, Steuerbefehle `drop`/`collect`, Cheats `/gamerule`, `/spawnitem`, `/setblock`.
- **Plan Etappe 9:**
  - Server: `QcItemEntityManager` (Item-Stapel mit Position/Geschwindigkeit über `QcPhysics::stepEntity`, Aufheben bei Überlappung mit dem Spieler nach kurzer Sperrzeit, Teilaufnahme bei vollem Inventar, Zusammenlegen gleicher Stapel in der Nähe, Altern nur in geladenen Chunks, Despawn nach 300 s, Obergrenze z. B. 512 Items, darüber neue Drops in eine begrenzte Warteschlange bis Platz frei wird, Speichern mit der Welt).
  - Drops statt direkt ins Inventar: Abbauen (ersetzt E-019), Mob-Beute, Rasterreste beim Schließen, Tod mit Weltoption `keepInventory` (`/gamerule keepInventory`, in `world.json`; ersetzt E-022 teilweise).
  - Protokoll: Nachrichten für Item-Spawn (id, item, count, Position) und Stapeländerung; Bewegung/Entfernen über `EntityMoves`/`EntityRemove`. Client: `QcClientEntity` um Item erweitern, `QcEntityMesher` zeichnet kleine, drehende Würfel bzw. gekreuzte Quads mit der Item-Textur. Steuerschnittstelle: `entities` listet Items.
  - Setzlinge: Drop-Chance (`chance` in Drops), Setzling-Blöcke (Kreuzform im Mesher), natürliche Blätter verrotten ohne Stamm in Reichweite 4 (Prüfung nur nach Abbau von Stamm/Blättern, verzögert), Setzling-Item wird beim Despawn mit hoher Chance eingepflanzt, eingepflanzte Setzlinge wachsen nach zufälliger Zeit über die Baum-Features des Generators zu Bäumen (Platzprüfung).
- **Danach (vom Menschen am 2026-10-08 nachbeauftragt):** Etappe 10 (Charaktermodell, Ich-/Dritte-Person-Ansicht, Anpassung), Etappe 11 (Ofen, Schmelzen, Fortschritt), Etappe 12 (Sound und Musik). Jede Etappe als eigener Feature-Branch (`feature/10-…`, `feature/11-…`, `feature/12-…`) nach dem üblichen Ablauf (Tests, `scripts/verify.sh`, unabhängige Verifikation, Squash-Merge).
- **Offene Befunde:** keine blockierenden.

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
| 9 | Item-Entities und Setzlinge | erledigt | 2026-10-08 | Verifikation nach zwei Nachbesserungsrunden PASS (Raster beim Stopp, Grenzen nach dem Laden, ruhende Items) |
| 8 | Politur und Abschluss | erledigt | 2026-10-08 | Einstellungen, deutsche Übersetzung, Benchmark (≈ 120 FPS bei 8 Chunks), Durchspiel-Test, README, Abschlussbericht; Mod-Lader nach mehreren Prüfrunden vereinfacht und gehärtet |
| 10 | Charaktermodell und Ansichten | offen | | nach 9; Beschreibung in `QtCraft-Agent-Auftrag.md` Abschnitt 4 (Punkt 10) |
| 11 | Ofen, Schmelzen und Fortschritt | offen | | nach 10; Beschreibung in Abschnitt 4 (Punkt 11) |
| 12 | Sound und Musik | offen | | nach 11; Beschreibung in Abschnitt 4 (Punkt 12) |

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
