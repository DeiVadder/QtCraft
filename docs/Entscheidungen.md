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
