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
