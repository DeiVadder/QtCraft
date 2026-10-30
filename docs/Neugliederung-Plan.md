# Neugliederung der Ordner – Bestandsaufnahme und Plan (Paket 80.1)

**Umgesetzt (2026-10-29):** 80.2 `1b88edf` (Werkzeug), 80.3 `f9d17a2` (app), 80.4 `e989131` (clientcore), 80.5 `a082970` (server), 80.6 `2048dfb` (common/content), 80.7a `7b25841` (tests/fixtures), 80.7b `a119315` (Test-Themenordner), 80.8 Abschluss (Doku, E-938). Die geltenden Regeln stehen in E-418 und E-938, der aktuelle Stand je Ordner in `docs/Architektur.md`, „Source layout“; dieser Bericht bleibt als Stand vor der Umsetzung (Zahlen, Pfade und Fragen von 80.1).

Stand: `dev` 492ca05 (2026-10-29). Bericht zur Entscheidung des Menschen; es wird kein Code verschoben. Grundlage: E-418/E-419 (Ordner mit etwa 5–25 Klassen, Include-Wurzeln `src/` und `tests/`, Schichten), E-417 (`target_sources`-Blöcke je Ordner, `tests/<thema>/tests.cmake`), E-890 (Shared Libraries im Debug). Der Plan hängt nur an Pfaden, nicht an Dateiinhalten (39.1 setzt gerade SPDX-Köpfe).

**Zählweise:** „Klassen“ = Header (eine Klasse mit ihren `.cpp`-Teilen zählt einmal, wie in `FORTSCHRITT.md`); bei Tests die `.cpp` (ein Test je Datei) plus Hilfs-Header (`h`); QML-Ordner nach `.qml`. Fett = über 25.

## 1. Bestand

| Bibliothek | Ordner: Klassen/Dateien (QML) |
|---|---|
| `src/app` | admin 6/12, controllers 24/48, debug 5/10, display 3/6, hooks 12/25, input 9/18, models 17/33, multiplayer 5/9, preview 8/16, **rendering 26/53**, session 6/11, startup 4/9 |
| `src/app/qml` | (Wurzel) 1 QML, admin 13 QML, advancements 3 QML, character 5 QML, **controls 34 QML**, gamepad 7 QML, handbook 5 QML, hud 12 QML, inventory 17 QML, menus 23 QML, settings 13 QML |
| `src/audio` | (Wurzel) 8/15 |
| `src/clientcore` | admin 2/4, camera 8/16, control 16/31, debug 7/14, icons 4/8, input 12/24, light 9/16, maps 3/6, materials 5/10, **meshing 34/60**, network 2/4, player 16/31, preview 7/14, session 24/46, settings 13/26, sound 10/18, texturepacks 7/13, textures 22/43, tick 10/17, weather 3/6 |
| `src/common` | auth 5/10, blockrules 4/8, **content/definitions 54/60**, content/loader 2/4, **content/parser 41/83**, **content/registry 28/52**, core 23/34, diagnostics 14/28, gameplay 23/46, handbook 8/14, handbook/builder 12/23, light 5/8, maps 4/6, physics 10/18, protocol 24/45, world 18/29 |
| `src/headless` | (Wurzel) 15/31 |
| `src/network` | (Wurzel) 6/11, tls 5/11 |
| `src/server` | admin 3/6, advancements 3/5, blockentities 5/10, blocks 12/24, boats 3/6, chunks 10/20, **commands 28/55**, core 16/30, entities 17/20, explosions 7/14, gameplay 17/34, golems 4/7, guardians 4/7, hanging 3/6, items 4/8, login 3/6, machines 11/21, maps 5/10, minecarts 5/10, **mobs 30/56**, network 3/6, pathing 12/21, plants 5/10, projectiles 7/14, signal 17/33, spawners 3/6, spawning 5/10, storage 23/43, tick 10/18, villages 14/27, weather 4/8 |
| `src/worldgen` | dungeons 3/5, generators 14/22, mineshafts 11/20, showcase 12/22, structures 3/6, surface 3/6, terrain 22/46, towers 3/5, villages 18/35 |
| `src/worlds` | (Wurzel) 10/18 |
| `tests` | **(Wurzel) 0+25h**, **client 116+0h**, content 19+0h, diagnostics 3+0h, **gameplay 161+4h**, **network 28+1h**, sound 8+0h, **world 72+6h** |

**Über 25:** 8 Ordner in `src/` (`content/definitions` 54, `content/parser` 41, `clientcore/meshing` 34, `qml/controls` 34 QML, `server/mobs` 30, `content/registry` 28, `server/commands` 28, `app/rendering` 26) und 4 in `tests/` (`gameplay` 161 + 4 h, `client` 116, `world` 72 + 6 h, `network` 28 + 1 h), dazu 25 lose Hilfs-Header in `tests/`. Gegenüber 2026-10-28: `qml/controls` +1, `gameplay` +1, `client` +5, `world` +2; `network` ist neu über der Grenze.

**Knapp darunter (22–24), nicht teilen, beobachten:** `app/controllers` 24, `clientcore/session` 24, `common/protocol` 24 (45 Dateien: die 14 Codecs sind `.cpp`-Teile von `QcProtocolCodec`), `common/core` 23, `common/gameplay` 23, `server/storage` 23, `qml/menus` 23, `clientcore/textures` 22, `worldgen/terrain` 22. Eine Teilung hier wäre ohne klare zweite Zuständigkeit; erst teilen, wenn ein Ordner über 25 wächst.

## 2. Vorschlag je übergroßem Ordner (`src/`)

Alle Verschiebungen bleiben in ihrer Bibliothek; die Schichten (E-419, `LAYERS`) zählen nur die oberste Ordner-Ebene und ändern sich nicht. Klassen- und Dateinamen bleiben. Die vollständigen Dateilisten stehen im **Anhang A**; hier die Übersicht:

| Alt (Klassen) | Neu (Klassen) | Kern der Teilung |
|---|---|---|
| `common/content/definitions` 54, `parser` 41, `registry` 28 | `content/registry` 19, `blocks` 17, `blockbehaviour` 14, `items` 20, `recipes` 12, `mobs` 10, `villages` 10, `worldgen` 21 | **Nach Spielbereich statt nach Schicht:** Definition, Parser und Sync-Serialisierer einer Art liegen zusammen (`QcItemDefinition`, `QcItemParser`, `QcItemSerializer`). `registry` behält den Rahmen: Registry, Namen, `QcContentParser`, `QcContentSerializer`, Feldleser, Übersetzungen. |
| `clientcore/meshing` 34 | `meshing` 9, `entitymeshes` 17, `figures` 8 | Chunk-Abschnitte / Dreiecke der Entitäten / Pose und Animation von Figuren |
| `server/mobs` 30 | `mobs` 12, `mobbrains` 12, `animals` 6 | Verwaltung und Tick / Gehirne und Bewegungsarten / Tierhaltung und Zähmen |
| `server/commands` 28 | `commands` 23, neu `backup` 3; `QcAccessList` → `login`, `QcChatLog` → `admin` | Befehlsschicht und -gruppen bleiben; Sicherungen und Neuaufbau, Zugangsliste und Chat-Verlauf sind Dienste, keine Befehle |
| `app/rendering` 26 | `rendering` 20, `realistic` 6 | Realistischer Modus (Nachbearbeitung, Bloom, AO, Schatten, Wasser, Materialkarten) |
| `app/qml/controls` 34 QML | `controls` 18, neu `frames` 9, `settings` +7 (13 → 20) | Bausteine / Bildschirm- und Dialograhmen, Navigation / `Setting*`-Zeilen zu den Einstellungen |

**Abweichung von E-418:** E-418 hat `content/parser` bewusst nicht geteilt („eine Teilung nach Spielbereichen trennte `QcContentParser` von seinen Teilen“). Der Vorschlag teilt alle drei Content-Ordner zugleich nach Bereich; `QcContentParser` ruft die Bereichs-Parser dann aus `content/registry` auf, so wie die Registry heute schon alle Definitionen kennt. Alternative ohne Bruch mit E-418: die drei Ordner lassen und die Grenze für „ein Parser je Definitionsart“ als Ausnahme festschreiben (dann bleiben 3 Ordner über 25). → **Frage 1**.

**Namen:** `content/worldgen` und `content/mobs` wiederholen Namen anderer Bibliotheken (`src/worldgen`, `server/mobs`); die Pfade bleiben eindeutig, weil Includes ab `src/` geschrieben werden. `server/backup` hat nur 3 Klassen (unter „etwa 5“, aber klar benennbar); Alternative: zu `server/storage` (dann 26).

### Optional, ändert die Bibliothek (nicht Teil der Pakete)

Header, die heute nur eine höhere Bibliothek einbindet (geprüft über die Includes):

| Klasse(n) | heute | nur benutzt von | möglich nach | Empfehlung |
|---|---|---|---|---|
| `QcQuickMove`, `QcRepair`, `QcSlotList`, `QcPortableContainers`, `QcMapCrafting` | `common/gameplay` | `server` | `server/gameplay` | nein: Inventarregeln gehören zur gemeinsamen Spiellogik (Vorhersage des Clients möglich) |
| `QcPackComparison` | `common/protocol` | `server` | `server/login` | offen lassen: Vergleich gehört zum Login-Protokoll |
| `QcBodyFit`, `QcBodyGrid` | `common/physics` | `server` | `server/entities` | nein: Physik ist gemeinsam |
| `QcCommandSyntax` | `common` | `clientcore` | `clientcore/admin` | offen lassen: beschreibt Befehle beider Seiten |

Die 43 Header aus `clientcore`, die nur `app` benutzt, liegen dort mit Absicht (Client ohne GUI, eigens testbar) und sind keine Kandidaten.

## 3. Tests

**Heute:** `tests/CMakeLists.txt` setzt `tests/` als Include-Ordner und bindet je Thema `tests/<thema>/tests.cmake` per `include()` ein (nicht `add_subdirectory`, E-417): Pfade in `qc_add_test(… SOURCES gameplay/test_x.cpp QcServerTestFixture.h …)` gelten ab `tests/`, alle Testprogramme landen in `build/<preset>/tests/`, Datenordner `test-data/<name>`. Am Ende von `tests/CMakeLists.txt` stehen Labels, `COST`, `REQUIRED_FILES`, `PROCESSORS`, `qc_concurrency_tests` (nach Testnamen, also unabhängig vom Ordner); Zeit-Tests in `tests/timing.cmake`.

**Vorschlag:** Themenordner direkt unter `tests/` (eine Ebene wie heute, je mit eigenem `tests.cmake`), keine zweite Ebene `tests/gameplay/<x>`. `gameplay` und `client` verschwinden, `world` und `network` bleiben verkleinert, `scenarios` bekommt seine C++-Szenariotests dazu. Die Hilfs-Header der Themenordner wandern mit ihrem Thema (`QcVillageTestUtil.h` → `villages/`). Vollständige Listen im **Anhang B**.

| Aus | Neue Ordner (Dateien) |
|---|---|
| `gameplay` 165 | `servercore` 21, `scenarios` +7, `player` 17, `items` 14, `machines` 16, `signal` 17, `vehicles` 10, `mobs` 23, `creatures` 16, `environment` 8, `villages` 12 (+ 12 aus `world`), `protocol` 4 (+ 5 aus `network`) |
| `client` 116 | `meshing` 18, `rendering` 22, `textures` 13, `session` 18, `input` 12, `screens` 19, `controls` 14 |
| `world` 78 | `terrain` 21, `structures` 12, `showcase` 6, `storage` 10, `villages` 12; `world` behält 17 |
| `network` 29 | `protocol` 5; `network` behält 24 |
| `tests/*.h` 25 | `tests/fixtures/` 25 (alle; Include dann `"fixtures/QcServerTestFixture.h"`) |

Der Ordner heißt `servercore`, nicht `server`, damit kein Ordner unter `tests/` wie eine oberste Ebene von `src/` heißt (`"server/…"` würde sonst zuerst unter `tests/` gesucht). `tests/network` ist schon heute so ein Fall; er schadet nicht, weil dort kein Header `network/…` liegt.

**Was sich an der CMake-Verdrahtung ändert:** je neuem Ordner ein `tests.cmake` (Kopfkommentar wie heute) und eine `include()`-Zeile in `tests/CMakeLists.txt`; die `qc_add_test`-Zeilen ziehen in der bisherigen Reihenfolge in die Datei ihres neuen Ordners, ihre Pfade schreibt `fix-includes.py` um. Testnamen, Build-Ordner, Datenordner, Labels, `COST`, `REQUIRED_FILES` und `qc_concurrency_tests` bleiben. `tests/fixtures` bekommt kein `tests.cmake` (nur Header). Jede Änderung an `*.cmake` wählt bei `--changed` ohnehin alle Tests.

**Zu beachten:** `test_lazy_screen` lädt eine `Test.qml` aus `QTCRAFT_QML_CONTROLS_DIR` (`tests/client/tests.cmake`, ein Ordner-Pfad, den `fix-includes.py` nicht umschreibt) und braucht `LazyScreen.qml` daneben; zieht `LazyScreen` nach `qml/frames`, muss die Definition mit. `test_mob_crowd_measure` prüft `__has_include("server/mobs/QcCreatureBodies.h")` (bleibt in `server/mobs`); `__has_include` schreibt das Skript ebenfalls nicht um. *(Umgesetzt in 80.3: die Definition heißt jetzt `QTCRAFT_QML_FRAMES_DIR` und zeigt auf `qml/frames`, seit 80.7b in `tests/controls/tests.cmake`.)*

## 4. Reihenfolge der Umsetzung (80.2 ff.)

Voraussetzung: **39.1 (SPDX-Köpfe) ist gemergt**, sonst trifft jede Verschiebung eine geänderte Datei. Pakete strikt nacheinander, jedes vor dem nächsten nach `dev`; während eines Pakets keine anderen Pakete in derselben Bibliothek (ein neuer Branch, der eine Datei im alten Ordner anlegt, legt ihn wieder an). Jedes Paket: `git mv`, Skript, CMake-Blöcke, Docs-Pfade, Code-Karte, Review, `verify.sh --quick` (alle Tests, weil CMake-Dateien geändert sind); kein TSan-Schritt nötig (keine Logik), voller Verify am Ende der Etappe.

| Paket | Inhalt | verschoben | berührt (Includes) | Größe |
|---|---|---:|---:|---|
| 80.2 | Werkzeug (Abschnitt 5) mit Selbsttest | – | – | S |
| 80.3 | `qc_app_core`: `rendering` → `realistic`; QML `controls` → `frames`, `settings`; `test_lazy_screen` | 6 Kl. + 16 QML | ~65 Dateien, 16 `.ts`-Orte | S |
| 80.4 | `qc_clientcore`: `meshing` → 3 Ordner | 25 Kl. | ~120 | S |
| 80.5 | `qc_server`: `mobs` → 3, `commands` → `commands`/`backup` + 2 Einzelne | 23 Kl. | ~170, 381 `.ts`-Orte | M |
| 80.6 | `qc_common`: Content → 8 Ordner | 115 Kl., ~180 Dateien | ~570 (fast das ganze Projekt) | L, allein laufen lassen |
| 80.7a | `tests/fixtures` | 25 h | ~320 Testdateien | M |
| 80.7b | Test-Themenordner, `tests.cmake` neu aufteilen | ~350 Tests | ~25 `tests.cmake` | M–L |
| 80.8 | Abschluss: `docs/Architektur.md` „Source layout“, CLAUDE.md-Beispiele, E-Nummer, voller Verify | – | – | S |

**Warum diese Reihenfolge:** klein vor groß (80.3 prüft das Werkzeug an QML, Plattformdateien und Typregistrierung), die alles berührende Content-Teilung allein in einer ruhigen Phase, Tests zuletzt (jede `src`-Verschiebung schreibt Includes in Tests um; die `tests.cmake` werden nur einmal neu geschnitten). 80.3–80.5 berühren sich kaum und könnten notfalls parallel laufen; empfohlen ist nacheinander.

**Risiken**

| Risiko | Lage | Gegenmittel |
|---|---|---|
| QML-Modul | ein Modul `QtCraft` für alle QML-Dateien, Typen nach Dateiname; keine `import "../…"` (geprüft). Pfade nur in `QML_FILES` und `set_source_files_properties(qml/controls/Ui.qml …)` | `fix-includes.py` schreibt `.qml`-Pfade in CMake schon um; `Ui.qml` bleibt in `controls` |
| Typregistrierung (QTBUG-93443) | `INCLUDE_DIRECTORIES` der erzeugten `qc_app_core_qmltyperegistrations.cpp` nennt die App-Ordner mit QML-Typen von Hand; `admin` (3 Typen) fehlt dort, gebaut wird trotzdem | in 80.2 klären, ob die Liste noch nötig ist; sonst bei jedem neuen Ordner mit QML-Typen ergänzen (`realistic` hat keine) |
| Übersetzungen | `.ts`-Orte (`<location filename>`) | `fix-includes.py` schreibt sie um; danach `update_translations` und prüfen, dass `de.json` vollständig bleibt |
| clangd-Index | Hintergrund-Index kennt alte Pfade bis zum Neuaufbau (~20 min) | nach jedem Merge Debug-Build im Haupt-Checkout auffrischen (Regel „Nach jedem Merge“) |
| `code-map.py` | läuft generisch über `src/` und `tests/` | nach jedem Merge neu erzeugen; nichts zu ändern |
| ccache / Build | verschobene Quellen verfehlen den Cache einmal; je Paket ein Neuaufbau der Bibliothek | Builds über den Slot-Broker; Platz für einen frischen Worktree (~40 GB) einplanen |
| Docs mit Pfaden | `Architektur.md` „Source layout“ (Zahlen schon heute veraltet), `CLAUDE.md` (Fixture-Includes), `Modding.md`, `Steuerschnittstelle.md`, `Performance.md`, `UI-Leitfaden.md`, Kommentare in `bench-headless.sh`/`bench-ab.sh` | je Paket mitziehen; `Entscheidungen.md`, `Log.md`, `FORTSCHRITT.md`-Verlauf und `docs/archiv` bleiben als Historie |
| Screenshots | Dateinamen hängen am Testnamen, nicht am Pfad; Testprogramme bleiben in `build/<preset>/tests/` (`bench-ab.sh` erwartet dort `test_scenario_measure`) | nichts zu tun |
| A/B-Messungen gegen alte Commits | alte Builds haben alte Pfade | betrifft nur Quelltext; die Programme heißen gleich |
| Offene Branches anderer Pakete | Umbenennung + Änderung im selben Merge | Git erkennt Umbenennungen; nach dem Merge `check-cmake-lists.py` und `fix-includes.py --check` |

## 5. Werkzeug

`fix-includes.py` arbeitet über den Dateinamen (Namen sind projektweit eindeutig): nach beliebig vielen `git mv` – auch eines ganzen Ordners auf einmal – schreibt **ein** Lauf alle Includes, die Pfade in allen CMake-Dateien unter `src/` und `tests/` (auch `.qml` und die `tests.cmake`) und die `.ts`-Orte um. Es fehlt:

1. **Zeilen in den richtigen `target_sources`-Block.** Heute von Hand; `check-cmake-lists.py` meldet nur. Bei 80.6 sind das ~200 Zeilen in 8 neuen Blöcken. → kleines Skript `scripts/move-classes.py <plan>`: Plan als Zeilen `<Klasse> <neuer Ordner>`, verschiebt Header samt `.cpp`/`.mm`-Teilen (längster Header-Präfix, wie `QcWorldRendererShadows.cpp` zu `QcWorldRenderer`), ruft `fix-includes.py` und sortiert die Zeilen in die Blöcke (neuer Block an seinem Platz, alphabetisch; `QML_FILES` sortiert).
2. **Tests:** dasselbe für `qc_add_test`-Zeilen (in das `tests.cmake` des neuen Ordners, mit den direkt folgenden `target_compile_definitions`/`add_dependencies` desselben Tests) und die `include()`-Zeilen in `tests/CMakeLists.txt`.
3. **Nicht umgeschrieben:** `__has_include("…")`, Ordner-Pfade in Compile-Definitionen (`QTCRAFT_QML_CONTROLS_DIR`), die `INCLUDE_DIRECTORIES`-Liste der Typregistrierung. Für die ersten beiden reicht eine Warnung im Skript (Pfad existiert nicht mehr).
4. Optional: `code-map.py --layout` gibt die Tabelle „Source layout“ mit Zahlen aus, damit `Architektur.md` nicht wieder veraltet.

Die Pläne dieses Berichts liegen als Tabellen im Anhang und lassen sich direkt in das Planformat von Punkt 1 übertragen.

## 6. Zahlen

| | vorher | nachher |
|---|---:|---:|
| Ordner mit Code in `src/` | 104 | 116 |
| Ordner mit Code in `tests/` | 8 | 29 |
| davon über 25 | 12 (+ 25 lose Header) | 0 |
| Pakete | – | 7 (80.2–80.8, 80.7 zweigeteilt) |

## 7. Fragen an den Menschen

1. Content nach Spielbereich teilen (Abschnitt 2, weicht von E-418 ab) – oder die drei Content-Ordner als Ausnahme lassen?
2. Test-Themenordner direkt unter `tests/` (eine Ebene, 23 neue) statt Unterordner von `gameplay`/`client`/`world`?
3. Sehr kleine Ordner wie `server/backup` (3) in Ordnung, oder lieber in `server/storage` (dann 26)?
4. Die Kandidaten „ändert die Bibliothek“ alle liegen lassen (Empfehlung)?
5. Namen recht so? (`blockbehaviour`, `entitymeshes`, `figures`, `mobbrains`, `animals`, `realistic`, `frames`, `servercore`, `creatures`, `environment`)

## Anhang A: Dateilisten `src/`

Teile einer Klasse (`.cpp`, `_mac.mm`, `_other.cpp`) wandern mit ihrem Header. „Kl.“ = Header, „Dateien“ = alle Dateien des neuen Ordners aus dieser Teilung.

#### `common/content/definitions`, `common/content/parser`, `common/content/registry`

| Neuer Ordner | Kl. | Dateien | Zuständigkeit | Klassen (Header-Stamm; Teile `.cpp` wandern mit) |
|---|---|---|---|---|
| `common/content/registry` | 19 | 33 | Kern: Registry, Namen, Rahmen von Parser und Sync, Übersetzungen, Fortschritte, Befehls-Makros, Klänge | QcAdvancementCatalog, QcAdvancementDefinition, QcAdvancementParser, QcAdvancementSerializer, QcAssetCatalog, QcCommandMacroDefinition, QcCommandMacroParser, QcContentFields, QcContentNames, QcContentParser, QcContentRegistry, QcContentSerializer, QcJsonFieldReader, QcNamedList, QcPackReference, QcSoundDefinition, QcSoundKeys, QcSoundParser, QcTranslations |
| `common/content/blocks` | 17 | 25 | Blöcke: Definition, Eigenschaften, Zustände, Formen, Texturen, Materialien und Bauteile | QcBlockDefinition, QcBlockParser, QcBlockProperty, QcBlockSerializer, QcBlockState, QcBlockStateTable, QcBlockTextureParser, QcMaterialDefinition, QcMaterialParser, QcPartFamily, QcPartGenerator, QcShapeCondition, QcShapeDefinition, QcShapeParser, QcShapePart, QcTextureAnimation, QcTextureDefinition |
| `common/content/blockbehaviour` | 14 | 21 | Verhalten von Blöcken: Explosion, Feuer, Flüssigkeiten, Signal-Staub, Schienen, Spawner, Aufgehängtes | QcBlastDefinition, QcBlastParser, QcFireDefinition, QcHangingDefinition, QcHangingParser, QcLiquidDefinition, QcLiquidParser, QcLiquidReaction, QcRailParser, QcSignalDefinition, QcSignalLinks, QcSignalParser, QcSpawnerContent, QcSpawnerDefinition |
| `common/content/items` | 20 | 33 | Items: Definition, Rüstung, Halten, Verzauberung, Effekte, Geschosse, Erfahrung, Amboss | QcAnvilSwap, QcAnvilSwaps, QcArmourDefinition, QcArmourParser, QcArmourSerializer, QcEffectDefinition, QcEffectParser, QcEnchantmentDefinition, QcEnchantmentParser, QcEnchantmentSerializer, QcExperienceDrop, QcHitEffect, QcHoldDisplay, QcHoldParser, QcHoldSerializer, QcItemDefinition, QcItemParser, QcItemSerializer, QcProjectileDefinition, QcProjectileParser |
| `common/content/recipes` | 12 | 19 | Was woraus wird: Rezepte, Maschinen, Schmelzen, Kochen, Feldbau, Steinsäge, Beutetabellen | QcCookingDefinitions, QcCookingParser, QcFarmingDefinitions, QcFarmingParser, QcLootTableDefinition, QcLootTableParser, QcLootTables, QcMachineDefinition, QcRecipeDefinition, QcRecipeParser, QcSmeltingRecipe, QcStonecutting |
| `common/content/mobs` | 10 | 16 | Wesen: Definition, Eigenschaften, Modelle, Aussehen von Spielern, Spawn-Gewichte | QcAppearance, QcAppearanceDefinition, QcMobDefinition, QcMobParser, QcMobReader, QcMobSerializer, QcMobTraits, QcMobTraitsContent, QcModelDefinition, QcSpawnWeights |
| `common/content/villages` | 10 | 17 | Dörfer und Dorfbewohner: Baupläne, Stile, Kleidung, Handel | QcTradeDefinitions, QcTradeParser, QcVillageCatalog, QcVillageContent, QcVillageDefinitions, QcVillageLayoutParser, QcVillageParser, QcVillagerDefinitions, QcVillagerOutfits, QcVillagerParser |
| `common/content/worldgen` | 21 | 31 | Daten der Weltgenerierung: Biome, Gelände, Strukturen, Vegetation, Welttypen und -optionen, Wetter, Showcase | QcCoastalTowerDefinition, QcCoastalTowerParser, QcDungeonDefinition, QcMineshaftDefinition, QcShowcaseDefinition, QcShowcaseParser, QcStructureDefinition, QcStructureParser, QcStructureSiteParser, QcStructureVariationParser, QcVegetationDefinition, QcVegetationParser, QcWeatherDefinition, QcWeatherParser, QcWorldOptionDefinitions, QcWorldOptionParser, QcWorldTypeDefinitions, QcWorldTypeParser, QcWorldgenCatalog, QcWorldgenDefinitions, QcWorldgenParser |

#### `clientcore/meshing`

| Neuer Ordner | Kl. | Dateien | Zuständigkeit | Klassen (Header-Stamm; Teile `.cpp` wandern mit) |
|---|---|---|---|---|
| `clientcore/meshing` | 9 | 14 | Chunk-Abschnitte: Mesher, Planer, Nachbarschaft, Vertex-Formate, Packen, Flüssigkeitsoberfläche | QcBlockRenderTable, QcBlockVertex, QcChunkMesh, QcChunkMesher, QcIndexPacking, QcLiquidSurface, QcMeshNeighbourhood, QcMeshScheduler, QcPackedMesh |
| `clientcore/entitymeshes` | 17 | 31 | Dreiecke der Entitäten je Bild: Wesen, Items in der Hand, Boote, Geschosse, Schilder, Rahmen, Fäden | QcBoatModel, QcBoatRiders, QcEnchantingMesher, QcEntityCulling, QcEntityMesh, QcEntityMesher, QcEntityVertex, QcHangingMesher, QcHeldItemMesher, QcMeshRotation, QcProjectileMesher, QcSignTextCache, QcSignTextMesher, QcSpriteShape, QcSpriteShapes, QcTetherMesher, QcTethers |
| `clientcore/figures` | 8 | 15 | Aussehen und Bewegung von Figuren: Pose, Animator, Beine, Umhang, Handschwung, Rüstung | QcAnimator, QcArmourLook, QcBoxLegs, QcCapeSwing, QcCapeTracker, QcEntityPose, QcFigureLook, QcHandSwing |

#### `server/mobs`

| Neuer Ordner | Kl. | Dateien | Zuständigkeit | Klassen (Header-Stamm; Teile `.cpp` wandern mit) |
|---|---|---|---|---|
| `server/mobs` | 12 | 23 | Kern der Wesen: Manager, Verhalten je Tick, Austausch, Gedränge, Erhalt, Körper, Eigenschaften | QcCreatureBodies, QcCreatureTraits, QcInfestation, QcMobBehaviour, QcMobCramming, QcMobCrowd, QcMobDrinking, QcMobExchange, QcMobManager, QcMobUpkeep, QcNameTags, QcServerCreatures |
| `server/mobbrains` | 12 | 21 | Entscheiden und Bewegen: Gehirne je KI-Art, Zustände, Lenkung, Flucht, Schwimmen, Schweben, Teleport | QcArcherBrain, QcArcherState, QcCreatureBrains, QcFishSwimming, QcMobBrain, QcMobFear, QcMobHover, QcMobSteering, QcMobTeleport, QcNeutralBrain, QcTameBrain, QcTameState |
| `server/animals` | 6 | 12 | Tierhaltung: Füttern, Züchten, Familien, Eierlegen, Zähmen, Besitzer | QcMobBreeding, QcMobFamilies, QcMobLaying, QcPetCare, QcPetOwners, QcServerAnimals |

#### `server/commands`

| Neuer Ordner | Kl. | Dateien | Zuständigkeit | Klassen (Header-Stamm; Teile `.cpp` wandern mit) |
|---|---|---|---|---|
| `server/commands` | 23 | 45 | Befehlsschicht (Register, Quelle, Rechte, Argumente, Hilfe, Bestätigung), Befehlsgruppen, Spielregeln | QcAdvancementCommands, QcAdventureCommands, QcBlockCommands, QcCheatCommands, QcChunkCommands, QcCommandArguments, QcCommandHelp, QcCommandRegistry, QcCommandSource, QcEffectCommands, QcExperienceCommands, QcGameRules, QcLimitCommands, QcMobCommands, QcPendingCommands, QcPermission, QcPlayerCommands, QcServerCommands, QcStandCommands, QcTickCommands, QcVillageCommands, QcWeatherCommands, QcWorldCommands |
| `server/backup` | 3 | 6 | Sicherungen und Neuaufbau: Weltkopie, Sicherungs-Thread, Chunks neu erzeugen | QcBackupRunner, QcChunkRegeneration, QcWorldBackup |
| `server/login` | 1 | 2 | (bestehend, +1) Zugangsliste: wer beitreten darf, wer Operator ist | QcAccessList |
| `server/admin` | 1 | 2 | (bestehend, +1) Chat-Verlauf für den Admin-Kanal | QcChatLog |

#### `app/rendering`

| Neuer Ordner | Kl. | Dateien | Zuständigkeit | Klassen (Header-Stamm; Teile `.cpp` wandern mit) |
|---|---|---|---|---|
| `app/rendering` | 20 | 41 | Weltdarsteller, Bildplan, GPU-Puffer und -Texturen, Himmel, Niederschlag, Linien | QcAtlasTexture, QcBufferWrite, QcDebugLineRenderer, QcDrawOrigins, QcEntityFrameBuilder, QcFramePlan, QcFramedMapTexture, QcGpuMeshStore, QcPrecipitationRenderer, QcRangeAllocator, QcRenderBridge, QcRenderPipelines, QcRenderSample, QcSectionArena, QcSectionLightPool, QcSkyRenderer, QcTextureStream, QcWorldRenderer, QcWorldRendererResources, QcWorldView |
| `app/realistic` | 6 | 12 | Realistischer Modus: Nachbearbeitung, Bloom, Umgebungsverdeckung, Schatten, Wasser, Materialkarten | QcAmbientOcclusion, QcBloomChain, QcMaterialTextures, QcPostProcess, QcShadowMap, QcWaterSurface |

#### `app/qml/controls` (QML)

| Neuer Ordner | QML | Zuständigkeit | Dateien (`.qml`) |
|---|---|---|---|
| `app/qml/controls` | 18 | Grundbausteine: Knöpfe, Schalter, Felder, Slots, Hinweise, Diagramm, Theme-Elemente, Ui-Singleton | Bevel, ControlSlot, FocusOutline, GameButton, GameLogo, GameSwitch, GameTextField, Hint, ItemSlot, KeyBadge, LineChart, ListRow, Segments, ThemedComboBox, ThemedScrollBar, ThemedToolTip, Ui, WindowFont |
| `app/qml/frames` | 9 | Rahmen von Bildschirmen und Dialogen, Navigation | LazyScreen, ModalDialog, NavButton, NavList, ScreenFrame, ScreenHeader, SideBar, SidebarScreen, UnsavedChangesDialog |
| `app/qml/settings` | 7 | (bestehend, +7) Zeilen und Gruppen der Einstellungen | SettingCheck, SettingChoice, SettingDropdown, SettingGroup, SettingRow, SettingSlider, SettingSwitch |

## Anhang B: Dateilisten `tests/`

Namen ohne `test_`-Vorsilbe und ohne `.cpp`; Hilfs-Header mit Endung.

| Ordner | Dateien | aus | Zuständigkeit | Dateien |
|---|---|---|---|---|
| `tests/protocol` | 9 | gameplay 4, network 5 | Protokoll, Codecs und Transporte | admin_protocol, chunk_protocol, entity_protocol, game_mode_protocol, latency, login_protocol, protocol, tcp_transport, tls_credentials |
| `tests/servercore` | 21 | gameplay 21 | Server-Kern: Befehle, Spielerverwaltung, Takt, Grenzen, Chunks, Prüfung von Eingaben, Sicherungen | block_entities, block_entity_compare, block_entity_measure, chunk_tools, chunk_unloading, entity_compare, entity_measure, entity_model, light_measure, player_management, server_commands, server_limits, server_validation, simulation_limits, tick_control, tick_phases, tick_scheduler, tick_systems, world_backup, world_maintenance, world_open_measure |
| `tests/scenarios` | 7 | gameplay 7 | (bestehend: Skriptläufe) + Szenario- und Gesamtläufe über die Steuerschnittstelle | client_server, explore_measure, gamepad_scenarios, headless_cli, scenario_measure, scenarios, survival_scenario |
| `tests/player` | 17 | gameplay 17 | Spieler: Spielmodi, Gesundheit, Hunger, Effekte, Erfahrung, Tod, Rüstung, Kampf, Geschosse, Angeln, Fortschritte | advancements, adventure_mode, adventure_rules, ammunition, armour_server, armour_stands, creative_mode, death_messages, experience, experience_sources, fishing, hunger, invulnerability, projectile_measure, projectiles, spectator_mode, status_effects |
| `tests/items` | 14 | gameplay 14 | Items und Blöcke im Spiel: Item-Daten, Bauen, Feldbau, Schilder, Rahmen, Karten | building, dirt_paths, farming_server, hanging, item_data, item_data_limits, item_data_paths, items, map_frames, map_store, maps, server_items_and_saves, sign_colours, signs |
| `tests/machines` | 16 | gameplay 16 | Maschinen und Behälter: Öfen, Trichter, Amboss, Verzaubern, Steinsäge, Rucksack, Crafting | QcEnchantingFixture.h, anvil, anvil_screen, anvil_variants, backpack, backpack_hand, backpack_world, enchanted_books, enchanting, enchanting_table, hopper_lines, machines, named_containers, quick_move, server_crafting, stonecutter |
| `tests/signal` | 17 | gameplay 17 | Signal-Staub: Schaltungen, Kolben, Trichter, Leser, Fallen, Notenblock | note_block, signal_bodies, signal_circuits, signal_dispensers, signal_hopper_edges, signal_hoppers, signal_indicator, signal_measure, signal_network, signal_parity, signal_pistons, signal_rails, signal_readers, small_signal_torch, target_block, temple_traps, tripwire |
| `tests/vehicles` | 10 | gameplay 10 | Loren, Schienen und Boote | QcBoatFixture.h, boat_measure, boat_riders, boats, cart_containers, minecart_measure, minecart_momentum, minecart_riders, minecarts, rail_layouts |
| `tests/mobs` | 23 | gameplay 23 | Wesen: Verhalten, Wege, Gedränge, Körper, einzelne Arten | QcShadewalkerFixture.h, archers, body_fit, creature_minds, creature_parts, door_steering, entity_push, fish, kjut, kjut_measure, mob_crowd_measure, mob_crowds, mob_departures, mob_navigation, mob_path_measure, mob_paths, mob_width, mobs, shadewalker_rules, shadewalkers, silverfish, spiders, witches |
| `tests/creatures` | 16 | gameplay 16 | Entstehen und Zähmen: Spawnen, Spawner, Golems, Tiere, Haustiere | QcPetFixture.h, biome_spawning, chickens, cows, creature_summons, golem_measure, golem_summons, iron_golems, monster_spawning, monster_variants, pet_rules, sand_golems, spawn_measure, spawners, taming, witch_hut_spawns |
| `tests/environment` | 8 | gameplay 8 | Welt-Ereignisse: Explosionen, Feuer, Wetter, Leere, Rutschiger Boden | explosion_limits, explosions, fire, fire_sources, slippery_ground, void_zone, weather, weather_effects |
| `tests/villages` | 24 | gameplay 12, world 12 | Dörfer: Generierung (qc_worldgen) und Dorfleben, Handel (qc_server) | QcVillageFixture.cpp, QcVillageFixture.h, QcVillageTestUtil.h, settlement_storage, trade_offers, trading, village_bounds, village_fields, village_generation, village_growth, village_layouts, village_life, village_limits_measure, village_measure, village_plan_cache, village_plan_hash, village_soak, village_species, village_swamp, village_variety, villager_measure, villager_walks, villagers, villages |
| `tests/meshing` | 18 | client 18 | Meshing von Chunks und Entitäten, Figuren | armour_mesh, boat_look, box_legs, cape, cape_swing, entity_culling, hand_swing, held_item, held_item_name, hold_content, liquid_surface, mesh_measure, mesh_scheduler, mesher, mesher_shapes, mesher_signal_shapes, packed_mesh, water_look |
| `tests/rendering` | 22 | client 22 | Darstellung auf der GPU: Bildplan, Puffer, Arena, Licht, Schatten, Nachbearbeitung, Himmel, Shader | draw_origins, frame_driver, frame_plan, frame_uniforms, framed_map_texture, gpu_mesh_upload, label_projector, material_maps, material_memory, material_textures, post_process, range_allocator, render_culling, section_arena, section_light, shader_variants, shadow_cascades, shadow_map, sky, texture_animation_shader, water_surface, world_renderer_uploads |
| `tests/textures` | 13 | client 13 | Texturen, Atlas, Texturpakete, Symbole | app_icon, atlas_fill, block_icons, item_icon_cache, mob_textures, pack_switch_hitch, preview_atlas_cache, texture_animation, texture_atlas, texture_pack_controller, texture_packs, texture_synthesizer, web_handbook |
| `tests/session` | 18 | client 18 | Client-Sitzung: Takt, Vorhersage, Abgleich, Interpolation, Licht, Wetter, Karten, Ton der Sitzung | block_light_engine, block_mirror, block_prediction, block_prediction_measure, client_maps, debug_report, interpolation, kjut_threads, moving_lights, prediction, push_bodies, reconciliation, session_audio, session_thread, session_tick, sprint_feel, weather_client, world_pause |
| `tests/input` | 12 | client 12 | Eingabe: Gamepad, Tastenbelegung, Maus, Kamera | camera, camera_relative, gamepad_controls, gamepad_input, gamepad_mapping, gamepad_navigation, gamepad_profiles, gamepad_screens, gamepad_sdl, key_bindings, mouse_capture, onscreen_keyboard |
| `tests/screens` | 19 | client 19 | Controller und Modelle der Bildschirme: Admin, Fortschritte, Figur, Chat, Inventar, Karten, Welten | admin_command_index, admin_controller, advancement_tree, advancement_tree_model, backpack_screen, character, character_draft, character_preview, chat_feed, creative_tabs, enchanting_offer, framed_maps, hud_target, map_hud, multiplayer_ui, rune_script, sign_controller, world_map_preview, world_options_controller |
| `tests/controls` | 14 | client 14 | Oberfläche allgemein: Bausteine, Einstellungen, Maßstab, Farben, Enums | control_palette, dials, display_options, display_settings, graph_item, lazy_screen, modal_dialog, screen_choice, setting_controls, settings, themed_tooltip, ui_enums, ui_palette, ui_scale |
| `tests/terrain` | 21 | world 21 | Gelände: Rauschen, Klima, Biome, Flüsse, Flüssigkeiten, Vegetation, Erze, Generator-Versionen, Welttypen | QcTerrainTestUtil.h, QcWaterTestUtil.h, biome_distribution, climate_order, cold_edges, flower_valley, generator_version_worlds, generator_versions, giant_trees, lava_lakes, noise, ores_metals, river_deltas, rivers, spawn_ground, terrain_generator, terrain_liquids, terrain_measure, vegetation, world_options, world_types |
| `tests/structures` | 12 | world 12 | Strukturen der Generierung: Tempel, Verliese, Minen, Türme, Ruinen, Gebäude | building_shapes, building_variants, coastal_towers, desert_temples, dungeons, jungle_temples, locate_places, mineshaft_inclines, mineshafts, structures, surface_ruins, temple_entrances |
| `tests/showcase` | 6 | world 6 | Showcase-Welt | QcShowcaseTestUtil.h, showcase, showcase_griefing, showcase_stations, showcase_world, signal_showcase |
| `tests/storage` | 10 | world 10 | Speichern: Chunk-Speicher, Weltdateien, Weltbibliothek, Block- und Entitätsdaten | QcChunkChecksum.h, block_entity_components, block_entity_storage, chunk_store, entity_components, entity_storage, persistence, world_details, world_library, world_storage |
| `tests/world` | 17 | world 17 | (bleibt) Chunks, Abschnitte, Licht, Physik, Flüssigkeiten, Blöcke, Pflanzen | block_light, boat_physics, body_push, chunk, coordinates, farming, flight, interaction, kitchen_blocks, light_engine, liquids, new_blocks, physics, plant_growth, section_access, section_snapshots, server_light |
| `tests/network` | 24 | network 24 | (bleibt) Netzspiel: Bots, Anmeldung, Admin-Kanal, Pausen, TCP-Server | QcAdminTestFixture.h, admin_channel, admin_settings, advancement_client, adventure_client, backpack_control, boat_bots, client_admin, creative_bots, creative_client, dedicated_server, ice_client, machine_bots, minecart_bots, multiplayer_bots, network_client, network_game, pause_keepalive, push_client, server_log, server_login, server_pause, signal_bots, void_client |

`tests/fixtures`: alle 25 Header aus `tests/` (QcBlockEntityDump, QcBotProcess, QcCircuitFixture, QcDumpDiff, QcLatencyTransport, QcMachineFixture, QcPlayerUseFixture, QcPredictionFixture, QcRailFixture, QcRawClient, QcRawWorkstations, QcRawWorldRecord, QcRecordingTransport, QcServerTestFixture, QcSessionMeshes, QcStoredWorld, QcTestLogin, QcTestMachines, QcTestProtocol, QcTestTls, QcTestUtil, QcTestWorld, QcTestWorldFiles, QcUnreadableFile, QcWorldMesh).
