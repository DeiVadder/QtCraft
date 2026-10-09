# Abschlussbericht QtCraft

Stand: 2026-10-08. Entwickelt und geprüft auf macOS arm64 (M4 Max) mit Qt 6.8 und Apple Clang 21.

QtCraft ist ein spielbarer Voxel-Sandbox-Prototyp. Man erstellt Welten (mit Seed), erkundet sie, baut ab und setzt Blöcke, craftet Werkzeuge, kämpft gegen Gegner, isst, stirbt und respawnt, speichert, beendet und lädt wieder. Mods ergänzen oder ändern Inhalte nur über Daten. Die Steuerschnittstelle erlaubt einem Agenten, das Spiel wie ein Mensch zu bedienen.

## Etappen

Jede Etappe lief auf einem eigenen Feature-Branch und wird nach einer unabhängigen Verifikation (eigener Agent, eigener Klon, `scripts/verify.sh`) per Squash-Merge als genau ein Commit nach `dev` übernommen; Etappe 8 enthält diesen Bericht und wird zuletzt gemergt. Nach `main`/`master` wurde nichts gemergt.

| # | Etappe | Branch | Ergebnis |
|---|--------|--------|----------|
| 0 | Repo-Grundgerüst | – | `.gitignore`, Auftrag, Prozess-Dokumente |
| 1 | Gerüst und Spike | `feature/01-scaffold-spike` | CMake/Presets, Schichten als Bibliotheken, QRhi-Renderer per `QQuickRhiItem` gewählt (E-003) |
| 2 | Durchstich Client/Server | `feature/02-client-server` | Binärprotokoll, In-Prozess-Transport, Physik mit Vorhersage und Abgleich, Chunk-Streaming, Steuerschnittstelle |
| 3 | Weltgenerierung | `feature/03-worldgen` | Eigenes Perlin-Rauschen, 8 Biome, Berge, Strände, Wasser, Höhlen, Erze, Bäume; Himmelslicht |
| 4 | Interaktion und Persistenz | `feature/04-interaction` | Abbauen/Setzen mit Werkzeugregeln, Inventar/Hotbar, Chat/Befehle, Weltbibliothek, Speicherformat mit Namen statt Ids |
| 5 | Gegner und Gesundheit | `feature/05-mobs` | Zombie, Schleim, Schwein aus Daten; KI (wandern, verfolgen, angreifen, fliehen), Kampf, Gesundheit, Tod/Respawn, Tag/Nacht |
| 6 | Crafting und Items | `feature/06-crafting` | Werkzeuge in 4 Stufen, 26 geformte/formlose Rezepte, 2 × 2- und 3 × 3-Raster auf dem Server, Rezeptbuch, Essen |
| 7 | Modding | `feature/07-modding` | Mod-Verzeichnis, Manifest mit Versionen und Abhängigkeiten, Ladereihenfolge, Fehlerbehandlung, Beispiel-Mod `mods/rubies`, Mods-Bildschirm |
| 8 | Politur und Abschluss | `feature/08-polish` | Einstellungen, vollständige deutsche Übersetzung, Tageslicht beim Start, Benchmark-Hook, Durchspiel-Test, README, dieser Bericht |
| 10 | Charaktermodell und Ansichten | `feature/10-character` | Skelettmodell aus Daten mit Outfits, Frisuren und Farben, Animationen (Stehen, Gehen, Rennen, Springen, Schwimmen, Schleichen, Schlagen, Treffer, Tod), Hand mit Item in der Ich-Perspektive, dritte Person hinten/vorne mit Kamerakollision, Charakter-Bildschirm, Zombie mit Skelett |
| 9 | Item-Entities und Setzlinge | `feature/09-item-entities` | Drops als Items in der Welt (Aufheben, Zusammenlegen, Despawn, Grenzen mit Warteschlange, Speichern), Wegwerfen, Spielregel `keepInventory`, Blattzerfall, Setzlinge mit Wachstum zu Bäumen |

Etappen 5 bis 8 brauchten Nachbesserungsrunden: Die Verifikation fand jeweils Blocker, die behoben und erneut unabhängig geprüft wurden. Etappe 7 und der Mod-Teil von Etappe 8 liefen mehrfach durch die Prüfung. Die Funde betrafen bösartige Mod-Dateien (Pipes, `/dev/zero`, Hardlinks), Inhalte, die der Server annahm, Clients aber ablehnen, sowie Start-Zeit und Speicher bei vielen oder großen Mods. Am Ende wurde der Mod-Lader vereinfacht: Jeder Mod wird direkt so geprüft, wie er beim Client ankäme, höchstens 64 Mods werden pro Start geladen, jedes gelesene Byte zählt gegen 64 MiB. Alle Etappen 1–8 sind in `dev` gemergt.

## Abgleich mit dem Endzustand (Abschnitt 1 des Auftrags)

| Punkt | Stand | Nachweis |
|-------|-------|----------|
| Viele Blocktypen mit unterschiedlichen Eigenschaften | erfüllt | 27 Blöcke in `content/qtcraft/blocks`, Eigenschaften in `docs/Architektur.md` (Content) |
| Keine feste Blockanzahl, Namen statt Ids in Speicherung | erfüllt | Paletten-Chunks, Regionsdateien mit Namenspalette, `tests/world/test_world_storage.cpp`, `test_mods` (Welt ohne Mod) |
| Generierung über Biom-/Schichtdaten aus der Registry | erfüllt | `biomes/`, `features/`, `ores/`, `worldgen/terrain.json`; Beispiel-Mod fügt Erz hinzu |
| Welten erstellen, speichern, laden, verwalten | erfüllt | Hauptmenü, `QcWorldLibrary`, `test_persistence`, Durchspiel-Test mit Neustart |
| Dynamische Generierung ohne spürbare Ruckler | erfüllt, Restrisiko | Worker-Threads für Generierung und Meshing; Benchmark unten (zwei unerklärte Einzelruckler unter Last) |
| Abwechslungsreiche Oberfläche, Biome, Wasser, Höhlen | erfüllt | Screenshots 03-*, Karte `03-map-seed-12345.png`; Wasser ohne Ausbreitung (optional) |
| Ich-Perspektive: laufen, springen, schwimmen, abbauen, setzen, Inventar, Gesundheit | erfüllt | `test_physics`, `test_interaction`, Screenshots 04-* |
| Mobs mit KI, Schaden beidseitig, Tod und Respawn | erfüllt | `test_mobs`, `test_server_validation`, Durchspiel-Test (Zombie-Kampf) |
| Crafting geformt/formlos, Werkzeuge mit Wirkung, Erweiterung über Daten | erfüllt | `test_crafting`, Referenzszenario, Mod-Rezepte in `test_mods` |
| Strikte Client/Server-Trennung, Multiplayer als überschaubarer Schritt | erfüllt | Einzelspieler über In-Prozess-Transport; ein Netz-Transport muss nur `QcTransport` implementieren |
| Mods: Verzeichnis, Manifest, Abhängigkeiten, Laden beim Start, datenbasiert | erfüllt | `docs/Modding.md`, `test_mods` (28 Fälle) |
| Mod-Sicherheit im Design | erfüllt | eine Lesestelle, Registry als einzige Schnittstelle, Grenzen und Rücklese-Prüfung; offene Fragen in `docs/Mod-Sicherheit.md` |
| Steuerschnittstelle: Aktionen und Beobachtung | erfüllt, ohne Screenshot | `docs/Steuerschnittstelle.md`; Screenshots nur über den Hook des grafischen Spiels |
| Steuerschnittstelle: lokal, standardmäßig aus, Basis der Integrationstests | erfüllt | nur `qtcraft-headless`; Szenario- und Durchspiel-Tests laufen darüber |
| Build und Tests grün, Spiel ohne Anleitung bedienbar | erfüllt | `scripts/verify.sh` grün (Debug, ASan/UBSan, TSan, Release, Analyse); Bedienhinweise im HUD |
| 60 FPS bei 8 Chunks | erfüllt | siehe Leistung |
| Englische Quellen und Doku, alles übersetzbar, deutsche Übersetzung | teilweise | UI-Texte und Inhalte vollständig übersetzt; Meldungen von Server und Mod-Loader (Chat-Antworten, Trenn- und Ladegründe) sind nur Englisch (siehe Mängel) |
| Architektur- und Fortschrittsdoku | erfüllt | `docs/Architektur.md`, `docs/FORTSCHRITT.md` |

## Leistung

Gemessen mit `QTCRAFT_BENCHMARK=40` im Release-Build: Sichtweite 8 Chunks (361–400 geladene Chunks), die Spielfigur sprintet und springt 40 s lang und lädt dabei laufend neue Chunks.

| Lauf | Durchschnitt | 99. Perzentil | schlechtestes Bild |
|------|--------------|---------------|--------------------|
| Seed 12345 | 119,4 FPS | 81,7 FPS (12,2 ms) | 23 ms |
| Seed 42 | 117,9 FPS | 67,4 FPS (14,9 ms) | 53 ms |

Der Durchschnitt ist durch V-Sync (120 Hz) begrenzt. In zwei frühen Läufen trat je ein einzelner Ruckler von 0,7 s bzw. 2,3 s auf, während parallel ein Verifier-Build lief. Vier weitere, instrumentierte Läufe zeigten weder solche Ruckler noch einen blockierten Hauptthread. Ganz ausschließen lässt sich die Ursache im Spiel aber nicht.

## Start- und Testanleitung

Siehe `README.md`. Kurzfassung:

```sh
cmake --preset release && cmake --build --preset release
build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft                 # spielen
build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft --mods-dir mods # mit Beispiel-Mod
ctest --preset debug                                                      # Tests
scripts/verify.sh                                                         # alle Prüfungen
```

- **Mod-Verzeichnis:** `<Datenverzeichnis>/mods`, unter macOS `~/Library/Application Support/QtCraft/QtCraft/mods`. Mit `QTCRAFT_DATA_DIR` lässt sich das Datenverzeichnis umlenken.
- **Hooks:** `--play`, `--seed`, `--look`, `--spawn`, `--chat`, `--overlay`, `--view-distance`, `--language`, `--mods-dir`, `--no-mods` sowie die Umgebungsvariablen `QTCRAFT_SCREENSHOT` und `QTCRAFT_BENCHMARK`. Alle stehen in `docs/Architektur.md`.

## Tests

- **Umfang:** 31 CTest-Ziele, darunter drei Headless-Szenarien. Sie laufen in den Builds Debug, ASan/UBSan und TSan.
- **Automatisierter Durchspiel-Test:** `tests/gameplay/test_survival_scenario.cpp`. Er läuft nur über die Steuerschnittstelle:
  1. Schwein jagen, Fleisch aufsammeln, Zombie bekämpfen, Fleisch essen, Schwert wegwerfen und wieder aufheben.
  2. Baum fällen, Stämme aufsammeln, Bretter im 2 × 2-Raster und per Rezeptbuch herstellen, Werkbank bauen; ohne Werkbank wird das 3 × 3-Rezept abgelehnt.
  3. Werkbank setzen, Spitzhacke herstellen, bis zum Stein graben.
  4. Speichern, Server neu starten und Position, Gesundheit, Werkzeugabnutzung und Werkbank wiederfinden.
- **Manueller Durchgang per Screenshots:** in `docs/screenshots/` (unten).
- **Statische Analyse:** `scripts/static-analysis.sh` mit clang-tidy, clazy und qmllint.

## Screenshots

| Datei | Inhalt |
|-------|--------|
| `01-renderer-spike.png` | Renderer-Spike (Alpha-Test, transluzentes Wasser) |
| `02-first-world.png` | erste generierte Welt |
| `03-birch-forest-lighting.png`, `03-desert.png`, `03-tundra.png`, `03-map-seed-12345.png` | Biome, Licht, Übersichtskarte |
| `04-hud-hotbar.png`, `04-inventory.png` | HUD und Inventar |
| `05-night-zombie.png` | Nacht mit Zombie |
| `06-crafting.png` | Crafting im Inventar |
| `07-mod-items.png` | Items der Beispiel-Mod im Spiel |
| `08-main-menu-de.png`, `08-settings-de.png`, `08-day-view-distance-8.png` | Hauptmenü und Einstellungen auf Deutsch, Tagansicht bei Sichtweite 8 |
| `10-first-person-sword.png`, `10-first-person-block.png` | Ich-Perspektive mit Arm und gehaltenem Schwert bzw. Block |
| `10-third-person-front.png`, `10-third-person-behind.png` | Dritte Person von vorne und hinten |
| `10-animation-walk.png`, `10-animation-swing.png`, `10-animation-swim.png` | Animationen Gehen, Schlagen (mit Spitzhacke), Schwimmen (Test-Hook `--pose`) |
| `10-zombies.png` | Zombies mit Skelettmodell und vorgestreckten Armen |
| `10-character-screen-de.png` | Charakter-Bildschirm |
| `09-items-and-saplings.png` | Gedroppte Items (Würfel, Sprites, Stapel) und zwei gesetzte Setzlinge |

## Bekannte Mängel

- **Übersetzung:** Chat-Antworten und Befehlsmeldungen des Servers (z. B. „Gave 5 x …“) sowie Gründe aus Server und Mod-Loader („Verbindung getrennt: …“, „Nicht geladen: …“) sind nur Englisch, weil diese Texte nicht im Client entstehen. Dafür wären übersetzbare Meldungsschlüssel nötig.
- **Einstellungen:** Eine Sprache aus `--language` oder `QTCRAFT_LANGUAGE` zeigt die Sprachauswahl nicht an (sie zeigt die gespeicherte Wahl).
- **Steuerschnittstelle:**
  - Sie liefert keine Screenshots.
  - Sie ist im grafischen Spiel nicht über einen lokalen Socket erreichbar, nur über `qtcraft-headless`.
- **Spielinhalt:**
  - Kein Ofen: Barren entstehen per Rezept aus Erz und Kohle (E-024).
  - Keine Wasserausbreitung.
  - Kein Sound.
- **Mobs:**
  - Einfache Wegfindung, keine Kollision zwischen Mobs.
  - `EntityMoves` sendet alle sichtbaren Mobs in jedem Tick (Items nur bei Bewegung um mehr als 2 cm; schwimmende Items werden trotzdem jeden Tick simuliert).
  - Licht auf Mobs ist nur grob angenähert.
- **Mods:**
  - Ein Mod ersetzt Definitionen nur vollständig.
  - Höchstens 64 Mods pro Start; viele kurze Übersetzungseinträge können beim Laden bis etwa 870 MB Speicher kosten (innerhalb der Grenzen gemessen).
  - Mods gelten global, nicht pro Welt.
  - Unter Windows werden Junctions nicht als Link erkannt.
  - Öffnet man eine Welt ohne einen früher genutzten Mod, gehen dessen Inhalte darin dauerhaft verloren (der Mods-Bildschirm warnt davor).
- **Spielerdateien:**
  - Namen werden auf Dateinamen abgebildet, „Bob“ und „bob“ teilen sich unter macOS eine Datei.
  - Es gibt keine Sperre gegen zwei Prozesse auf derselben Welt.
- **Server-Prüfungen:**
  - Beim Abbauen, Setzen und Angreifen prüft der Server keine Sichtlinie.
  - Für UseItem und Chat gibt es keine Ratenbegrenzung.
- **Weltgenerierung:**
  - Der Spawnpunkt kann auf einer Baumkrone liegen.
  - Biome bilden eher rundliche Flecken.
- **Charakter:**
  - Der Charakter-Bildschirm zeigt keine eigene Vorschau; man sieht das Ergebnis im Spiel in der Frontansicht (F5).
  - Animationen sind im Code festgelegt, nicht aus Daten; Mods können Modelle, Outfits und Frisuren ergänzen, aber keine neuen Bewegungen.
  - Gehaltene Werkzeuge sind flache Sprites (in dritter Person zwei gekreuzte), keine Voxel-Modelle.
  - Die Kamera der dritten Person prüft Wände mit einem Strahl, nicht mit einem Volumen.
- **Items und Pflanzen:**
  - Items in einem Block, der nachträglich gesetzt wird, werden nicht herausgeschoben.
  - Blätter zerfallen nur nach dem Entfernen eines Stamms; Blätter nahe einem anderen Baum bleiben (E-030).
  - Ausstehende Blattprüfungen werden nicht gespeichert.
  - Geworfene Items sind als Sprite von der Kante her kaum sichtbar.
- **Maus:** Das Fangen der Maus per Cursor-Zentrierung ist unter macOS nur über Screenshots, nicht interaktiv geprüft.

## Offene Fragen

Keine. `docs/FRAGEN.md` enthält keine offenen Einträge. Die Rückfragen zu Beginn betrafen Merge-Rechte, Commit-Attribution, Texturen und die Wiederaufnahme nach Pausen; die Antworten stehen in `docs/FORTSCHRITT.md`.

## Optionale Extras

Nicht umgesetzt: TCP-Mehrspieler, Skript-Hooks für Mods, Wasserausbreitung, Sound. Der Endzustand hatte Vorrang. Die Leistung liegt ohne weitere Optimierung über dem Ziel.
