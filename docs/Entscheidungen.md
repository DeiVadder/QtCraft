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

## E-019 Abbau-Drops direkt ins Inventar

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

## E-022 Mobs werden nicht gespeichert, Inventar bleibt beim Tod

- **Was:** Mobs existieren nur um Spieler herum und verschwinden beim Schließen der Welt; beim Tod behält der Spieler sein Inventar und erscheint am Spawnpunkt.
- **Warum:** Kein Entity-Speicherformat nötig; Mobs erscheinen ohnehin ständig neu. Ohne Item-Entities (E-019) gäbe es keinen Weg, verlorene Gegenstände wiederzuholen.
- **Verworfen:** Mobs in Regionsdateien speichern; Inventar beim Tod verlieren.

## E-023 Entities als pro Frame erzeugtes Mesh

- **Was:** Mob-Modelle sind Quader aus den Daten; der Client erzeugt jedes Frame ein gemeinsames Mesh aller sichtbaren Entities (interpoliert, gedreht) und zeichnet es mit der Block-Pipeline. Licht unter Dächern wird grob angenähert (Spalte bedeckt → dunkler).
- **Warum:** Wenige Dutzend Quader kosten pro Frame kaum etwas, und es braucht weder Skelett-Animation noch eigene Shader.
- **Verworfen:** Eigene Instanzierungs-Pipeline (Aufwand ohne Messnutzen bei dieser Anzahl); Licht exakt aus dem Lichtvolumen (liegt nur in den Mesh-Jobs vor).
