# Fortschritt QtCraft

Wiederaufnahme-Dokument: Ein frischer Agent ohne Chatverlauf liest zuerst dieses Dokument, dann `QtCraft-Agent-Auftrag.md`, `docs/Entscheidungen.md` und `docs/FRAGEN.md`.

## Aktueller Stand

- **Aktuelle Etappe:** 11 – Ofen, Schmelzen und Fortschritt: noch nicht begonnen. Etappe 10 ist in `dev` gemergt.
- **Branch:** `feature/11-furnace` (von `dev`)
- **Nächster Schritt:** Baseline-Messung auf `dev` mit neuem `scripts/bench.sh` (Werte nach `docs/Performance-Verlauf.md`), Plan für Etappe 11 hier eintragen (Auftrag Abschnitt 4, Punkt 11), dann umsetzen.
- **Umgesetzt (Etappe 10):** Skelett-Modelle und Outfits/Frisuren aus Daten (`models/`, `appearance/`, Flag `playable`), `QcAnimator`, Entity-/Hand-Pipeline, Ich-Perspektive mit Hand und Item, Rück-/Frontansicht (F5, Einstellung, Steuerbefehl `view`) mit Kamera-Kollision gegen feste Blöcke, Protokoll v11 (Aussehen, Pose-Flags, Blickneigung, Tod), Charakter-Bildschirm mit Speicherung in `settings.ini`, Zombie nutzt das Humanoid-Skelett. Verifikation nach einer Nachbesserungsrunde PASS.
- **Plan Etappe 10:**
  - Daten: `models/*.json` (Knochen mit Rolle, Drehpunkt und Quadern mit Slot-Namen, flach ohne Hierarchie), `appearance/*.json` (Outfits: Slot → Textur + Tönung `skin`/`hair`/`shirt`/`pants`/keine; Frisuren: zusätzliche Quader am Kopf, mit Haarfarbe getönt). Mobs können statt `model`-Quadern `"skeleton"` + `"outfit"` nutzen (Zombie wird humanoid). Neue prozedurale Muster für Gesicht/Stoff/Haar (hell, damit Tönung wirkt).
  - `QcAnimator` (clientcore, testbar): aus Pose-Zustand (Gehgeschwindigkeit, Boden, Wasser, Schleichen, Schlagen, Treffer, Tod, Blickneigung) und Zeit → Drehungen pro Rolle; Animationen Stehen, Gehen, Rennen, Springen, Schwimmen, Abbauen/Angriff, Treffer, Tod.
  - Protokoll v11: `Hello` mit Aussehen, `SetAppearance` (C→S), `EntitySpawn` mit Aussehen und Pose-Flags, `EntityMove` mit Blickneigung und Flags (schlägt, schwimmt, schleicht, rennt, am Boden), `EntityRemove` mit „gestorben“ (Client spielt Todesanimation). Server prüft Aussehen (Namen aus Registry, Längen), setzt „schlägt“ beim Abbauen/Angreifen/Benutzen.
  - Rendering: eigenes Entity-Vertexformat mit Tönungsfarbe, Entity-Pipeline/Shader; Hand-Pipeline mit gestauchter Tiefe (immer vor der Welt). Gegenstand in der Hand (Block-Würfel oder Item-Sprite).
  - Kamera: Ich-Perspektive / hinten / vorne (Taste F5 und Einstellung), Kollision per Raycast; Steuerschnittstelle `view`.
  - UI: Charakter-Bildschirm (Farben, Frisur, Outfit), gespeichert in `settings.ini`, Übertragung an den Server.
  - Tests (Parser, Animator, Protokoll, Server-Validierung, Kamera-Kollision), Screenshots beider Ansichten und mindestens dreier Animationen, Doku.
- **Umgesetzt (Etappe 9):** `QcItemEntityManager`, `QcPlantManager`, `QcTreeShape` (Server), Protokoll v10 (`EntitySpawn` mit Item-Stapel, `DropItem`), `entities.qce`, Spielregel `keepInventory` (E-028 bis E-031), Setzlinge für Eiche/Birke/Fichte, Kreuzform im Mesher, Items im Entity-Mesher, Taste Q, Steuerbefehle `drop`/`collect`, Cheats `/gamerule`, `/spawnitem`, `/setblock`.
- **Plan Etappe 9:**
  - Server: `QcItemEntityManager` (Item-Stapel mit Position/Geschwindigkeit über `QcPhysics::stepEntity`, Aufheben bei Überlappung mit dem Spieler nach kurzer Sperrzeit, Teilaufnahme bei vollem Inventar, Zusammenlegen gleicher Stapel in der Nähe, Altern nur in geladenen Chunks, Despawn nach 300 s, Obergrenze z. B. 512 Items, darüber neue Drops in eine begrenzte Warteschlange bis Platz frei wird, Speichern mit der Welt).
  - Drops statt direkt ins Inventar: Abbauen (ersetzt E-019), Mob-Beute, Rasterreste beim Schließen, Tod mit Weltoption `keepInventory` (`/gamerule keepInventory`, in `world.json`; ersetzt E-022 teilweise).
  - Protokoll: Nachrichten für Item-Spawn (id, item, count, Position) und Stapeländerung; Bewegung/Entfernen über `EntityMoves`/`EntityRemove`. Client: `QcClientEntity` um Item erweitern, `QcEntityMesher` zeichnet kleine, drehende Würfel bzw. gekreuzte Quads mit der Item-Textur. Steuerschnittstelle: `entities` listet Items.
  - Setzlinge: Drop-Chance (`chance` in Drops), Setzling-Blöcke (Kreuzform im Mesher), natürliche Blätter verrotten ohne Stamm in Reichweite 4 (Prüfung nur nach Abbau von Stamm/Blättern, verzögert), Setzling-Item wird beim Despawn mit hoher Chance eingepflanzt, eingepflanzte Setzlinge wachsen nach zufälliger Zeit über die Baum-Features des Generators zu Bäumen (Platzprüfung).
- **Danach (vom Menschen am 2026-10-08 nachbeauftragt):** Etappe 10 (Charaktermodell, Ich-/Dritte-Person-Ansicht, Anpassung), Etappe 11 (Ofen, Schmelzen, Fortschritt), Etappe 12 (Sound und Musik), Etappe 13 (Bauteile und Blocklicht), Etappe 14 (Lampe, Flüssigkeiten und Weltdetails), Etappe 15 (Profiling und Optimierung; alle am 2026-10-08 ergänzt). **Reihenfolge der Ausführung: 10, 11, 12, 13, 14, 15** (Profiling zuletzt, damit es alle Inhalte erfasst). Jede Etappe als eigener Feature-Branch (`feature/10-…` bis `feature/15-…`) nach dem üblichen Ablauf (Tests, `scripts/verify.sh`, unabhängige Verifikation, Squash-Merge).
- **Offene Befunde:** keine blockierenden. Klein: gehaltenes Schwert in dritter Person nur als dünner Stab sichtbar; GUI-Teile (Outfit-Liste, Mob-Rückfallfigur, Charakter-Bildschirm) ohne eigene Tests.

## Vom Menschen getroffene Festlegungen (2026-10-07)

- Squash-Merge nach `dev` durch den Agenten nach grüner unabhängiger Verifikation ist erlaubt. Nie nach `main`/`master`.
- Commit-Messages ohne jede Attribution (auch kein `Co-Authored-By`).
- Texturen ausschließlich prozedural/selbst erstellt, keine Downloads.
- Ab Etappe 11: Performance-Check pro Etappe (`scripts/bench.sh`, Baseline und Verlauf in `docs/Performance-Verlauf.md`, Schwelle 10 %), siehe `QtCraft-Agent-Auftrag.md` Abschnitt 3. Baseline-Messung zu Beginn von Etappe 11 auf `dev`.
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
| 11 | Ofen, Schmelzen und Fortschritt | offen | | nach 10; Beschreibung in Abschnitt 4 (Punkt 11) |
| 12 | Sound und Musik | offen | | nach 11; Beschreibung in Abschnitt 4 (Punkt 12) |
| 13 | Bauteile und Blocklicht | offen | | nach 12; Beschreibung in Abschnitt 4 (Punkt 13) |
| 14 | Lampe, Flüssigkeiten und Weltdetails | offen | | nach 13; Beschreibung in Abschnitt 4 (Punkt 14) |
| 15 | Profiling und Optimierung | offen | | nach 14; Beschreibung in Abschnitt 4 (Punkt 15); Messen vor Optimieren |

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
