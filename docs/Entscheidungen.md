# Entscheidungen

Format je Eintrag: Was, Warum, verworfene Alternative.

## E-001 Sprache der Prozess-Dokumente

- **Was:** `FORTSCHRITT.md`, `FRAGEN.md` und `Entscheidungen.md` sind deutsch (Prozessdokumente für den Auftraggeber). Doku zu Spiel und Schnittstellen (`Architektur.md`, `Steuerschnittstelle.md`, `Modding.md`, `Mod-Sicherheit.md`, `ASSETS.md`), Quelltext und Standard-UI-Texte sind englisch.
- **Warum:** Der Auftrag fordert Englisch für Spiel- und Schnittstellendoku; die Prozessdokumente richten sich an den deutschsprachigen Auftraggeber.
- **Verworfen:** Alles englisch – erschwert dem Auftraggeber das Beantworten von Fragen.

## E-002 Texturen ausschließlich prozedural

- **Was:** Alle Texturen werden aus Daten-Beschreibungen (Farben, Rauschen, Muster) zur Laufzeit erzeugt; Mods dürfen zusätzlich PNG-Dateien mitbringen.
- **Warum:** Vom Auftraggeber so festgelegt; keine Lizenzrisiken, keine Binärdateien im Repo.
- **Verworfen:** CC0-Downloads.

## E-003 Welt-Renderer: eigener QRhi-Renderer über `QQuickRhiItem`

- **Was:** Die Welt wird von `QcWorldView` (`QQuickRhiItem`) mit eigenem `QcWorldRenderer` gezeichnet: ein Uniform-Buffer pro Frame, ein Textur-Atlas, zwei Pipelines (opak mit Alpha-Test, transluzent mit Blending ohne Tiefenschreiben), Index-Buffer pro Chunk-Sektion, Frustum-Culling pro Sektion. Shader in GLSL 440 über `qt_add_shaders` (alle RHI-Backends).
- **Warum:** Der Spike (ca. eine Stunde) lief auf Anhieb unter Metal, inkl. Alpha-Test (Blätter) und transluzentem Wasser (`docs/screenshots/01-renderer-spike.png`). Volle Kontrolle über Vertexformat und Draw-Calls ist für Voxel-Meshes mit vielen kleinen Updates günstiger als Qt Quick 3D.
- **Verworfen:** Qt Quick 3D mit eigener `QQuick3DGeometry` – mehr Overhead pro Node, Material-System passt schlecht zu Atlas/Alpha-Test, kein Mehrwert für Voxel.

## E-004 Screenshots nur mit echtem Fenster

- **Was:** Der Screenshot-Hook (`QTCRAFT_SCREENSHOT=<datei.png>`, `QTCRAFT_SCREENSHOT_FRAMES=<n>`) läuft mit einem echten, kurz geöffneten Fenster.
- **Warum:** Mit `QT_QPA_PLATFORM=offscreen` gibt es auf macOS kein QRhi („No QRhi found for window"), `QQuickRhiItem` bliebe leer.
- **Verworfen:** Offscreen-QPA; eigenes `QQuickRenderControl`-Offscreen-Rendering (Aufwand ohne Nutzen, da das echte Fenster erlaubt ist).

## E-005 Schichten als Bibliotheken

- **Was:** `qc_common` (nur QtCore), `qc_server` (QtCore + QtConcurrent), `qc_clientcore` (GUI-freie Client-Logik: Weltspiegel, Meshing, Vorhersage, Textur-Synthese; QtGui nur für `QImage`), `qtcraft` (Rendering, QML, Eingabe). CMake erzwingt die Schichtung über die Link-Abhängigkeiten.
- **Warum:** Server und Client-Logik headless testbar; Meshing ohne GPU testbar; die Steuerschnittstelle nutzt `qc_clientcore` ohne Fenster.
- **Verworfen:** Ein einziger Client-Target – Meshing- und Session-Tests bräuchten sonst Qt Quick.

## E-006 Statische Analyse: Konfiguration

- **Was:** `.clang-tidy` mit `bugprone`, `performance`, `modernize`, `misc` und ausgewählten `readability`-Checks. Abgeschaltet: `modernize-use-trailing-return-type`, `modernize-use-auto` (explizite Typen gewünscht), `modernize-avoid-c-style-cast` (meldet auch funktionale Casts wie `float(x)`, die für triviale Typen bewusst genutzt werden), `modernize-return-braced-init-list`, `misc-include-cleaner` (Qt-Umbrella-Header). In `tests/` zusätzlich `misc-const-correctness` (QFETCH) und `misc-use-internal-linkage` (moc). clazy mit Level 0–2 ohne die reinen Stil-Checks. `scripts/static-analysis.sh` bündelt clang-tidy, clazy und `qmllint` (Target `all_qmllint`).
- **Warum:** Hohe Signalqualität ohne Rauschen; jede Abschaltung ist begründet.
- **Verworfen:** `WarningsAsErrors` in clang-tidy – das Skript schlägt bei jedem Finding ohnehin fehl.

## E-007 Protokoll: QDataStream-Binärformat über `std::variant`

- **Was:** Jede Nachricht ist eine Struktur in `QcMessages.h`; `QcMessage` ist ein `std::variant` dieser Strukturen. Ein Frame besteht aus `quint16` (Variant-Index als Typkennung) und den mit `QDataStream` (Big Endian, `Qt_6_8`) serialisierten Feldern; Chunkdaten sind zusätzlich mit `qCompress` komprimiert. Die Protokollversion (`QcProtocol::version`) wird im `Hello` geprüft. Der Decoder begrenzt alle Längen und lehnt abgeschnittene Frames und überzählige Bytes ab. Die Nachrichtenstrukturen liegen bewusst gemeinsam in einem Header, weil sie zusammen die Variante bilden (Ausnahme von „eine Klasse pro Header").
- **Warum:** Binär, versioniert, ohne Fremdbibliothek; der Compiler erzwingt über `std::visit`, dass jede Nachricht kodiert wird. Die In-Process-Strecke serialisiert genauso wie später ein Socket.
- **Verworfen:** Protobuf/FlatBuffers (Fremdabhängigkeit); CBOR (größer, kein Mehrwert).

## E-008 Bewegung: feste Ticks, Client-Vorhersage, Abgleich per Sequenznummer

- **Was:** Server und Client simulieren mit 20 Ticks/s und derselben Physik (`QcPhysics`, Common). Der Client sendet pro Tick eine Eingabe mit Sequenznummer, sagt sie sofort lokal voraus und spielt nach jedem `PlayerState` des Servers die noch nicht bestätigten Eingaben erneut ab. Der Server begrenzt Eingaben über ein Budget (Schutz gegen Speedhacks). Die Kamera interpoliert zwischen den letzten beiden Ticks.
- **Warum:** Server bleibt autoritativ, Steuerung fühlt sich trotzdem latenzfrei an; deterministische Physik macht die Vorhersage im Normalfall exakt.
- **Verworfen:** Client meldet Positionen (nicht autoritativ); reine Server-Bewegung ohne Vorhersage (spürbare Verzögerung über Netz).

## E-009 Basisinhalte als eingebettetes Content-Pack

- **Was:** `content/qtcraft` (Manifest, Blöcke, Texturen, Übersetzungen als JSON) wird als Qt-Ressource `:/content/qtcraft` eingebettet und über denselben `QcContentPackLoader` geladen wie später Mods. Der Server schickt die gesamte Registry per `ContentSync` an den Client.
- **Warum:** Kein Sonderweg für eingebaute Inhalte; der Client braucht keinen Dateizugriff auf Inhalte, was auch für Mehrspieler passt.
- **Verworfen:** Inhalte als Dateien neben dem Programm (Installationspfade plattformabhängig); hart kodierte Blöcke.

## E-010 Thread-Übergaben über `std::mutex` statt Qt-Event-Queue

- **Was:** Daten zwischen Threads laufen über `QcMailbox` bzw. `QcWorkerPool` (`std::mutex`/`std::condition_variable`); Qt-Queued-Calls tragen nur noch das Wecksignal. Für deren Slot-Objekt gibt es eine eng gefasste TSan-Unterdrückung (`tests/tsan.supp`).
- **Warum:** QtCore ist nicht mit TSan instrumentiert; Datenübergaben über die Event-Queue erschienen als (falsche) Data Races. Mit `std::mutex` sieht TSan die Happens-Before-Kanten, echte Races bleiben sichtbar.
- **Verworfen:** Breite Unterdrückung von `QObject::event` (würde echte Races in allen Slots verstecken); Qt mit TSan selbst bauen (Installation, nicht erlaubt).

## E-011 Meshing nur bei bekannten Nachbarn

- **Was:** Eine Sektion wird erst gemesht, wenn die vier seitlichen Nachbar-Chunks geladen sind; der Server streamt deshalb einen Ring mehr als die Sichtweite. Fehlende Nachbarn gelten als opak.
- **Warum:** Vermeidet doppeltes Meshing an der Ladefront und sichtbare Löcher an Chunkgrenzen.
- **Verworfen:** Sofort meshen und bei Nachbar-Ankunft neu meshen (doppelte Arbeit beim Streaming).

## E-012 Steuerschnittstelle als JSON-Zeilen über einen Headless-Client

- **Was:** `QcControlInterface` (clientcore) übersetzt JSON-Zeilen in Aktionen einer `QcClientSession`; `qtcraft-headless` startet Server und Bot-Client in einem Prozess (stdin/stdout oder Skriptdatei). Standardmäßig ist nichts geöffnet.
- **Warum:** Gleiches Protokoll wie Spieler, headless testbar, trivial von außen (Skripte, spätere MCP-Brücke) anzusprechen.
- **Verworfen:** Eigener Server-Debug-Zugang (Sonderweg am Protokoll vorbei); TCP-Port (unnötige Angriffsfläche).

## E-013 Übergangs-Generator für Etappe 2

- **Was:** `QcTestTerrainGenerator` erzeugt sanfte Hügel mit See aus wenigen Basisblöcken. Er bleibt als billiger, vorhersagbarer Generator für Tests erhalten, wenn in Etappe 3 der datengetriebene Biom-Generator kommt.
- **Warum:** Der Durchstich braucht eine Welt; Tests brauchen eine einfache, stabile Welt.

## E-014 Weltgenerierung: Klimakarte, Kontinentrauschen, Gitter-Höhlen

- **Was:** `QcTerrainGenerator` (Server) bestimmt pro Säule Temperatur und Feuchte aus Rauschfeldern; jedes Biom (Daten) hat einen Klimapunkt, die Höhe wird per inverser Distanzgewichtung im Klimaraum gemischt (weiche Übergänge), das dominante Biom liefert die Oberflächenblöcke. Ein Kontinentrauschen trennt Land und Ozean. Höhlen entstehen aus zwei 3D-Feldern (Schnittmenge zweier Isoflächen = Röhren) und einem Kavernenfeld, ausgewertet auf einem 4er-Gitter und trilinear interpoliert. Bäume und Kakteen werden pro Säule deterministisch gewürfelt; jeder Chunk prüft auch einen Rand von 3 Säulen, damit Kronen über Chunkgrenzen stimmen. Erzadern sind Zufallswege innerhalb des Chunks. Rauschen: eigenes seed-basiertes Perlin (`QcNoise`), plattformunabhängig.
- **Warum:** Alle Blöcke, Biome, Strukturen und Erze kommen aus Daten (`biomes/`, `features/`, `ores/`, `worldgen/terrain.json`); neue Inhalte brauchen keinen Generator-Code. Determinismus erlaubt „nur Änderungen speichern".
- **Verworfen:** Biome als Voronoi-Zellen (harte Kanten); 3D-Dichtefeld für das gesamte Gelände (teurer, für Etappenziel unnötig); fremde Rauschbibliothek (Lizenz, Abhängigkeit).

## E-015 Himmelslicht im Mesher berechnet

- **Was:** Pro Mesh-Auftrag wird für die 3×3-Chunk-Umgebung (vertikal ±16 Blöcke) Himmelslicht per Breitensuche berechnet: oberhalb des höchsten lichtdämpfenden Blocks Stufe 15, sonst Ausbreitung mit −max(1, Opazität) pro Schritt. Ecken erhalten den Mittelwert der offenen Nachbarzellen (weiches Licht); der Shader skaliert mit dem Tageslicht.
- **Warum:** Kein zusätzlicher Netzwerkverkehr, Server bleibt schlank, Ergebnis ist innerhalb der Lichtreichweite exakt, Blockänderungen meshen alle Sektionen der 3×3-Chunk-Umgebung vom Boden bis eine Sektion über der Änderung neu (Licht wirkt seitlich bis 15 Blöcke und die ganze Säule hinab).
- **Verworfen:** Licht auf dem Server berechnen und mitschicken (mehr Daten, komplexe Inkrementalpflege); reine Höhenkarten-Abschattung (Höhleneingänge und Überhänge falsch).

## E-016 Chunkdaten ohne zlib: Lauflängenkodierung

- **Was:** Paletten-Indizes werden pro Sektion als Läufe (Länge, Index) übertragen statt `qCompress`.
- **Warum:** `qUncompress` kennt keine Ausgabegrenze; ein bösartiger Server könnte mit kleinem Frame riesige Allokationen auslösen (Befund S2). Lauflängen sind durch die Sektionsgröße hart begrenzt und für Gelände ähnlich kompakt.
- **Verworfen:** Zlib über die privaten Qt-Header mit eigener Grenze (private API, Mehraufwand).

## E-017 Kartenexport als Entwicklungswerkzeug

- **Was:** `qtcraft-headless --map <png> [--map-radius n]` rendert die Oberfläche von oben. Spiel-Option `--spawn x,z` setzt den Startpunkt für reproduzierbare Screenshots.
- **Warum:** Biomverteilung, Ozeane und Übergänge lassen sich in Sekunden über Kilometer prüfen.

## E-018 Speicherformat: Regionen nur mit geänderten Chunks, Namen statt Ids

- **Was:** Siehe `Architektur.md`, Abschnitt Persistence. Geänderte Chunks bleiben im Speicher, solange die Welt offen ist; gespeichert werden die Regionen, die sich seit dem letzten Speichern geändert haben, jeweils vollständig und atomar.
- **Warum:** Einfach, robust (kein Teil-Überschreiben), die Dateigröße bleibt bei typischem Spiel klein, und alles Ungeänderte entsteht deterministisch aus dem Seed. Namen in der Palette machen Welten unabhängig von der Registrierungsreihenfolge.
- **Verworfen:** Ein Datei-pro-Chunk-Format (viele kleine Dateien); Regionen mit Sektor-Tabellen und Teilaktualisierung (Aufwand ohne Nutzen bei wenigen geänderten Chunks); Speichern von Laufzeit-Ids.

## E-019 Abbau-Drops direkt ins Inventar (abgelöst durch E-028)

- **Was:** Abgebaute Blöcke legen ihre Drops direkt ins Inventar; was nicht passt, verfällt.
- **Warum:** Keine herumliegenden Item-Entities nötig, deutlich weniger Code und Netzverkehr; das Spielziel (abbauen, sammeln, craften) bleibt vollständig.
- **Verworfen:** Item-Entities mit Physik und Aufsammeln (mögliche spätere Erweiterung).

## E-020 Bedienoberfläche

- **Was:** QML-Overlays (Inventar, Pause, Chat) über der Welt; `QcGameController` leitet Eingaben nur weiter, solange kein Overlay offen ist. Item-Icons liefert ein `QQuickImageProvider` aus dem Textur-Atlas. Eine Weltliste im Hauptmenü legt Welten an, lädt und löscht sie (mit Rückfrage).
- **Warum:** Overlays nutzen die normale Qt-Quick-Fokus- und Mausbehandlung; die Welt rendert unverändert weiter.

## E-021 Cheat-Befehle nur bei erlaubten Cheats

- **Was:** Befehle, die Regeln umgehen (`/give`, ab Etappe 5 auch `/time`, `/summon`, `/heal`, `/kill`), laufen nur mit `QcServerConfig::allowCheats` (Standard im Einzelspieler: an). Ein späterer dedizierter Server setzt es aus und erlaubt sie nur Operatoren.
- **Warum:** Im Einzelspieler sind sie Test- und Komfortwerkzeug (auch für die Steuerschnittstelle); im Mehrspieler würden sie die Validierung des Servers aushebeln.
- **Verworfen:** Befehle ganz entfernen (Szenariotests und Agenten bräuchten Umwege).

## E-022 Mobs werden nicht gespeichert, Inventar bleibt beim Tod (Inventar-Teil abgelöst durch E-029)

- **Was:** Mobs existieren nur um Spieler herum und verschwinden beim Schließen der Welt; beim Tod behält der Spieler sein Inventar und erscheint am Spawnpunkt.
- **Warum:** Kein Entity-Speicherformat nötig; Mobs erscheinen ohnehin ständig neu. Ohne Item-Entities (E-019) gäbe es keinen Weg, verlorene Gegenstände wiederzuholen.
- **Verworfen:** Mobs in Regionsdateien speichern; Inventar beim Tod verlieren.

## E-023 Entities als pro Frame erzeugtes Mesh

- **Was:** Mob-Modelle sind Quader aus den Daten; der Client erzeugt jedes Frame ein gemeinsames Mesh aller sichtbaren Entities (interpoliert, gedreht) und zeichnet es mit der Block-Pipeline. Licht unter Dächern wird grob angenähert (Spalte bedeckt → dunkler).
- **Warum:** Wenige Dutzend Quader kosten pro Frame kaum etwas, und es braucht weder Skelett-Animation noch eigene Shader.
- **Verworfen:** Eigene Instanzierungs-Pipeline (Aufwand ohne Messnutzen bei dieser Anzahl); Licht exakt aus dem Lichtvolumen (liegt nur in den Mesh-Jobs vor).

## E-024 Barren ohne Ofen (abgelöst durch E-037)

- **Was:** Eisen- und Goldbarren entstehen per formlosem Rezept aus Erz und Kohle im Raster; es gibt (noch) keinen Ofen. Erze droppen sich selbst, Kohle- und Diamanterz ihr Material.
- **Warum:** Der Auftrag verlangt Werkzeuge mehrerer Stufen; ein Ofen bräuchte Block-Entities mit Zustand und Zeit, die es bisher nicht gibt. So bleibt die Werkzeug-Kette Holz → Stein → Eisen → Diamant spielbar.
- **Verworfen:** Ofen jetzt einführen (eigenes Speicher- und Synchronisationsformat); Erz direkt als Werkzeugmaterial (unlogisch, keine Kohle-Verwendung).

## E-025 Raster auf dem Server, dazu ein Rezeptbuch

- **Was:** Das Crafting-Raster liegt auf dem Server (Slots 36–44), der das Ergebnis selbst berechnet; zusätzlich crafted `CraftRecipe` direkt aus dem Inventar. Beim Schließen wandert der Rasterinhalt zurück ins Inventar.
- **Warum:** Server-Autorität wie beim Inventar; das Rezeptbuch macht Crafting für die Steuerschnittstelle und Einsteiger einfach, ohne Felder einzeln zu belegen. Rückgabe beim Schließen verhindert, dass Gegenstände im Raster „verschwinden“.
- **Verworfen:** Raster nur clientseitig (Betrug möglich); nur Rezeptbuch (Raster gehört zum Spielgefühl); Rasterinhalt beim Schließen fallen lassen (keine Item-Entities, E-019).

## E-026 Datenbasierte Mods mit vollständiger Ersetzung

- **Was:** Mods sind Inhaltspakete wie das Basisspiel (`pack.json` plus JSON/PNG-Dateien), ohne ausführbaren Code. Eine Definition mit dem Namen eines bestehenden Eintrags ersetzt diesen vollständig (keine Teil-Patches); die Ladereihenfolge folgt den Abhängigkeiten, bei Gleichstand der Pack-Id. Kaputte Mods (Manifest, fehlende/zu alte/abgeschaltete Abhängigkeit, Zyklus, doppelte Id) werden samt allen abhängigen Mods übersprungen, das Spiel startet trotzdem.
- **Warum:** Pflicht laut Auftrag sind datenbasierte Mods; dieselbe Lade-Mechanik für Basisspiel und Mods vermeidet Sonderwege. Vollständige Ersetzung ist eindeutig und leicht zu prüfen; Teil-Patches bräuchten Regeln je Feld. Eine stabile Reihenfolge macht Overrides vorhersagbar.
- **Verworfen:** Skript-Hooks per `QJSEngine` (optionales Extra, Sicherheitsfragen offen, siehe `docs/Mod-Sicherheit.md`); Merge einzelner Felder; Abbruch des Spielstarts bei einem fehlerhaften Mod.

## E-027 Welten merken sich ihre Pakete

- **Was:** `world.json` speichert Id, Name und Version aller beim Speichern geladenen Pakete. Beim Öffnen meldet der Server fehlende oder geänderte Pakete als Warnung; die Welt öffnet trotzdem. Blöcke entfernter Mods werden zu Luft, ihre Items verschwinden (Namensauflösung, E-018).
- **Warum:** Spieler sollen erfahren, warum Inhalte fehlen, ohne dass eine Welt unbenutzbar wird.
- **Verworfen:** Laden verweigern (zu streng für Einzelspieler); unbekannte Namen als Platzhalter-Blöcke aufbewahren (eigenes Format, späterer Schritt).

## E-028 Drops als Item-Entities

- **Was:** Abgebaute Blöcke, Mob-Beute, Rasterreste, die nicht ins Inventar passen, und weggeworfene Items (Taste Q) liegen als Item-Entities in der Welt. Sie fallen, bleiben liegen, schwimmen in Wasser oben, verschmelzen mit gleichen Stapeln im Umkreis von 1 m und werden aufgehoben, sobald sie den Spieler (Box plus 0,5 m) berühren und die Sperrzeit vorbei ist (0,5 s, geworfen 2 s). Passt nur ein Teil ins Inventar, bleibt der Rest liegen. Items altern und bewegen sich nur in geladenen Chunks und verschwinden nach 300 s. Sie werden mit der Welt gespeichert (`entities.qce`).
- **Grenzen:** Höchstens 512 aktive Items (in geladenen Chunks); weitere Drops warten in einer Warteschlange (höchstens 1024 Einträge, nahe gleiche Drops werden zusammengefasst, bei Überlauf fällt der älteste weg) und erscheinen, sobald Platz frei wird. Lädt ein Chunk mit mehr Items, als aktiv sein dürfen, ruhen die jüngsten (außer frischen Drops unter 10 s) an Ort und Stelle (eingefroren, für Clients unsichtbar, gleiche Id) und wachen ältestes zuerst auf, sobald Platz frei wird. Wartende Drops erscheinen nur in geladenen Chunks. Höchstens 4096 Items liegen in der Welt (plus die Warteschlange), darüber werden die ältesten in entladenen Chunks verworfen. Stapel über `maxStack` werden geteilt. Liegende Items werden nicht simuliert, bis der Block darunter verschwindet; an Clients gehen nur Items im Umkreis von 32 m, Bewegungen erst ab 2 cm Abstand zur zuletzt gesendeten Position (schwimmende Items wippen sonst ständig). Beim Beenden des Servers wird ein offenes Raster wie beim Trennen geleert, auch für bereits getrennte Spieler.
- **Warum:** Vom Menschen am 2026-10-08 beauftragt; die Grenzen halten Server-Tick und Netzverkehr auch bei großen Abbauaktionen klein.
- **Verworfen:** Drops ohne Grenze (Tick-Zeit wächst unbegrenzt); Drops über der Grenze direkt ins Inventar (widerspricht dem Aufsammeln); Items im Chunk-Format speichern (bräuchte ein neues Regionsformat).

## E-029 Spielregel keepInventory

- **Was:** Die Weltoption `keepInventory` (in `world.json` unter `gameRules`, Befehl `/gamerule keepInventory true|false`, Kontrollkästchen beim Anlegen einer Welt) entscheidet, ob der Spieler beim Tod Inventar und Rasterinhalt behält oder als Items fallen lässt. Voreinstellung und Wert für ältere Welten: behalten.
- **Warum:** Das bisherige Verhalten (E-022) bleibt Standard, das Fallenlassen ist die gewünschte Option.
- **Verworfen:** Fallenlassen als Standard (ändert bestehende Welten unerwartet).

## E-030 Blattzerfall über Stammnähe

- **Was:** Blätter, die ein Baum-Feature als Krone nutzt, verrotten, wenn innerhalb von 4 Blöcken (in jeder Achse) kein Stamm eines Baum-Features mehr steht. Geprüft wird nur nach dem Entfernen eines Stammblocks, für alle Blätter im Umkreis, mit 0,5 bis 4 s Verzögerung, höchstens 64 Prüfungen und 8 Zerfällen pro Tick. Liegt ein Teil der Umgebung in einem nicht geladenen Chunk, wird die Prüfung nach 5 s wiederholt. Verrottete Blätter lassen ihre Drops fallen (Setzling mit 8 % Chance).
- **Warum:** Einfach, ohne Blockzustände und ohne Dauerscans; der Baum zerfällt sichtbar nach und nach.
- **Grenze:** Blätter, die nahe am Stamm eines anderen Baums hängen, bleiben. Per Cheat gesetzte Blätter verhalten sich wie natürliche (Spieler erhalten sonst keine Blätter, ihre Drops sind leer bis auf Setzlinge).
- **Verworfen:** Minecraft-artige Distanz entlang verbundener Blätter (Blockzustand pro Blatt nötig); Zufallsticks über alle geladenen Blätter (Dauerlast).

## E-031 Setzlinge als datenbasierte Pflanzen

- **Was:** Ein Block mit `sapling`-Objekt (Feature, Wachstumszeit, Boden, Pflanzchance) ist ein Setzling. Er steht nur auf erlaubtem Boden, bricht mit seinem Boden weg und wächst nach zufälliger Zeit (nur in geladenen Chunks) über dieselbe Baumform wie der Generator (`QcTreeShape`) zum Baum, wenn der Stamm Platz hat; sonst versucht er es alle 30 s erneut. Liegt ein Setzling-Item 300 s, pflanzt es sich mit der Pflanzchance (Basisspiel 75 %) an seiner Stelle ein. Setzlinge haben die Kreuzform (`"shape": "cross"`), die der Mesher als zwei gekreuzte, beidseitige Quads zeichnet. Wachsende Setzlinge werden in `entities.qce` gespeichert.
- **Warum:** Wiederaufforstung ohne Skripte, vollständig aus Daten erweiterbar.
- **Verworfen:** Zufallsticks pro Chunk (Dauerlast), eigene Baumformen für gewachsene Bäume.

## E-032 Skelettmodelle und Aussehen aus Daten

- **Was:** `models/*.json` beschreibt Skelette aus flachen Knochen (Name, Rolle `head`/`body`/`right_arm`/`left_arm`/`right_leg`/`left_leg`/`none`, Drehpunkt, Quader mit Slot-Namen). `appearance/*.json` beschreibt Outfits (Slot → Textur und Tönung `skin`/`hair`/`shirt`/`pants`/`none`) und Frisuren (eigene Quader am Kopfknochen). Spieler nutzen das Modell `qtcraft:humanoid`; Mobs können statt fester Quader `skeleton` und `outfit` angeben (der Zombie tut das, mit `armsForward`). Getönte Texturen sind hell, die Farbe kommt pro Vertex aus dem Aussehen. Das Gesicht ist ein dünner Quader vor dem Kopf mit transparenter Gesichtstextur.
- **Warum:** Mods können Skins, Frisuren und Modelle ergänzen, ohne Code; Farben brauchen keine Texturen pro Spieler im Atlas.
- **Verworfen:** Knochen-Hierarchie (für die gewünschten Animationen nicht nötig); eigene Texturen pro Spieler im Atlas (Atlas müsste zur Laufzeit wachsen); Gesichtstextur pro Würfelseite (Mesher bleibt einfacher).

## E-033 Animation auf dem Client aus Server-Zuständen

- **Was:** `QcAnimator` berechnet Knochendrehungen aus einer Pose (Geschwindigkeit, gelaufene Strecke, Boden, Wasser, Schleichen, Rennen, Schlag-, Treffer- und Todesfortschritt, Blickneigung) und die Bewegung der Hand in der Ich-Perspektive. Der Server sendet nur Zustände: `EntityMove`/`EntitySpawn` tragen Blickneigung, Flags (Schlag begonnen, am Boden, im Wasser, schleicht, rennt) und das gehaltene Item; `EntityRemove.died` startet die Todesanimation. Zeitabläufe (Schlag 0,3 s, Tod 1 s, Itemwechsel, Landen) zählt der Client.
- **Warum:** Server bleibt autoritativ für Zustände, Animationen kosten kein Netz und laufen flüssig mit Interpolation; dieselbe Mechanik nutzen Spieler und Mobs.
- **Verworfen:** Keyframe-Animationen aus Daten (mehr Aufwand, für Quader-Figuren kein Gewinn).

## E-034 Hand in eigener Pipeline mit gestauchter Tiefe

- **Was:** Arm und Item der Ich-Perspektive sind ein eigenes Mesh im Kamera-Raum, gezeichnet zuletzt mit einem Vertex-Shader, der die Tiefe in die vordersten 5 % des Bereichs staucht (je nach Backend 0..1 oder −1..1). Entities und Hand nutzen ein eigenes Vertexformat mit Tönungsfarbe.
- **Warum:** Die Hand verschwindet nie in Wänden, ohne zweiten Render-Pass oder Tiefen-Löschen.
- **Verworfen:** Tiefentest aus (Selbstverdeckung der Quader falsch); zweiter Pass (mehr Code im Renderer).

## E-035 Ansichten und Kamerakollision

- **Was:** Ich-Perspektive, dritte Person hinten und vorne (F5, Einstellung „Ansicht“, Steuerbefehl `view`, Test-Hook `--camera`). Die Kamera steht 4 m vom Auge entfernt; ein Raycast vom Auge zieht sie bis 0,3 m vor den ersten Block heran. Im Tod gilt die Ich-Perspektive.
- **Warum:** Einfach und robust; der vorhandene Block-Raycast reicht.
- **Verworfen:** Kugel-Sweep statt Strahl (Kanten können kurz sichtbar sein, für den Zweck ausreichend).

## E-036 Aussehen im Client gespeichert, vom Server geprüft

- **Was:** Das Aussehen (Outfit, Frisur, vier Farben) liegt in `settings.ini` des Clients und geht mit `Hello` und bei Änderung mit `SetAppearance` an den Server. Der Server ersetzt unbekannte Outfits durch das Standard-Outfit, unbekannte oder falsche Frisuren durch „keine“, kürzt Farben auf RGB; Namen über 64 Zeichen lehnt schon der Decoder ab. Andere Clients bekommen den Spieler danach neu gemeldet.
- **Warum:** Das Profil gehört zum Spieler, nicht zur Welt; mehrspielerfähig, ohne dem Client zu trauen.
- **Verworfen:** Aussehen in der Spielerdatei der Welt (müsste pro Welt neu gewählt werden).

## E-037 Ofen als Maschinen-Blockfähigkeit, Schmelzrezepte aus Daten

- **Was:** Ein Block mit `"machine": {"category", "speed"}` ist eine Maschine mit Eingabe-, Brennstoff- und Ausgabefeld. Schmelzrezepte (`smelting/*.json`: `input`, `result`, `seconds`, `category`, Standard `smelting`) gelten für Maschinen derselben Kategorie; Kategorien sind Namen ohne Namensraum, damit jedes Paket Rezepte für den Ofen des Basisspiels beisteuern kann. Brennstoff ist `fuelSeconds` an Items und Blöcken (Block-Items erben ihn). Der Ofen (8 Bruchstein) ersetzt E-024: Eisen- und Golderz → Barren, Sand → Glas, Ton → Ziegelblock, Bruchstein → Stein, Stämme → Holzkohle, rohes → gebratenes Schweinefleisch, je 10 s; Kohle und Holzkohle brennen 80 s, Holz 15 s, Stöcke und Setzlinge 5 s, Holzwerkzeuge 10 s. Die Raster-Rezepte für Barren, Glas und Ziegel entfallen. Der Ofen hat (bis zu Blockzuständen in Etappe 13) seine Front fest auf der Nord- und Südseite.
- **Warum:** Fortschritt über eine echte Verarbeitungsstation, vollständig über Daten erweiterbar; Mods können eigene Maschinenblöcke mit eigener Kategorie und Geschwindigkeit anlegen, ohne Code. Ton wird direkt zum Ziegelblock gebrannt, weil es kein Ziegel-Item braucht, das nur ein Zwischenschritt wäre; gebrannter Ton für die Minenlampe (Etappe 14) kann ein eigenes Rezept bekommen.
- **Verworfen:** Schmelzrezepte als weiterer `type` der Handwerksrezepte (andere Felder, eigene Prüfung, kein Raster); Kategorien mit Namensraum (Mod-Rezepte für den Basisofen bräuchten dann `qtcraft:`-Präfixe); ausrichtbare Front ohne Blockzustände (eigener Sonderweg vor Etappe 13).

## E-038 Maschinenzustand auf dem Server

- **Was:** `QcMachineManager` hält pro Blockposition die drei Stapel, die Restbrennzeit und den Fortschritt. Eine Maschine zündet ein Brennstoffteil nur, wenn die Eingabe ein Rezept ihrer Kategorie hat und das Ergebnis in die Ausgabe passt; Brennen und Fortschritt laufen mit der Maschinengeschwindigkeit, ohne Brennstoff fällt der Fortschritt doppelt so schnell zurück, eine andere Eingabe beginnt von vorn. Nur Maschinen in geladenen Chunks ticken; höchstens 4096 Maschinen (Setzen über die Grenze wird abgelehnt). Gespeichert wird in `machines.qce` (eigene Formatversion, Grenzen und Plausibilität beim Lesen, Namen statt Ids; unbekannte Items fallen mit Warnung weg, beschädigte Dateien verhindern das Öffnen der Welt mit Meldung). Wird der Block ersetzt oder abgebaut, fallen die Inhalte als Item-Entities. Benutzen öffnet die Maschine (Schleichen setzt dagegen), mit Reichweitenprüfung; der Server schließt sie, wenn der Spieler stirbt, sich entfernt oder der Block verschwindet. Die Maschinenfelder sind die Slots 45–47 in `MoveInventoryItem`; in die Ausgabe darf nichts hinein (auch nicht per Tausch), ins Brennstofffeld nur Brennstoff. `MachineState` geht bei Änderungen der Stapel oder ab einem Prozentpunkt Fortschritt an alle, die die Maschine offen haben.
- **Warum:** Server-Autorität wie bei Inventar und Raster; die Grenzen halten Tick-Zeit und Dateigröße beschränkt, die Prozent-Schwelle den Netzverkehr.
- **Verworfen:** Maschinen in den Regionsdateien speichern (neues Regionsformat); Maschinen auch in entladenen Chunks weiterrechnen (Arbeit ohne Zuschauer, beim Laden nachholen wäre ungenau); Ausgabe ins Inventar statt Entnahme (widerspricht dem gewohnten Spielgefühl).

## E-039 Hunger

- **Was:** Jeder Spieler hat 0–20 Nahrungspunkte (`QcHunger`). Aktivität erzeugt Erschöpfung (Gehen 0,01 pro m, Sprinten 0,1 pro m, Schwimmen 0,015 pro m, Sprung 0,05, Sprintsprung 0,2, Abbau 0,005, Angriff 0,1, erlittener Schaden 0,1); je 4 Erschöpfung kosten einen Punkt. Ab 18 Punkten heilt der Spieler einen Lebenspunkt alle 4 s (kostet 3 Erschöpfung), darunter nicht. Bei 0 Punkten verliert er alle 4 s einen Lebenspunkt, aber nie unter 1: Hunger tötet nicht. Essen (`food` an Items, jetzt Nahrungspunkte statt Lebenspunkte, ganze Punkte bis 20) geht nur, wenn er nicht satt ist. Wiederbeleben füllt die Punkte. Der Wert steht in der Spielerdatei (Format 2; ältere Dateien laden mit vollen Punkten), geht als `PlayerFood` an den Client und als Leiste rechts neben den Herzen ins HUD. `/feed [0..20]` setzt ihn (Cheat).
- **Warum:** Nahrung bekommt einen Zweck (gebratenes Fleisch aus dem Ofen ist mehr wert), Regeneration wird an Versorgung gebunden, ohne Einsteiger durch Verhungern zu bestrafen. Das bisherige Feld `food` bleibt gültig, nur seine Bedeutung wechselt; vorhandene Mods laden weiter.
- **Verworfen:** Zusätzliche Sättigung als zweiter Puffer (mehr Zustand, für den Prototyp kein Gewinn); Verhungern bis zum Tod (zu hart im Einzelspieler); Essen weiterhin direkt heilen lassen (dann bliebe Hunger ohne Wirkung).

## E-040 Klänge als Syntheseparameter aus Daten

- **Was:** Klänge sind keine Audiodateien, sondern Definitionen in `sounds/*.json` (Bus, Lautstärke, Tonhöhen-Streuung, 1–8 Varianten, Schleife, Reichweite, 1–8 Schichten aus Sinus/Rechteck/Dreieck/Sägezahn/Rauschen/Knistern mit Frequenzverlauf, Einsatz, Dauer, Hüllkurve, Tief-/Hochpass, Tremolo). `QcSoundSynthesizer` rendert sie deterministisch (Rauschen aus einem Seed aus Name und Variante) und normiert jede Wiedergabe auf die Spitze 0,9; Schleifen werden mit gleichleistungs-überblendetem Ende nahtlos. Der Client rendert beim Weltstart alle Klänge im Hintergrund (`QcSoundBank`, höchstens 16 Mi Samples, darüber bleibt ein Klang mit Warnung stumm). Parser und Inhalts-Synchronisation prüfen dieselben Grenzen (`QcSoundDefinition::problem()`). `qtcraft-headless --export-sounds` schreibt WAV-Dateien zum Anhören.
- **Warum:** Auftrag: prozedurale Klänge ohne Downloads, Mods sollen Klänge ergänzen. Daten statt Code halten Mods ausführungsfrei und klein in der Synchronisation; die Normierung macht `volume` zur einzigen Lautstärkeangabe; die Grenzen deckeln Rechenzeit und Speicher auf dem Client gegen bösartige Inhalte.
- **Verworfen:** Selbst erzeugte WAV-Dateien im Repo (Mods könnten nur PNG-artige Binärdaten liefern, schwerer zu prüfen, größer); Synthese erst beim ersten Abspielen (Ruckler im Spiel); unnormierte Pegel (jede Schichtänderung ändert die Lautheit).

## E-041 Eigener Software-Mixer über QAudioSink

- **Was:** Ein Mixer (`QcAudioMixer`, ohne Audiogerät testbar) mischt Stimmen mit Verstärkung, Panorama (konstante Leistung), Tonhöhe (lineare Interpolation), Schleife und Bus (Effekte, Umgebung, Musik) unter Master-Lautstärke und Stummschaltung; höchstens 32 Stimmen, eine neue verdrängt die leiseste Nicht-Schleife; Verstärkungsänderungen rampen über einen Mischblock; ein weicher Begrenzer (ab 0,75, tanh) verhindert Übersteuern. `QcAudioOutput` speist ihn per `QAudioSink` im Pull-Modus (Float-Stereo, sonst 16 Bit, 80 ms Puffer) über `QcMixerDevice`; ein Mutex schützt den Mixer vor dem Audio-Thread. Ohne Gerät läuft das Spiel stumm (Grund einmal geloggt). Läufe mit Screenshot-Hook bleiben stumm, außer `QTCRAFT_AUDIO=1`; `--no-audio`/`QTCRAFT_AUDIO=0` schalten Ton ab.
- **Warum:** `QSoundEffect` kann kein Panorama und keine Bus-Lautstärken, braucht Dateien oder Puffer pro Klang und begrenzt die Kontrolle über gleichzeitige Stimmen. Ein eigener Mixer ist rein und deterministisch testbar und hält alles in einem Ausgabestrom.
- **Verworfen:** `QSoundEffect` pro Klang (kein Panorama); Push-Modus mit eigenem Timer (mehr Thread-Logik); Mischen in einem eigenen Thread (der Pull-Modus liefert ihn bereits).

## E-042 Klangmaterial an Blöcken, Klänge an Mobs und Maschinen

- **Was:** Blöcke tragen ein Klangmaterial (`"sound": "stone"` → `qtcraft:stone`); der Klang einer Aktion (`break`, `place`, `step`, `hit`) heißt `<Namensraum>:block.<Material>.<Aktion>`, einzelne Aktionen lassen sich mit `"sounds"` ersetzen. Ohne Angabe folgt das Material aus Werkzeug, Form und Darstellung (Axt → Holz, Schaufel → Erde, Kreuz/cutout → Gras, durchscheinend → Glas, sonst Stein; Flüssigkeiten und Unsichtbares stumm). Mobs haben `ambient`, `hurt`, `death` (Standard: kein Umgebungsklang, allgemeiner Treffer- und Todesklang), Maschinen einen Betriebsklang (Standard Ofen-Knistern). Unbekannte Namen meldet der Mod-Lader als Warnung, zur Laufzeit bleibt es still. Basisinhalte: acht Materialien mit je vier Klängen, Mob-, Spieler-, Wasser-, UI-, Ofen- und Umgebungsklänge; der Beispiel-Mod bringt das Material `crystal` und Käferklänge mit.
- **Warum:** Wenige Materialien decken viele Blöcke ab; bestehende und künftige Mod-Blöcke klingen ohne Änderung plausibel; das Schlüsselschema ist ohne Code erweiterbar.
- **Verworfen:** Schlüssel pro Block (viele Dateien, Mods müssten jeden Block vertonen); Materialien als eigener Definitionstyp (zusätzliches Konzept ohne Gewinn); Rückfall auf Stein bei fehlenden Material-Klängen (versteckt Tippfehler).

## E-043 Klangereignisse vom Server, Platzierung im Client

- **Was:** Was andere sehen könnten, meldet der Server als `SoundEvent` (Protokoll v13: Name, Position, Lautstärke, Tonhöhe; Dekoder prüft Länge und Bereiche): Abbauen und Setzen, Mob-Umgebungsklänge (im Mittel alle 10 s je Mob), Treffer und Tod von Mobs und Spielern, Essen, Aufheben, brennende Maschinen (im Mittel alle 2,5 s). Er sendet nur an Spieler innerhalb der Reichweite des Klangs (Reichweite × max(1, Lautstärke)) und würfelt mit einem eigenen Zufallsgenerator, damit Drops unverändert bleiben. Der Client berechnet Verstärkung `(1 − d/Reichweite)²`, dämpft Klänge hinter dem Hörer um bis zu 25 % und setzt das Panorama aus der Blickrichtung (höchstens 85 % zu einer Seite). Eigene Schritte (alle 1,7 m, Untergrund unter den Füßen), Sprung, Landung, Platschen, Schwimmzüge, Abbau-Schläge und Schritte anderer Wesen aus deren Bewegung entstehen im Client; UI-Klicks beim Loslassen der Maus über einem Button. Die Sitzung merkt sich die letzten 64 Klänge für die Steuerschnittstelle (`sounds`).
- **Warum:** Server-Autorität bleibt gewahrt, und der Mehrspielerfall ist abgedeckt; die Platzierung hängt von der eigenen Blickrichtung ab und gehört deshalb in den Client. Eigene Schritte ohne Server-Umweg klingen ohne Latenz.
- **Verworfen:** Panorama auf dem Server (für jeden Empfänger anders); Klänge implizit aus Block-Updates ableiten (Setzen und Abbauen nicht unterscheidbar, keine Mob-Klänge); Schritte anderer Spieler als Server-Ereignisse (viel Verkehr).

## E-044 Umgebungsschleifen und algorithmische Musik

- **Was:** `QcAmbience` bestimmt alle 10 Ticks Zielpegel: Wind unter freiem Himmel (0,3 auf Höhe 64 bis 1 auf Höhe 120, unter dünnem Dach gedämpft), Höhle bei mindestens 4 undurchsichtigen Blöcken in 24 Blöcken über dem Kopf, Wasser nach der Zahl der Wasserblöcke in einer Stichprobe (Radius 6, jeder zweite Block); die Pegel blenden mit 0,5 pro Sekunde über. `QcMusicComposer` komponiert pro Seed ein Stück auf einer Dur- oder Moll-Pentatonik (66–90 bpm, Motive A A' B A mit Varianten und Pausen, Bass pro Takt, Liegeakkorde, Schluss auf dem Grundton, rund 30 s); es wird im Hintergrund gerendert und in einer Welt mit Pausen von 60–180 s gespielt.
- **Warum:** Einfache, nachvollziehbare und testbare Regeln; Pentatonik klingt ohne Harmonielehre stimmig; Pausen verhindern Ermüdung.
- **Verworfen:** Biom-abhängige Umgebung (Biome sind Server-Daten); fest vorkomponierte Stücke (keine Abwechslung); Musik im Hauptmenü (unnötige Last beim Start).
