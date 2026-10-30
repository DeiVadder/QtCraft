# Log QtCraft

Datierter Verlauf der Stände, die früher als „Laufender Stand“ und weitere Stand-Zeilen in `docs/FORTSCHRITT.md` standen, neueste zuerst; ein Abschnitt je Aktualisierung. Der aktuelle Stand steht kurz in `docs/FORTSCHRITT.md` („Aktueller Stand“); neue Einträge kommen hier oben dazu. Die Texte sind unverändert übernommen, nur in Absätze geteilt.

## 2026-10-25/26 (aus FORTSCHRITT verschoben am 2026-10-28)

- **Stand (2026-10-26 22:45):** seit 18:45 in `dev`: T2 schlanke Sanitizer-Builds (`554297a`, E-890: Shared Libraries in debug/asan/tsan, ASan-Baum −66 %, TSan −58 %, Relink nach Bibliotheksänderung 1 s, ccache 20 GB eingerichtet), Fenster-A/B nach 79.14 (crowd GUI-CPU +10…15 %, vom Menschen hingenommen, als Leistungs-Reserve notiert), 79.16 Wüstentempel wie im Vorbild plus Dorf-Tempel-Boden (`d0f8df9`, E-893, vom Menschen freigegeben), 39.4 Schattenwandler/Schattenperle (`e515b92`, E-894); Broker v4 läuft. **Läuft:** zweiteiliger voller Verify auf `e515b92` (ASan, dann TSan mit Überspringen der geteilten Schritte). **Danach:** Zeitgrenzen aus den LIMIT WARNINGs gesammelt anpassen (`test_latency` 270, `test_mineshafts` 630, `test_section_access` 180, `test_block_prediction` 150, COST `test_jungle_temples` 70, `test_temple_entrances` 60), 79.15 (Spinnenlauf, diagonaler Minen-Eingang), 79.17 (nur auf Freigabe), 45, 50.2, 74.5, 73.4, 41.2b/c.
- **Stand (2026-10-26 18:45):** heute in `dev`: `test_machine_bots` an E-887 angepasst (`2ef66a0`), 79.14a Mauszeiger/Augen/Laufanimation (`398f6ed`, E-888), 79.14b Tempel-Angleichung, Dschungel-Bewuchs, große Minenschächte, Trockental (`43247b7`, E-889), 50.3 Prüfskript Getter/Setter (`565ba59`, E-891), 39.3 README/Playing/Building/HowItWorks/CONTRIBUTING (`43e77ae`), T1e Broker: lange Läufe nie unter 4 Jobs (`c39b083`, E-892; laufenden v3-Broker im ersten ruhigen Moment mit `scripts/qc-slot.py stop` ersetzen). **Läuft:** T2 schlankere Sanitizer-Builds (`feature/t2-lean-sanitizer-builds`, E-890 reserviert, Messung vorher/nachher). **Voller Verify** über 45.0b, 48.2, 79.10–79.14, T2 steht aus (ASan-Teil 2026-10-26 grün bis auf den inzwischen behobenen `test_machine_bots`, TSan-Teil wegen Plattenplatz nach T2). **Danach:** 79.15/79.16 (gesammelt), 39.4 Enderman-Name, 45, 50.2, 74.5, 73.4, 41.2b/c.
- **Übergabe (2026-10-25 17:00, Wochenbudget 96 %, Reset Montag 2026-10-26 02:00):** `dev` steht auf `e8d2f50` plus diesem Übergabe-Commit, alles gemergt, **keine offenen Feature-Branches und keine laufenden Agenten**; nur die Worktrees `playtest-0da2a4f` (Mac-Server-Stand der ersten Zip, kann weg) und ein alter `cpp-coreguidelines-violations-58f944` (50.2-Vorarbeit) liegen noch. Protokoll **80**. Main-Checkout hat einen frischen Debug-Build, Code-Karte aktuell, 128 GiB frei.
  - **Heute gemergt (24./25.10.):** 48.1, 79.5, 79.6, 79.4, 74.8, 73.5, 79.7, 45.0, 79.9, 41.3, 48.2, 45.0b, 79.10, 79.11, 79.12 (Einzelheiten in der Stand-Zeile darunter und in E-872…E-887).
  - **Voller Verify zuletzt grün** auf `d62e4c8` (nach 41.3). **Danach ungeprüft im vollen Lauf:** 48.2, 45.0b, 79.10, 79.11, 79.12 (je Quick-Verify grün, zentraler TSan grün für 45.0b, 79.10, 79.11). **Erster Schritt nach dem Reset:** voller Verify in zwei Teilen (`--full --sanitizers asan`, dann `build/asan` löschen und mit `du` prüfen, dann `--full --sanitizers tsan`; `build/asan` ist 87 GB, ohne andere Bäume auf der Platte).
  - **Als Nächstes, in dieser Reihenfolge:** 79.14 Spieltest-3-Befunde (Maus nach Truhe, versunkene Tempel mit Gelände-Anpassung, zu kleine Minenschächte, Augen/Laufanimation von Wolf/Hund/Katze/Kuh, Trockental klären), 79.13 Befehle auffindbar (`/help`, `/locate`-Namen, Minenschacht/Verlies finden), dann Etappe 45 (45.1a HDR …), 50.2, 74.5 (MSVC-Tests in der VM, mit dem Menschen).
  - **Offen, nur notiert (Mensch: Nebenfunde erst notieren, nicht ungefragt beheben):** `test_rivers::villageRoadsCrossRiversOnBridges` mit Seed 1086708103; `test_rivers::shareAndOutlets` mit Zufalls-Seed 1299031228 (geschlossene Flussspalten 0,129 > 0,08, auf `dev` vor 79.14b nachgestellt, 2026-10-26); `test_lava_lakes::lakesAreRareAndApart` mit Seeds 429872594 (68 Seen) und 1037809934 (83 Seen); `test_river_deltas::armsRunIntoTheSea` mit Seed 1941738149 (bei -336 3856) (alle 2026-10-26 aus T2, mit `QC_TEST_SEED` nachstellbar); Angehängtes am zweiten geschobenen Block bleibt hängen (79.12); Bett in der Hand zeigt eine Hälfte (79.10); Admin-Spielerliste nutzt noch eigenen Ping (79.11); zu knappe Zeitgrenzen (`test_river_deltas`, `test_pet_rules`, `test_server_validation`, `test_world_pause`, `test_stonecutter`, `test_anvil_variants`) beim vollen Verify prüfen.
  - **Windows:** letzte Zip `deploy/QtCraft-0.1.0-fc36c30-Windows-x64.zip` (Stand vor 45.0b/79.10–79.12, TLS mit OpenSSL 3.5.9). Das x64-Qt-Kit der VM hat jetzt `plugins/tls/qopensslbackend.dll` (für 48.2 nachgebaut, Rezept in `docs/Building.md`). Neue Zip mit `scripts/vm-package-windows.sh dev` (VM muss laufen; nur auf Wunsch des Menschen).
  - **Arbeitsregeln dieser Sitzung (auch in der Memory):** ab 90 % Wochenbudget nur ein Agent gleichzeitig (neue Pakete erlaubt), ab 99 % WIP-Commit und Übergabe; höchstens zwei Debug-Bäume oder ein TSan-Lauf; Fix-Agenten bekommen einen **eigenen isolierten Worktree** (`git switch <feature-branch>` darin; ein veralteter Hook sperrt Schreiben in fremde Worktrees, nicht per Shell umgehen); Spielgefühl-Pakete (Haltung, Schwung) nur nach Freigabe des Menschen auf Screenshots; TSan-Tests immer mit `ctest --preset tsan` (Unterdrückungsliste), nie nacktes `ctest`.
  - **Dashboards:** QtCraft Fortschritt und QtCraft Performance-Bilanz (Artefakte) stehen auf dem Stand vom 24.10. 20:30; nach dem Reset nachziehen.
- **In `dev`:** alle Etappen der Reihenfolge bis einschließlich 40, 47, 51, 56, 57, 59, 60, 61 und **62** erledigt (62 voller Verify grün 2026-10-21 auf `77670b8`: 62.1, 62.2a/b, 62.3 Stolperdraht/Fallen-Kiste/Steinschneider, 62.3b Steinschneider mit Feldern, 62.3c, 62.4 Rahmen und Gemälde; Protokoll 57); 47.1b (`e09df00`, E-756); Werkzeug T1 Slot-Broker (`c1d2d4a`, E-781: `scripts/qc-slot.py run`). Dazu 62.5 Sichtfehler (`c0e7854`), Etappe 71 Karten im Code fertig (71.1 `3bbc7dd` Protokoll 58, 71.2 `824139f` Protokoll 59, Karten in Rahmen und Wandkarten), Broker T1b mit Lastgrenze (`31d1dc8`).
- **Stand (2026-10-25 08:55):** in `dev`: 79.12 Signal-Parität (`e8d2f50`, E-887: Kolbensockel hält Hebel/Knopf/Fackel/Staub außer vorn – Entscheidung des Menschen, bewusst anders; Fackel/Staub wecken Kolben/Spender/Werfer zwei Blöcke weit; Staub-Treppe über Nicht-Leiter nur hinauf; Komparator-Seite liest Signalstaubblock als 15; Station `signal_parity`; @signal-stress +3,8 % ohne Spielregel-Abkürzung). **Offen, nur notiert (Mensch):** Angehängtes am zweiten und weiteren geschobenen Block bleibt hängen (Test `attachmentsOnTheSecondPushedBlockCurrentlyStay`). 79.11 Latenzanzeige (`feb084f`, E-886, **Protokoll 80**). **Budget 95 %:** keine neuen Pakete bis zum Reset (Montag 02:00); nächster Schritt danach: voller Verify über 45.0b, 48.2, 79.10–79.12, Main-Debug-Build neu. Davor seit 00:50 in `dev`: 48.2 OpenSSL für Windows (`70963c2`, E-872; Windows-Zip mit TLS `deploy/QtCraft-0.1.0-fc36c30-Windows-x64.zip`, VM-Qt-Kit um `qopensslbackend.dll` ergänzt), 45.0b Render-Anker (`3d0846d`, E-883; Fenster-A/B Welt-Render −4…−6 %, Upload wie vor 45.0), 79.10 Spieltest-2-Befunde (`05cdbd8`, E-885, **Protokoll 79**; offen: Bett in der Hand zeigt eine Hälfte). **Notiert, nicht behoben (Mensch: Nebenfunde erst notieren):** `test_rivers::villageRoadsCrossRiversOnBridges` scheitert mit Zufalls-Seed 1086708103 („bridge at -2555 928 ends in water“). **Als Nächstes (einzeln, Budget ≥ 90 %):** 79.12 Umsetzung nach Freigabe. Vorher: **Voller Verify grün** auf `d62e4c8` (dev nach 41.3 und dem Überlauf-Fix): Debug 373/373, ASan/UBSan 361/361 (nach dem Fix `d62e4c8` für `(x0 + x1) / 2` nahe 2^30, E-883), TSan 361/361, Zeit-Tests, Release, statische Analyse, Smoke; `test_push_client` scheiterte einmal unter TSan und Last (VM-Build) in der Zeit-Phase, allein 3/3 grün (Last-Effekt). Zeitgrenzen von 7 Tests angehoben. Gelaufen in zwei Teilen (ASan, dann TSan), weil `build/asan` 87 GB groß ist. Davor heute in `dev`: 48.1 TLS mit OpenSSL 3.5.9 (`861b7e3`, E-872, Protokoll 75), 79.5 Klangbild (`a123672`, E-879, Protokoll 76), 79.6 Fleckenfell/Schmied (`c0948c9`, E-880), 79.4 bewegte Blocktexturen (`a03591b`, E-878), 74.8 Hinweis beim Tausch der Inventarsätze (`4a3a187`, E-283 ergänzt), 73.5 weicher Schub Spieler–Wesen mit Vorhersage (`31cf187`, E-882), 79.7 starrer Handschwung und Werkzeughaltung nach Vorbild, vom Menschen auf Screenshots freigegeben (`272967e`, E-881), 45.0 kamera-relatives Rendering, Weltrand bei etwa 2^30 Blöcken statt 3e7 (`9c4b791`, E-883, **Protokoll 77**; Fenster-A/B: Bildzeit gleich, Welt-Render-CPU +0,03–0,14 ms, angenommen), 79.9 Bauwerke/Dörfer/Straßen/Stege halten jede Delta-Spalte frei (`8128a08`, E-850 ergänzt). 41.3 Zaubertisch- und Amboss-Oberfläche mit Runen, Pergament, schwebendem Buch (`0946c24`, E-884, **Protokoll 78**). **Läuft:** 48.2 OpenSSL für Windows (VM-Build, Zip mit TLS geprüft: `deploy/QtCraft-0.1.0-e092cb7-Windows-x64.zip`; Review-Fixes laufen; Linux nur CMake, kein Linux-Build und keine Installationen). **Wartet:** 45.0b Versätze am Render-Anker (Code auf `feature/45-0b-draw-offsets`, noch nicht gebaut). **Budget:** ab 90 % Wochenlimit nur ein Agent gleichzeitig (Mensch 2026-10-25). **Danach:** 45.0b bauen, Review, Fenster-A/B; 50.2, 74.5 (MSVC-Tests in der VM, mit dem Menschen); Nacharbeiten Lore-Rollgeräusch, weiches Panning, große Bilder asynchron, Truhendeckel-Animation, Swing-Länge fremder Spieler mit Eile, dritte Person Schwungkurve; dann Etappe 45 (45.1a HDR …). **Playtest:** VM-Zip nur für eigene Tests, nächster VM-Build vom neuesten Stand gemeinsam mit dem Menschen; Playtest-Builds aus der GitHub-CI später. **Dashboards:** QtCraft Fortschritt und QtCraft Performance-Bilanz (Artefakte, 2026-10-24). **Geschuldet:** Screenshots `signal_decoupled`, `release.sh`-Lauf mit OpenSSL in Frameworks, Main-Checkout-Debug-Build/clangd. **Platte:** höchstens zwei Debug-Bäume oder ein TSan-Lauf gleichzeitig; Worktrees direkt nach dem Merge entfernen; Fix-Agenten bekommen einen eigenen isolierten Worktree (ein veralteter Hook sperrt Schreiben in fremde Worktrees).
- **Danach:** 71 (Karten), 58, 63–66, 73 (Monster-Spawn-Parität, eingeplant 2026-10-20), 68, 69, 70; Reihenfolge in der Tabelle „Etappen“.

## 2026-10-23/24 (aus FORTSCHRITT verschoben am 2026-10-25)

- **Stand (2026-10-23 18:10, Firmen-Account, Team-Plan):** neu in `dev`: 79.1 Zaunverbindungen in Strukturen (`c51f28d`, E-867; Review ohne Blocker, Hinweise eingearbeitet; Golden-Hashes der Dorfpläne neu, nur Verbindungs-Eigenschaften ändern sich). **Läuft:** 79.2 Eis-Gitter auf `feature/79-2-ice-grid` (E-868: Ober-/Unterseite von Eis und Glas zu Wasser entfällt; Review ohne Blocker, Fixes laufen: fließendes Wasser, Ufer, Mutationstests, `test_lava_lakes`-Seed 510556236); 41.2b BUD-Varianten am Amboss auf `feature/41-2b-bud-variants` (E-870). **Regeln des Menschen (2026-10-23):** höchstens 3 Agenten gleichzeitig; GUI-Benchmarks derzeit erlaubt (Sperre, nie parallel zu Builds, `--screen builtin`). **Geschuldet:** voller Verify über 41.1/50.1/41.2a/79.1 (79.1 ohne vollen Verify gemergt, Plattenplatz um 35 GiB), zentraler TSan für 79.2 (Meshing); Benchmark für 79.1 entfällt (nur Weltgenerierung). Danach: 41.2c, 41.3, 73.4, 48 TLS, 50.2, 74.5. Offene Entscheidung des Menschen: 74.6. Hinweis: Agenten ohne `isolation: worktree` scheiterten heute an einem Hook (verwaister Worktree `epic-solomon-109268`); Paket-Agenten mit eigener Worktree-Isolation starten.
- **Stand (2026-10-23 16:10) – Übergabe an den anderen Account (Wochenbudget dieses Accounts 98 %):** heute in `dev`: 41.1 Zaubertisch (`60d947c`, E-864, Protokoll 70), 50.1 benannte Casts nach ES.49 (`2dcce76`, E-865; Linux-GCC-Build erst im ersten CI-Lauf geprüft), 41.2a Amboss und Namensschilder (`11d80d9`, E-866, Protokoll 71), Tailscale-Abschnitt in `docs/Playtest.md`. **Pausiert:** 79.1 Zaunverbindungen in Strukturen auf `feature/79-1-structure-connections` (Worktree `.claude/worktrees/qtcraft-79-1-connections`, `5554b0d`, E-867; Handoff `b3daff6` in `docs/handoff-79-1.md` auf dem Branch; Fix wirkt (Screenshots), Quick-Verify 342/343: offen sind die Golden-Hashes in `test_village_plan_hash` (Seeds 42, 7, 20261017; Dorf-Zäune verbinden sich jetzt, neue Werte im Verify-Log) und drei Befunde der statischen Analyse; danach Quick-Verify, Review → Merge). **Reihenfolge danach:** 79.2 Eis-Gitter (erst auf dem Mac nachstellen), 41.2b, 41.2c, 41.3 (Zaubertisch- und Amboss-Oberfläche), 73.4 Hexenhütte, 48 TLS, 50.2, 74.5 Tests unter MSVC (VM-Build-Ordner `C:\Qt-Projekte\Claude\QtCraft` liegt bereit), dann 76.1/76.2, 55, 45 … (Tabelle „Etappen“). **Offene Entscheidungen des Menschen:** 74.6 Internet-Spiel ohne VPN/Router. **Voller Verify** über 41.1/50.1/41.2a/79.1 steht noch aus (vor dem Abschluss der Etappen 41 und 79). In der VM laufen eventuell noch `qtcraft-headless --server` (Port 25600) und `qtcraft.exe` vom x64-Test.
- **Stand (2026-10-23 05:00):** seit 22:20 in `dev`: 77.2 „Server verwalten“ (`09f8f78`, E-863), Weizen-/Ack-Fix (`1728784`, BlockAck nach dem Entity-Sync), 70 Flussdelta (`9e701e4`, E-850/E-851: verzweigte Arme, selten ~1 je 10 breite Mündungen, Flussstücke, Flussbreite, Randschienen), Performance-Fix E-862 (`5ae41ee`). **Voller Verify auf `9e701e4`:** Debug 339/339, TSan 331/331, Release, statische Analyse (1100 Dateien), Smoke grün; ASan 330/331 – einzig `test_river_deltas::islandsJustAboveTheSea` mit Zufalls-Seed 2115900183 (Fix-Agent auf `feature/delta-seed`). Nach dessen Merge gelten 70, 73, 74.2, 76.0b/c, 77, 78, 79 als abgeschlossen. Offen danach: Kosten der Flussbreiten-Änderung (Release-`test_terrain_measure`), 74.4 Windows-x64 (x64-Kit vom Menschen), T1d Broker, 70.2 am Ende, 76.1/76.2 (helleres Theme als Kandidat). Wochenbudget 94 % (Regel: ab 95 % ein Agent, ab 99 % Übergabe).
- **Stand (2026-10-22 22:20):** heute in `dev`: 76.0b/76.0c (UI-Leitfaden, Hauptmenü C, Theme Qt Neon), 73.3 Kjut, 73.2 Biom-Spawnlisten (Protokoll 68), 77.1 Admin-Kanal (67), 78 Block-Vorhersage (69), 79 Weltgen-Dichte, 74.2 Windows-Build (ARM64-VM), Eis/Schleim, 69-Nachträge, clazy-Hänger, Fenster-A/B nach 73–79 (Speicher −12…−16 %, Sichtweite 16 Sprint 754 MB; GUI-CPU-Regression durch E-862 behoben, Tick +20–30 % durch mehr Wesen gewollt, E-836). In Arbeit: 70 Delta (Umbau organischer, Rückmeldung des Menschen), 77.2 Bildschirm „Server verwalten“. Geschuldet: voller Verify der Etappen 73, 76, 77, 78, 79 (je nach Abschluss), 74.4 Windows-x64-Paket (x64-Kit vom Menschen), T1d Broker.
- **Stand (2026-10-22 04:30):** 58, 63, 64, 65, 66, 68 (68.1/68.2), 47.3, 74.1 und 74.3 erledigt (voller Verify grün 2026-10-22 auf `03b538e` (ASan-Fix `453ce82`), Fenster-Benchmark ohne Regression (`b3f5bad`)). **Läuft:** 73.1 Monster-Spawn-Regeln (`feature/73-1-spawn`, E-836, Review-Fixes eingearbeitet, `dev` gemergt, Quick-Verify grün; wartet auf zentralen TSan/vollen Verify und Merge), 69 Flüsse (`feature/69-rivers`, E-840 ff.). **Wartet auf den Menschen:** 76.0 UI-Leitfaden (Entwürfe als Artefakt, 9 Entscheidungsfragen), x64-Kit in der VM für 74.2 (erster ARM64-Build fertig auf `feature/74-2-windows`, E-861). Offen: Block-Indizes 1 Byte/4 Bit (Speicher Sichtweite 16 noch 859 MB statt ≤ 800); Szenario mit vielen bewaffneten Spielern (47.3 Third Person ungemessen); `cull`; QRhi-Batchkopien auf Vulkan/D3D/GL; Blumental als echte Mulde; lupdate-Pfade in Worktrees.

## 2026-10-21 07:25 (Pause)

Nach Etappe 62: 62.5 (Rauch-Textur ohne Namensraum, Rahmen-Station von der falschen Seite fotografiert; über die Offscreen-Tour gefunden), 71.1 Karten und 71.2 Karten in Rahmen in `dev` (Reviews fanden u. a. verwaiste Karten beim Zoom, verhungernde Zeilen, leere Showcase-Wandkarte, Randmarken mit Richtungsverrat – alle behoben). Broker T1b: Lastgrenze 1,25 × Kerne mit Schätzung der kurzfristigen Last, Ausnahme bei fremder Last nur einzeln. Etappe 71: Benchmarks ohne Regression, voller Verify für die Pause abgebrochen. Etappe 58 begonnen (Client-Speicher bei Sichtweite 16 −18 %). Pause auf Wunsch des Menschen.

## 2026-10-21 04:00

Etappe 62 abgeschlossen: 62.3 (`4fe6b13`), 62.4 Rahmen und Gemälde (`b8e3410`, Haltprüfung nach Ereignissen statt jeden Tick), 62.3b Steinschneider mit Feldern wie im Vorbild (Mensch 2026-10-20; `2fe17e7`, clazy hing minutenlang an einem Index auf ein Temporäres in `QVERIFY`), 62.3c ein Fenster zur Zeit (`77670b8`). Headless: Tick gleich, Speicher +8–15 %; Fenster: GPU −41…−56 % (47.1b), Render-CPU +3…+24 % durch mehr Zeichenaufrufe; voller Verify grün. Plan: Etappe 73 Monster-Spawn-Parität (kein Creeper, kein Phantom, eigenes Monster „Kjut“). 71.1 Karten im Review-Fix.

## 2026-10-20 22:00

62.2b in `dev` (`475f4eb`, Protokoll 53; Zeittests einmal nur unter Last gescheitert, allein grün), 62.2a nach Review-Fixes in `dev` (`33b3148`, Protokoll 54; Text-Cache je Schild, Notenbudget). Fenster-Benchmark 47.1 zeigte +28–42 % GPU durch die Filterung; Ursache (Fable-Agent auf Wunsch des Menschen): `discard` im Shader aller Blöcke schaltete das Verwerfen verdeckter Fragmente ab. 47.1b trennt Vollblöcke und ausgeschnittene Flächen (E-756), Review fand einen Culling-Fehler (Sections nur mit Pflanzen), behoben und neu gemessen: GPU stand −20 %, sprint −14 %, light −7 %, glass +1 % gegenüber vor 47.1; in `dev` (`e09df00`). Werkzeug T1 Slot-Broker (`c1d2d4a`, E-781) nach drei Review-Runden: Builds, Verify und Benchmarks laufen über `scripts/qc-slot.py run`, Benchmarks exklusiv bei Last < 4; eine Panne beim Wechsel (alter Daemon verstand neue Clients nicht) machte eine Messreihe ungültig. Plan: Etappe 73 Monster-Spawn-Parität nach 66 (kein Creeper, kein Phantom; eigenes Eltern-Kind-Monster „Kjut“ mit Funkengel und Schwarzpulver). 62.3 fertig und im Review-Fix (Steinschneider mit Feldern wie im Vorbild), 62.4 begonnen.

## 2026-10-20 15:15 (Pause)

Pause auf Wunsch des Menschen. 62.2a und 62.2b mitten in der Arbeit angehalten, Stand je als WIP-Commit gesichert (ungeprüft). Aufträge: 62.2a Schild (Texteingabe, Text im Bild, Server-Prüfung), Notenblock (Bretter + Signalstaubstück, Instrument nach Block darunter), Zielblock (Heu + 4 Stücke, Stärke nach Trefferabstand), Signal-Kontrollleuchte (Glasscheibe + Stück, färbbar), Kompass und Uhr (Eisen bzw. Gold + 4 Stücke); 62.2b Räucherofen und Schmelzofen (Schmelz-Kategorien), Lagerfeuer (4 Kochplätze, Schaden, löschen und anzünden), Kuchen (7 Bissen), Seerose (Sumpf und Feuchtgebiet).

## 2026-10-20 14:45

Etappe 61 in `dev` (`2a89671`; nach Rückfragen des Menschen: Bauteil direkt auf der kleinen Signalfackel wird versorgt, ein Block darüber nicht stark; Rezept aus Signalstaubstücken). Größengrenzen in Code-Zeilen (`275bb8c`, E-760; 28 Stellen nach Zuständigkeit geteilt, eigenes Prüfskript für Funktionen). 62.1 Bauvarianten in `dev` (`3709ff2`; Review: Protokoll 52, Laub an entrindeten Stämmen, bemooste Blöcke aus Ranken). 62.2a und 62.2b gestartet. Plan ergänzt: 62.2 mit Signal-Kontrollleuchte (färbbar), Stück-Rezepten, Kompass und Uhr; 71 Karten nach 62.4; 72 API-Dokumentation vor dem Release; voller Verify nur für komplette Etappen.

## 2026-10-20 11:45

Etappe 59 in `dev` (`93d99a0`; voller Verify grün bis auf `test_scenarios` unter ASan, allein fünfmal grün: Last-Effekt beim Aufsammeln). Etappe 60 in `dev` (`07c4835`; Review: Ofentext, Dorfplan-Prüfsumme jetzt nach Blocknamen statt IDs; voller Verify grün, Headless-Benchmark neutral). 61 in Nachbesserung nach Rückfrage des Menschen, 62.1 begonnen.

## 2026-10-20 09:25

Wiederaufnahme. Der Mensch bemerkte, dass sein Fenster während eines Verify im Sekundentakt den Fokus verlor: die Offscreen-Tests des Spiels wurden beim Start kurz zur Vordergrund-App; behoben (`e264b65`, `QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM` für Skript-Läufe und alle Tests), bestätigt. 47.1 Aussehen freigegeben und gemergt (`5d7f77c`, Aufnahmen als JPEG); Etappe 47 erledigt bis auf den Fenster-Benchmark. 59a und 59b zusammengeführt (`6067b71`), Benchmark und voller Verify laufen. Etappe 60 begonnen. Plan ergänzt: 48 TLS vor dem Release, 62 Blockliste freigegeben (62.1–62.4, mit Stamm-Achse, Treppen-Ecken, vertikaler Stufe, kleiner Kürbislaterne), 69 Flüsse, 70 Flussdelta.

## 2026-10-20 06:20 (Pause)

Pause auf Wunsch des Menschen. Seit 05:30: Etappe 59 begonnen. 59a Zuschauer fertig mit Review-Fixes (Schwung beim Verlassen, Spawner, Blitze, Angel, Lore; Operator-Befehle bleiben erlaubt). 59b Abenteuer fertig, Review gefunden: die erlaubte Höchstgröße der Listen überschreitet die Rahmengrenze des Protokolls; Fixes begonnen und als WIP-Commit gesichert (ungeprüft). Offene Schritte stehen in `docs/FORTSCHRITT.md` („Pausiert“).

## 2026-10-20 05:30

Etappe 47: 47.2 UI-Skalierung (75–200 %, Auto nach logischer Fenstergröße, Faktor auf 1024 × 640 bzw. 640 × 360 begrenzt) und Zugänglichkeitsrollen in `dev` (`def8fde`); im Review gefunden: die Fensterschrift kehrte nach 200 % nicht auf 100 % zurück. 47.1 (Beschriftungen, anisotrope Filterung im Shader, Arm, Grundhelligkeit nach Himmelslicht, Laub) fertig, Review behoben (Blockkanten, Zweierpotenz-Pakete, Grenzen der Beschriftungen), `dev` eingemergt, Beschriftungen folgen der HUD-Skalierung; wartet auf die Freigabe des Aussehens. Etappe 58 bleibt ausgesetzt; 59 beginnt.

## 2026-10-20 04:20

Etappe 57 erledigt: 57c Fassaden geteilt (`be41568`; Registry mit vier Katalogen, `QcChunkSaveState` und `QcBlockEntityExtras` aus der Server-Welt, `QcWorldMirror` aus der Client-Session; zwei Reviews ohne Befund zum Verhalten). Fenster-Benchmark `@core` gegen `dev` vor 57a ohne Regression, voller Verify grün bis auf den neuen Test `app_offscreen_view_hidden_keeps_sections`, der unter TSan von einer festen Wartezeit abhing (zum Ausblenden noch kein Abschnitt gemesht); der Testhaken wartet jetzt auf gemeshte Abschnitte, der Test lief danach unter TSan zweimal grün. Weiter mit Etappe 47.

## 2026-10-20 02:05

Fenster-Benchmark der Etappen 51 und 56 nachgeholt (`5567623` gegen `650a1cf`, `@core`, `@ui`, `explosive-chain`): keine Regression, Spitzenspeicher 3–16 % niedriger. Etappe 57: 57a Tick-Systeme als Liste (`7b173e6`), 57d Renderer-Ressourcen nach Lebensdauer und `FrameInput` (`742e271`; dabei gefunden: nach einem QRhi-Wechsel wurden die Chunk-Meshes nie neu geschickt, jetzt `meshesLost`/`remeshAll`), 57b Schicht `qc_worlds` (`03d6a63`; `src/app` ohne Server-Header und Server-Typen, gemeinsame Datentypen nach `common`/`network`) nach Review, Nachbesserung und zentralem TSan-Lauf in `dev`. 57c (Fassaden teilen) läuft.

## 2026-10-19 23:50

Etappe 56 erledigt: 56e keine Ausgabeparameter nach `dev` (`650a1cf`, E-694) nach drei Review-Runden mit je zwei Prüfern; Headless-Benchmark gegen `dev` ohne Unterschied über dem Rauschen (crowd mit 6 Runden −0,1 % Mittel, +2,6 % p99), voller Verify grün (3337 s). Entscheidungen des Menschen: Ausnahme „Subjekte“ (Objekte mit Identität, auf denen ein System arbeitet) bleibt; Rucksack als Etappe 68 nach 62 (eine Reihe, platziert mit ganzem Inventar, aus der Hand nur Rucksack-Reihe und Hotbar); ein bis zwei geschuldete Fenster-Benchmarks dürfen laufen, ohne die übrige Arbeit zu blockieren.

## 2026-10-19 18:10

Wiederaufnahme nach der Pause. 56b Test-Fixtures nach `dev` (`457f1c6`): der TSan-Lauf scheiterte nur an `test_latency` an der Zeitgrenze (Last durch andere Programme), allein zweimal grün, also Last-Effekt. 56d `LazyScreen`, Enums und Fotomodus nach `dev` (`e8a8d1a`), TSan-Lauf grün. 56e gestartet als zwei parallele Pakete (`src/common` und der Rest von `src/`).

## 2026-10-19 16:50 (Pause)

Pause auf Wunsch des Menschen. In `dev` seit 14:20: Feuchtgebiete (`fca90b7`, E-660/E-661), 56a Logging zentral (`a953b13`, E-680), 56c Handbuch-Übersichten, Schichtregel, `docs/Log.md`, drei Code-Regeln (`97925c5`, E-685–E-688).

Offen zur Wiederaufnahme: (1) 56b Test-Fixtures (`feature/56b-test-fixtures`, fertig, Quick-Verify grün; `dev` eingemergt) – noch `scripts/verify.sh --tsan-concurrent --changed` (wurde für die Pause abgebrochen), dann mergen. (2) 56d `Main.qml`-Umbau, `LazyScreen`, Fotomodus, Enums (`feature/56d-main-qml`, Review und Fixes fertig) – Pad-Kombinationen Back+Y (Screenshot) und Back+X (HUD aus) nach Entscheidung des Menschen fertig (`1a4d887`, Quick-Verify mit Timing-Phase grün); offen nur der zentrale TSan-Lauf, dann Merge. (3) Danach 56e: bestehende Ausgabeparameter umbauen (27 skalare, 73 `void`-Funktionen mit Container-Parameter, `scripts/check-out-params.py`). Dann Etappe 57.

Solange der Mensch am Rechner arbeitet: keine Fenster-Läufe, zentrale Verifies mit `QC_QUIET_WAIT=120 QC_BUILD_JOBS=8 nice -n 10`.

## 2026-10-19 14:20

Stand 2026-10-19 14:20: Etappe 40 erledigt (40.3 `94020eb`), Etappe 51 erledigt (51.1 `274c86f` Wüstentempel/Sandgolem, 51.4 `d2fa1f7` Ruinen/Brunnen/Oasen, 51.3 `2fd6ce7` Dschungel/Dschungeltempel/Hexe, 51.2 `b054332` Sprengstoff/Küstentürme; alle mit vollem Verify und Headless-Benchmark, Fenster-Benchmarks geschuldet).

Dazu in `dev`: IDE-Quellgruppen (E-640), Icon-Cache mit Generation (E-650), QML-Ordner (E-670), Render-Pass-Gruppe im Weltrenderer, Testauswahl nach Inhalt, Pre-push-Hook, `QC_BUILD_JOBS`, Regel gegen Ausgabeparameter, Hotfix des Phasen-Status, Race-Fix `test_sand_golems`, Gehege-Fix.

Offen: Feuchtgebiete (`feature/wetland-blend`, E-660/E-661, wartet auf Freigabe des Aussehens durch den Menschen).

Weiter mit Etappe 56.

Solange der Mensch am Rechner arbeitet: keine Fenster-Läufe, zentrale Verifies mit `QC_QUIET_WAIT=120 QC_BUILD_JOBS=8 nice -n 10`.

## 2026-10-19 (Paket 51.3)

**Paket 51.3 (Branch `feature/51-3-jungle-witch`, von 51.1 abgezweigt, Stand 2026-10-19):** Dschungel-Biom mit Ranken (E-620), Dschungeltempel mit Hebel-Rätsel (geheime Kammer hinter klebrigen Kolben) und Pfeilfallen, Spender-Aktion `shoot` (E-621), Hexenhütte auf Stelzen auch über Wasser (`waterDepth`, `preferWater`, `clearFrom`; Kessel, Blumentopf; E-622), Hexe als Bogenschützin mit Wurftränken nach Ziel-Effekt und Heiltrank (E-623), Treffer-Wirkung `splash` (E-624); Showcase-Stationen 99/100, Handbuch EN/DE, Tests `test_jungle_temples`, `test_temple_traps`, `test_witches`. Kein neues Protokoll/Weltformat; die Weltgenerierung ändert sich für alle Seeds (neues Biom). Merge-Reihenfolge 51.1 → 51.4 → 51.3 → 51.2; zentral offen: TSan, Zeit-Tests, voller Verify, headless Benchmark (Generierung, Tick mit Hexen).

## 2026-10-19 07:00

Stand 2026-10-19 07:00 (Pause auf Wunsch des Menschen): in `dev` 46.2 (`36ac0c3`, Etappe 46 erledigt), 38.2 (Etappe 38 erledigt), 40.2 (`ab864c3`), Gehege-Fix der Showcase (`8c37861`: Kühe stiegen über Heuballen auf den Zaun; Regel in E-500) und CLAUDE.md-Regel `-j 4` für Agenten-Builds.

Offen zur Wiederaufnahme: (1) 40.3 (`feature/40-3-gamepad-settings`, E-610–E-615, Review und Nachbesserung fertig, Fenster-Benchmark ohne Regression) – voller Verify lief bei der Pause, Ergebnis prüfen, dann mergen; damit Etappe 40 erledigt. (2) 51.1 (`feature/51-1-desert-temple`, E-590–E-594, Review und Nachbesserung fertig; Entscheidungen des Menschen: Sand darf das Wasserloch der Farm zuschütten – Bauaufgabe des Spielers; Keller darf Minenschächte und Verliese anschneiden) – offen: zentraler Benchmark (`@server`, Wüstenwelt mit Tempel), voller Verify, Merge. (3) 51.2 (`feature/51-2-explosives`, E-600–E-605, Protokoll 50, Review und Nachbesserung fertig, Release-Kette 2,07 ms Ø / 4,94 ms max) – offen: nach 51.1 `dev` mergen und Küstentürme und Oberflächen-Bauwerke gegenseitig meiden lassen (Test), voller Verify, Merge.

Danach 51.3, 51.4.

Vorschlag-Chip für den lupdate-Pfadfehler in Worktrees liegt beim Menschen.

## 2026-10-19 00:45

Stand 2026-10-19 00:45: 46.1e/40.1b in `dev` (`f16395f`, voller Verify grün bis auf `test_dungeons` unter TSan an der Zeitgrenze; dabei Fehler in `verify-phases.sh` gefunden: die Timing-Phase überschrieb den Status von Phase 1, Hotfix `f96255f` mit Grenzen für `test_dungeons`/`test_spawners`).

38.2 in `dev` (`d0663f0`, voller Verify grün, headless ohne Regression); Etappe 38 erledigt.

46.2: Fenster-Benchmark ohne Regression (`client_ticks_missed` 0, p99 nachgemessen), voller Verify läuft, dann Merge.

40.2 (`feature/40-2-gamepad-ui`, E-580–E-584: Pad-Belegung, Radialmenü, Fokus-Navigation, Bildschirmtastatur, Hotbar-Flackern behoben): Review fertig, Nachbesserung läuft (Schieberegler, gehaltene Richtung beim Öffnen, Passwort im Klartext, Sonderzeichen, Fokus-Bereich je Frame, High-DPI, Hotbar nach verworfener Auswahl); offen für 40.3: Hotbar nur als Radialmenü, Radialmenü mit der Maus, Tastensymbole je Controller, eigene Belegung.

40.2 außerdem: Testhaken `--overlay "@<ms> pad:<schritt>"`; Handbuch und Übersetzungen; Nachbesserung fertig, Fenster-Benchmark ohne Regression (Performance-Verlauf), `dev` mit 46.2 eingemergt.

40.3 (`feature/40-3-gamepad-settings`, Worktree `.claude/worktrees/qtcraft-40-3`, vom Kandidaten 40.2, E-610–E-615): Reiter „Controller“ in den Einstellungen (Umsehgeschwindigkeit, Umkehren, Totzonen, Kennlinie, Triggerpunkt, Vibration, Nur Radialmenü), Profile je Controller-Modell (SDL-GUID), Tasten ändern mit Erfassen am Pad und Tausch bei Konflikten, Tastensymbole je Familie (Xbox, PlayStation, Nintendo, generisch; gezeichnet), Meldungen beim Anschließen/Trennen/Übernehmen, Pause beim Trennen des führenden Pads, Radialmenü mit der Maus (Taste G), Vibration bei Schaden; Handbuch, Übersetzungen, Bildschirmfotos `gamepad-settings.png`, `gamepad-playstation.jpg`.

40.2 und `dev` (40.2 als `ab864c3`, mit 46.2) eingemergt; Review-Nachbesserung fertig (Pause beim Trennen und Vibration nur, wenn der Pad das benutzte Gerät ist; keine Vibration ohne Fenster-Fokus und vor der ersten Gesundheit vom Server; Seite öffnet auf dem Profil des benutzten Pads; Profil-Grenze nach letzter Änderung, angeschlossene Modelle bleiben; Erfassen bricht bei Taste/Klick überall und beim Trennen des letzten Pads ab; Pad-Hinweise nicht mehr von den Einstellungen verdeckt; Steuerkreuz-Symbol deutlicher).

Offen: zentraler TSan und voller Verify.

## 2026-10-18 22:30

Stand 2026-10-18 22:30: Timing-Paket mit Ruhe-Sperre (E-550, `ee01c36`: Schwelle = Kerne, `~/.cache/qtcraft/quiet.lock` während der Timing-Phase, Agenten rufen `scripts/wait-quiet.sh` vor eigenen Builds) und 38.1 (`1d9fa77`, voller Verify grün bis auf `test_tick_scheduler` unter Last 36 vor der Ruhe-Sperre, allein 3 × grün) in `dev`.

46.1e/40.1b: `dev` gemergt, Benchmark ohne Regression, voller Verify läuft, danach Merge.

46.2 (`feature/46-2-session-thread`, E-560): Session-Kern auf eigenem Thread, Messung vorher (Tempo bei 250-ms-Frames 0,25 → 1,00), Review und Nachbesserung fertig (Flugwunsch, Headless exakt ohne Thread, TCP-Nachlauf, neue Thread-Tests); offen: zentraler TSan, Fenster-Benchmark mit `client_tick_late_*`/`client_ticks_missed`, voller Verify.

38.2 (`feature/38-2-mineshafts`, E-570–E-572): Minenschächte mit Ebenen, Treppen, Brücken, Zugängen, Kisten-Loren, Showcase-Station; Review fertig, Nachbesserung läuft (Bäume am Ausgang, Loren-Dupe bei `/regenerate`, Förderturm am Hang, Test Abdichtung); danach Benchmark `@server explore` und voller Verify.

Offen für 40.2 zusätzlich: Hotbar-Flackern (ältere Server-Antwort überschreibt neueren lokalen Slot).

## 2026-10-18 abends

2026-10-18 abends: 46.1 in `dev` (`93d42fd`, voller Verify grün 2115 s, Benchmark ohne Regression, crowd nachgemessen +2 %).

Timing-Tests getrennt (E-550, `feature/timing-tests`: `tests/timing.cmake`, zweite Phase in `verify.sh` bei ruhiger Maschine, `--defer-timing` für parallele Agenten), zusammen mit 38.1 im Kandidaten `feature/38-1-spawners` (Benchmark ohne Regression, explore +7 %; Konflikte mit 46.1 in Arbeit, danach voller Verify, getrennt mergen).

46.1e/40.1b (`feature/46-1e-40-controls`): Review-Fixes fertig (Hintergrund-Drücke verworfen, Schleichen bleibt, Pad-Führung, Sammel-Structs aufgelöst, Szenario-Test 4 s), `dev` gemergt, Fenster-Benchmark läuft, dann zentraler TSan und voller Verify.

Offen für 40.2: Start (Pause), Y (Inventar), Fallenlassen und Block wählen auf dem Pad.

## 2026-10-17 bis 2026-10-18

**Laufender Stand (Stand 2026-10-17):** Etappe 37 ist komplett in `dev` (37.1 bis 37.3, E-400 bis E-415; voller Verify von 37.3 grün, 1330 s). Strenge Design-Prüfung 37.R fertig: muss (Wesen-Datensätze als versionierte Komponenten, dauerhafte Wesen-IDs, Dorf-Daten für Mods ergänzbar, Verhalten nicht am Plan-Zwischenspeicher), sollte (`QcMobManager` aufteilen, Fähigkeiten/KI/Haltung aus Daten, Dörfer wachsen über Spieler-Betten, Leben erst nach Abgleich, Planer nach Zuständigkeit teilen, Showcase-Referenz verkleinern, Testdörfer, Langzeittest); 37.R1 umgesetzt (E-416: Plan-Zwischenspeicher verdrängt selten benutzte, gehaltene Pläne bekannter Dörfer; Leben erst nach Abgleich; `test_block_entity_compare` auf kleiner Station, 172 statt 394 Zeilen, lesbare Abweichungen), geprüft, nachgebessert (Pläne auch für vorher geladene Chunks, Abgleich über den Chunk-Index, Station mit Abklingzeit, Ausbrennen und Signal-Gedächtnis), Benchmark headless ohne Regression; in `dev` (voller Verify grün, 1667 s).

53.1 (CMake aufräumen, E-417) in `dev`: Quelllisten je Ordner sortiert mit `scripts/check-cmake-lists.py`, `qc_app_core` mit dem QML-Modul, Tests je Themenordner (`tests/<thema>/tests.cmake`), Presets für diesen Rechner in `CMakeUserPresets.json` (nicht in git, `scripts/ensure-user-presets.sh` legt sie an), `cxx_std_23` über `target_compile_features`.

53.2a in `dev` (E-418: Includes ab `src/`, neue Unterordner `server/mobs|minecarts|plants|items|chunks|tick`, `src/worldgen/` als Bibliothek `qc_worldgen`, `clientcore/texturepacks|icons`, `scripts/fix-includes.py`).

53.2b in `dev` (E-419: Dorf-Planer geteilt in `QcRoadNetworkPlanner`, `QcPiecePlacer`, Steg-Planer und `QcVillageDraft`, Pläne per Hash als gleich belegt; Schicht-Tabelle in `fix-includes.py --check`; Basis-Inhalt nur wo gebraucht); Etappe 53 erledigt.

37.R2 umgesetzt (E-420: Wesen-Einträge als Kopf plus Komponenten mit ID/Version/Länge, Unbekanntes bleibt erhalten, dauerhafte 64-Bit-Wesen-ID `uid` mit `nextCreatureId` in `world.json`, Ansprüche über ID, Weltformat 13; Block-Entities noch positionell → eigenes Paket 37.R2b vor 39), geprüft und nachgebessert (Item-Warteschlange behält Komponenten, feste Grenzen 64/16/48, Kopf-Felder bleiben erhalten, doppelte IDs neu vergeben; Speichern +6,6 %, ~0,1 µs je Entity), in `dev` (voller Verify grün, 1863 s).

37.R3 (Dorf-Datenformate für Mods, E-421) umgesetzt, geprüft und nachgebessert (benannte Kopie ohne geerbte Biome, Biom-Konflikte mit Warnung und `remove`, Dorf-Einstellungen feldweise zusammengeführt, Golem-Felder je Stil über den Einstellungen, Golem-Grenze je Art), in `dev` (voller Verify grün, 1507 s).

Gerenderter Benchmark der Dorf-Sektion fertig (`c9e8a63`, neue Szenarien `village`, `village-night`, `village-swamp`, `minecarts`): keine wiederholte Regression bei gleichem Bild; Dorf-Szenen kosten GUI-CPU durch mehr Wesen im Bild (gleiche Kosten je Wesen), Server-Tick in Mob-Szenen +20 % durch die Wegfindung aus 37.1; Ziele bis auf p99 (VSync von außen) und Speicher bei 16 Chunks (bekannter Befund) erfüllt.

52.0 auf `feature/52-0-creature-architecture` (Worktree `.claude/worktrees/qtcraft-52-0`): 52.0a fertig (E-422: `QcNaturalSpawner`, `QcVillagePopulation`, eine Meldung beim Verlassen der Welt mit Grund; Verhalten gleich), 52.0b fertig (E-423: Haltung stehen/liegen/sitzen, `QcComponent<T>` für Zustand je Art, `abilities` und `ai` aus Daten, `QcMobBrain` mit Passiv-, Feind- und Dorfbewohner-Gehirn, `QcDoorPassage`, `QcMobLegs`; Verhalten gleich), 52.0c fertig (E-424: `QcVillageFixture` statt Seed-Dörfern, `test_village_soak` mit Invarianten über Tage, `test_village_limits_measure` mit 1024 Dorfbewohnern: kein dauerhaftes Verhungern des Wegbudgets, nur kurze Wartezeiten morgens; vier echte Fehler gefunden und behoben: zwei Wüstenhäuser mit Falle, Brunnen ohne Ausstieg, Tür-Blockade in der Tür).

Prüfung von 52.0 fertig und nachgebessert (Testdörfer in allen vier Drehungen ohne Spielfehler, Abmelde-Meldung erst sammeln dann melden dann entfernen, Abmelden möglich, Langzeittest prüft Geburten; Aktions-Ausgang für Geschosse als Vorgabe für 52.1 in E-423); Benchmark headless ohne Regression; 52.0 in `dev` (voller Verify grün, 1650 s).

52.1a fertig (E-425: Entity-Art Geschoss aus `projectiles/*.json`, Flug ohne Tunneln, Treffer über den Nahkampf-Schaden, Stecken/Aufsammeln, Grenzen 1024/64, Bogen mit Spannen, Pfeil, Feder vorerst von Zombies, Bogenschießstand, Protokoll 42, Weltformat 14; Aktions-Ausgang `launches`), 52.1b fertig (E-426: Skelett-Bogenschütze mit `QcArcherBrain`, Abstand 5–12 m, Spannen 1 s, Vorhalt mit Schwerkraft, Brennen bei Tageslicht für Zombies und Skelette, Knochen und Knochenmehl, Drop-Chancen werden jetzt gelesen; 50 Skelette 0,09 ms je Tick); Prüfung von 52.1 (a+b) fertig (ein Muss: wartender Pfeil an ungeladenem Chunk fällt weiter; dazu Schaden je nach Zielbewegung, Schlag und Schuss im selben Tick, Spannen bei Menü/Tod/Bogenwechsel, unbekannte Geschoss-Verweise, `QcMobBehaviour::Actions`), Nachbesserung fertig (alle 15 Punkte, Abbruch des Spannens als `DrawLauncher`-Aktion, Schaden normiert, Munitionsname im Inhalts-Abgleich), 52.1c fertig (E-427: `ammunition` je Werfer mit Namen und `#tag`, `onHit` mit damage/knockback/ignite/break, `throwable` mit `QcMsg::ThrowItem`, Schneeball, Schnee nur mit Schaufel; Spender schießen noch nicht, später), Prüfung von 52.1c fertig (kein Muss; Treffer ohne Schaden machten Wesen unverwundbar, `damage` neben `onHit` ignoriert, Rückstoß-Stärke hob senkrecht mit, Brennen spät gemeldet, Schneeschicht ohne Halt gab Schneeball), Nachbesserung läuft; danach gerenderter Benchmark von 52.1 (`@core` plus Pfeil-/Skelett-Szene) und headless, voller Verify, Merge; auf `feature/52-1-projectiles` (Worktree `.claude/worktrees/qtcraft-52-1`).

Neue Regel (Mensch 2026-10-17): Parameter-Structs nur als benennbares Konzept (CLAUDE.md); Prüfung bestehender Parameter-Structs fertig (3 klare Fälle, 1 Grenzfall zum Aufräumen), HUD/Trichter fertig auf `feature/hud-hopper-fixes` (Chat endet über den Statuszeilen, Prüfung `QcLayoutCheck` mit Test `app_offscreen_hud_layout`; Trichter ohne Fehler: der Entwurf brauchte 8 Ticks je Trichter und der erste Trichter darunter zieht alles, Test `test_hopper_lines`), Merge nach 52.1.

37.R2b fertig auf `feature/37-r2b-block-entity-components` (E-440: Block-Entities als Tabellen je Art mit Rahmen wie E-420, Einträge mit eigener Version und Länge, Unbekanntes je Chunk gehalten, Weltformat 15; Speichern/Öffnen +6–8 %), Prüfung läuft.

52.P und HUD/Trichter in den Kandidaten 52.1 gemergt; Nachbesserung 52.1c fertig (Schneeschicht ohne Halt gibt weiter einen Schneeball, Mensch 2026-10-17); TSan-Rennen in `test_network_game` zentral nicht nachstellbar (2 Läufe allein grün); Benchmark 52.1 fertig (keine Regression; crowd GPU +18 % bei Tag sind brennende Zombies, nachts +0 %; `SCENE_TIME`), voller Verify grün (1955 s); 52.1 samt 52.P und HUD/Trichter in `dev` (`c655887`).

Zweiter Zug auf `feature/37-r2b-block-entity-components` (Worktree `.claude/worktrees/qtcraft-37-r2b`): 37.R2b, 52.H (nachgebessert: Fenster endet mit dem Tod, Kontaktschaden im eigenen Takt, `QcServerSystems`) und 54 (nachgebessert: Prellböcke, wartende Loren an ungeladenen Chunks, Ruhe neben den Gleisen) zusammen, schneller Verify aller 226 Tests grün; Benchmark ohne Regression, voller Verify grün (1973 s); in `dev` (`83f3285`).

Danach parallel: 52.3 Enderman und Enderperle (`feature/52-3-enderman`, Worktree `.claude/worktrees/qtcraft-52-3`, E-460 ff.), 52.6 Eisengolem mit Entstehung bei Panik und nach Arbeit/Schlaf, Bau aus Eisenblöcken und Kürbis (`feature/52-6-iron-golem`, Worktree `.claude/worktrees/qtcraft-52-6`, E-470 ff.); 52.4 (Kühe, Milch heilt Effekte) erst nach dem Merge von 52.2.

Fortschritts-Einträge bis zum nächsten Merge auf `feature/52-3-enderman`.

52.3 nachgebessert, Benchmark ohne Regression, voller Verify grün (2082 s), in `dev` ( `Blow { creature, damage, from, attacker, knockback }`, `damage(const Blow&)`, Komponenten 10/11).

52.3 (E-460: neutrales Gehirn mit Blick auf den Kopf, sichere Teleport-Ziele, Wasser und Regen schaden, Blöcke tragen mit Spielregel `mobGriefing`, Enderperle mit Treffer-Wirkung `teleport`, `confined` für Showcase-Gehege), Prüfung fertig (kein Muss; Ablegen nahm Persistenz, Wut traf den nächsten statt den Angreifer, Blickkegel zu breit auf Distanz, Block-Liste ungeprüft, Teleport einen Tick spät), Nachbesserung läuft.

52.6 fertig (E-470–E-472: Golem-Gehirn, Entstehung bei Panik und nach Schlaf+Arbeit mit 3 Dorfbewohnern, Bau aus Eisenblöcken und geschnitztem Kürbis, Schere, Groll des Dorfs, Stationen `golem_farm` und `golem_workshop`; Komponente 12; Entstehung jetzt auch bei `mobSpawning` aus — Abweichung von E-415, Mensch gefragt; laut Prüfung prüft das Vorbild `doMobSpawning` dabei nicht), Prüfung fertig (Muss nur fürs Zusammenführen: `Blow`/`damage` an 52.3 angleichen; sollte: Golems auf Glas/Dächern, schlafende Bewohner ohne Panik, dorflose Farmen als eine Gruppe, Golems wandern ab, Obergrenze nur geladen, Golems als Tiere gezählt, Tratsch braucht 5 Bewohner wie im Vorbild), nachgebessert (an `Blow`/`damage` aus 52.3 angeglichen, Tratsch 5 / Panik 3 nach Schlaf wie im Vorbild, `sleptAt` gespeichert, nur auf undurchsichtigem Boden, Golems bleiben im Dorf) und mit `dev` (52.3) zusammengeführt (alle 231 Tests grün; Golems jagen Endermen ohne sie auf Spieler zu hetzen); Benchmark ohne Regression (Signal-Stationen +1,5 µs je neuem Wesen), voller Verify: erster Lauf fand ein altes Datenrennen beim Beenden (`QcPreviewSource` wartet jetzt auf den Atlas-Bau) und eine knappe Zeitgrenze in `test_network_game`; zweiter Lauf grün bis auf `test_tick_phases` unter ASan bei Last (allein 3/3 grün); in `dev`.

52.2 zweig-lokal nachgebessert (Effekte nur bei Änderung gesendet, Lege-Zeit gespeichert als Komponente 13), wartet auf 52.6.

52.5 Zähmen (Wölfe/Hunde, Ozelots/Katzen) läuft auf `feature/52-5-taming` (Worktree `.claude/worktrees/qtcraft-52-5`, E-480 ff., Komponenten ab 14).

Zusammenführen der Wesen-Pakete nacheinander: 52.3 → 52.6 → 52.2 (gemeinsame Dateien `QcServerCombat::Blow`, `QcMobManager::damage`, `QcMobDefinition::Ai`, Komponenten, Handbuch).

Beim Zusammenführen: Komponenten-Kennungen 9/10 von 52.3 kollidieren mit 9 (Effekte) aus 52.2 — 52.3 bekommt 10/11, 52.6 ggf. 12.

52.2 auf `feature/52-2-spiders` (Worktree `.claude/worktrees/qtcraft-52-2`): 52.2a Status-Effekte mit Gift (E-450, `effects/*.json`, `/effect`, HUD, Treffer-Wirkungen `effect` und `spawn`) und 52.2b Spinnen und Höhlenspinnen (E-451, Klettern in der gemeinsamen Physik und Wegfindung) fertig; 52.2c fertig (E-452 Hühner, Eier mit `spawn`, Federn nicht mehr von Zombies; E-453 Silberfische, befallene Steine, Adern nur in Bergen; Gehege 91–93), Prüfung von 52.2 fertig (`test_survival_scenario` nur Last: alleine 3/3 grün; Muss: Effekte jeden Tick gesendet, `hurtTimer`-Reste brechen nach dem Merge mit `dev`; sollte: Spinnen-Provokation über `struckBy` aus 52.3, `mobGriefing` für Silberfische, Lege-Zeit speichern, eigene Effekt-Version, benannte Initialisierer).

Zweigeteilt: zweig-lokale Fixes laufen jetzt; Merge-abhängige Punkte beim Zusammenführen nach 52.3 und 52.6.

Beim Zusammenführen: Protokoll 44, Spielerdatei 6, `QcServer.h` (52.H teilt ihn).

37.R2b nachgebessert (Größenschutz 64 MiB, Schwänze beim Neubau verworfen, Mod-Kennungen ab 0x8000, `QCBE` 5).

52.H fertig (E-429, `QcHurtWindow`), Prüfung läuft.

Etappe 54: Prüfung fertig (kein Muss; offene Gleisenden in Stationen, Echtzeit-Test, Kippen auf Zäunen), Nachbesserung läuft.

Plan: nach dem Merge von 52.1 ein zweiter Zug 37.R2b + 52.H + 54 mit einem Benchmark und einem vollen Verify.

Etappe 54 fertig (E-430: `surface.slipperiness` in Original-Einheiten, Eis/Packeis 0,98, blaues Eis 0,989 neu, gemeinsame Physik mit Vorhersage, Wegkosten für Eis, Loren fliegen am Gleisende mit Schwung und setzen wieder auf, Prellbock statt Stopp, Station `ice_slide`, Protokoll 43), Prüfung läuft; Branch `feature/54-slippery-momentum` (Worktree `.claude/worktrees/qtcraft-54`, vom Kandidaten 52.1, E-430 ff.; Mensch 2026-10-17: mehr parallel, Token-Budget reicht).

52.P fertig auf `feature/52-p-param-structs` (`QcPathGround` statt `Context`/`Ground`, `mayPlan` eigener Parameter, `UiParts` aufgelöst, `ChunkCounter`, `Plates` und ein Helfer für den Schaltungs-Kontext; Verhalten gleich, keine E-Nummer), Merge nach 52.1.

52.H läuft parallel auf `feature/52-h-invulnerability` (Worktree `.claude/worktrees/qtcraft-52-h`, E-429).

Bei mehr als zwei parallelen Paketen TSan und Release-Messungen nur zentral (Mensch 2026-10-17, CLAUDE.md).

Merkposten für 52.7: Bett mit verfallenem Anspruch bleibt leer bis zu einem Kind; Wegbudget eventuell ein Viertel für Nicht-Dorfbewohner reservieren.

Folgepakete in der Tabelle; Reihenfolge (Mensch 2026-10-17): 37.R1 → 53 (mit Planer-Teilung) → 37.R2 → 37.R3 → ein gerenderter Benchmark der ganzen Dorf-Sektion (37.1–37.R3 gegen den Stand vor 37, Mensch 2026-10-17: nicht je Teil-Etappe), dazu das Loren-Szenario aus 32 → 52 (beginnend mit 52.0) → 54 → 38 → 51 … GUI-Benchmarks wieder erlaubt (nach 37), offene gerenderte Messungen nachholen.

Folgeaufgaben: Signal-Szenarien an Stationen statt festen Koordinaten binden; Chat-Zeilen im HUD überlappen die Herzreihe; Trichter-Förderlinien mit ~2300 Trichtern bewegten im Benchmark-Entwurf keine Items (untersuchen).

Bei Abbruch: `git log`/`git status` im Worktree, Agent mit dem Rest des Pakets neu beauftragen.

52.2 mit `dev` zusammengeführt (Protokoll 46, `QcMobExchange.h`, Spinnen-Wut über `struckBy`, Silberfische mit `mobGriefing`, Gift im Unverwundbarkeits-Fenster), Benchmark ohne Regression (neue Showcase-Wesen ~0,9 µs je Wesen), voller Verify grün (1660 s), in `dev`.

Als Nächstes 52.5 mit `dev` zusammenführen (Komponente 14, Protokoll 47), dann 52.4 und 52.7.

52.5 (Zähmen) mit `dev` zusammengeführt (eine Ablage für Wesen-Treffer, Protokoll 47, Komponente 14), Benchmark ohne Regression, Atlas-Bau von Menü-Vorschau und Texturpaketen über `QcWorkerPool`/`QcMailbox` statt `QtConcurrent` (TSan-Rennen beim Beenden), voller Verify grün (1367 s), in `dev`.

52.8 eingeplant (Mensch 2026-10-18): `confined` gegen Griefing je Wesen, `noAi` je Wesen, Showcase dann mit `mobGriefing` an.

52.7 (Dorf-Spielweisen: Dörfer aus Betten und Arbeitsplätzen, Spieler-Siedlungen, Umzug, Bauern auf jedem Feld, Essen aufsammeln, Weltformat 16, Showcase-Siedlung) nachgebessert, mit `dev` zusammengeführt, Benchmark ohne Regression, voller Verify grün (2124 s), in `dev`.

Als Nächstes 52.8 (`confined` gegen Griefing, `noAi`, Showcase mit `mobGriefing` an); 52.4 läuft.

52.8 (`mayGrief` mit `confined`, `noAi` je Wesen mit `/mob` und `/summon … noai`, Showcase mit `mobGriefing` an, Dorfreihen-Bewohner confined) geprüft, Benchmark ohne Regression, voller Verify grün bis auf `test_showcase_world` unter ASan (Siedlung bekam ein Kind; Test lässt das jetzt zu), in `dev`.

Client-Spur vorgezogen (Mensch 2026-10-18: parallel, ohne dass die Folgeschritte warten): 46.1a Bestandsaufnahme und Plan (E-530) plus erster Umbau auf `feature/46-1-client-structure`, 40.1 Gamepad-Backend (SDL3 als Submodule im gemeinsamen Cache, eigene Schnittstelle, virtueller Controller; E-540) auf `feature/40-1-gamepad-backend`; danach Steuerung im Spiel nach 46.1, dann 46.2 und 40.2/40.3.

Server-Spur: 52.4 im Abschluss, 38.1 läuft (E-520).

52.4 (Kühe mit Milch, Fische mit Schwärmen und Eimern, Angel; nur Wasser) nachgebessert, mit `dev` abgeglichen, Benchmark ohne Regression, voller Verify bis auf drei Zeittests unter Last (`test_tick_scheduler`, `test_character_screen_qml`, `test_dedicated_server`; allein grün), in `dev`.

Etappe 52 komplett.

40.1 (Gamepad-Backend: SDL 3.4.18 als Submodule im gemeinsamen Cache `~/.cache/qtcraft/sdl3/<tag>-<arch>-c2`, `clientcore/input` ohne SDL, `QcSdlGamepadSource`, virtueller Controller über die Steuerschnittstelle, Lizenzen) geprüft und nachgebessert, gerendert ohne Regression, voller Verify grün (2620 s), in `dev`.

## 2026-10-16

**Keine GUI-Benchmarks (Mensch 2026-10-16 morgens, am Rechner):** keine gerenderten Benchmarks und keine Fensterläufe bis zum Ende von Etappe 37 (komplett, inkl.

37.3 und 37.R); danach sind sie wieder erlaubt (Mensch 2026-10-16) und die offenen gerenderten Messungen werden zuerst nachgeholt. Headless Messungen, Tests und Offscreen-Screenshots sind erlaubt. Gerenderte Messungen sammeln sich unter „Offene Benchmarks“; Fensterläufe immer mit `--screen builtin`.

## 2026-10-15

**Zwischenschritt vor dem vollen Verify von 31.3 (vom Menschen 2026-10-15 beauftragt; Reihenfolge: Benchmark 31.3 → A/C auf eigenem Branch von `dev` und nach `dev` mergen → `dev` in 31.3 mergen → voller `verify.sh` mit A → Merge 31.3 → 49):** voller `verify.sh` schneller: (A) `ctest -j` in allen Test-Schritten, Tests mit gemeinsamen Ressourcen (feste Ports, Datenordner, Subprozesse) per `RESOURCE_LOCK`/`RUN_SERIAL`, unter Last wackelige Tests reparieren; (C) die nächste Etappe darf schon während des finalen Verifys im eigenen Worktree (abgezweigt vom Kandidaten) lesen, planen und Code schreiben, Builds nur mit `nice`, Tests erst nach dem Verify; scheitert der Verify, Fix auf dem alten Branch und in die neue Etappe mergen. (B) TSan im vollen Lauf nur auf einer Teilmenge: erst nach Messung mit A entscheiden.

## 2026-10-14

**Aktuelle Etappe:** Etappe 31.2 (Entities) ist in `dev` (`d24f2b7`, 2026-10-14), davor 31.1 (`8efb027`) und 30 (`fb07253`); dazu der Nebenstrang GitHub (CI-, Release- und Pages-Workflows, GPL-3.0, Web-Handbuch; `94542e4`, E-950). Nach dem Projektzeugnis vom 2026-10-14 hat der Mensch den Plan angepasst (E-370): Etappe 43 (Speicher der Übersetzungen) ist beauftragt und kommt vor 31.3; der Vorschlag 44 (Client-Tick-Thread) ist als 46.2 in Etappe 46 aufgegangen, die Nummer 44 ist stillgelegt; neue Etappe 47 (Feinschliff Darstellung); 39 (Veröffentlichung) rückt ans Ende; neuer Vorschlag 48 (TLS im Mehrspieler). **Reihenfolge ab jetzt:** 43, 31.3, 49, 32, 33, 34, 35, 36, 37 (37.1, 37.2, 37.3, 37.R, 37.R1), 53, 37.R2, 37.R3, 52 (52.0 zuerst), 54, 38, 51, 46 (46.1, 46.2), 40, 41, 47, 45, 55, 50, 39; Vorschläge 42 und 48. Nach `main`/`master` merged nur der Mensch.

**Nächster Schritt:** Etappe 43 (Speicher der Übersetzungen) auf `feature/43-translation-memory` (Branch noch nicht angelegt), von der `dev`-Spitze. Erst headless messen (Speicher und Ladezeit der Übersetzungen, Spitzenspeicher eines headless Laufs), dann die Namen der Teile-Familien zur Laufzeit aus den `%1`-Vorlagen zusammensetzen, Handbuch-, Creative- und Web-Handbuch-Suche finden dieselben Namen, dieselbe Messung nachher.

Danach 31.3 (Chunks entladen) auf `feature/31-3-chunk-unloading`; deren Pflicht-Benchmark misst den Speicher bei 16 Chunks gegen das Ziel ≤ 800 MB (offener Befund in `docs/Performance.md`). **Reihenfolge am Etappen-Ende:** unabhängige Prüfung und Fixes → Benchmark (GUI-Messungen derzeit ausgesetzt, siehe Offene Benchmarks; jede Messung prüft auch die absoluten Ziele) → einmal voller `verify.sh` → Merge. `FORTSCHRITT.md` bei jedem Zwischenschritt aktualisieren und committen (Abbrüche durch Limits). Projektname: „QuaderCraft“ vorgemerkt, vor der Umsetzung (Etappe 39) fragen.

**Umgesetzt (Etappe 30):** E-351 bis E-355. `/summon` zählt nur gerufene wache Wesen (`summonLimit`, Standard 64, 0 = aus) – die „verschwindenden“ crowd-Kreaturen waren abgelehnte Summons; kein Despawn ohne Spieler im Spiel; `QcBodyFit` setzt gerufene Wesen frei (auch neben Zäunen/Mauern, nie aufs Dach, nie unter y 0, nicht in Lava/Magma/Feuer) und schiebt Wesen aus Blöcken (sonst Ersticken); Physik: Rundungs-Überlappungen bis 1e-7 m zählen als Berührung (E-355, auch Spieler); farm-seeds zählt nur bis 32 m (kein Bug); crowd gegen Stände vor 30 nur über `flat-crowd` vergleichen; `QcMobManager` nach Aufgaben geteilt (`QcMobUpkeep`, `QcMobSteering`). Prüfung ohne Blocker, alle Befunde behoben; voller `verify.sh` grün (3908 s); `test_survival_scenario` unter TSan 5/5.

Offen: Flag „gerufen“ wird nicht gespeichert; GUI-Benchmark von crowd mit den neuen Zahlen; Körper breiter als 1 m (nur Mods) an Zaunpfosten nicht geprüft.

**Umgesetzt (Etappe 31.1, Block-Entities):** E-356 bis E-362. Register `QcBlockEntities` (Tabellen je Art mit Chunk-Index) für Container inkl. Trichter, Maschinen, Setzlinge, Schaltungs-Speicher und geplante Ticks; ein Weg für Setzen (`placed()`), Ankommen, Neu-Generieren und Kolben (`relocate`, auch geplante Ticks aller Blöcke); zentrale Prüfung `QcBlockEntityChecks` (in `/worldinfo` und BENCH-Zeile); Seitendateien `blockentities/r.x.z.qcb` je Region über Zwischennamen; Weltformat 3, ältere Welten werden übersetzt abgelehnt (auch im Hauptmenü); `QcSectionMarks` mit 5 Eigenschaften; 24 weitere Grenzen in `QcServerLimits`; Neugliederung (Items/Plants, BlockScreens/Machines, `QcMachineRules`, `QcHopperQueue`); Goldendatei alt/neu als Test. Headless: Tick flach, Speichern +4/+8 %, Öffnen +7 %; gerendert keine Regression außer Signal-Phase an zwei Stationen (+0,013/+0,027 ms, E-362). Prüfung mit 1 Blocker und 7 Soll-Befunden, alle behoben; voller `verify.sh` grün (3915 s).

Offen für 31.2: Neugliederungsliste E-361, alte Lese-Pfade (`QcPlayerStorage::migratedPath`, `QcAccessList` Format 1); für 31.3: Chunk-Index/Lesen bei Bedarf, ganzes Speichern konsistent (Manifest/Journal), ausstehende Schaltungsarbeit und Trichter-Weckliste je Chunk.

**Umgesetzt (Etappe 31.2, Entities):** E-363 bis E-369. Gemeinsame Basis `QcEntity` mit Art und Register `QcEntities`: eine Synchronisierung, eine Ruheregel (`settle`, jeden Tick nach dem Chunk-Laden), `displace` für Kolben (jetzt auch fallende Blöcke), `overlapsLiving` nur über Lebewesen; Spieler nur für Id, Box, Schub und Synchronisierung im Modell (Vorhersage unverändert); Protokoll 32 mit Art-Byte; Entities je Region `entities/r.x.z.qen` (Version 2, Sortierschlüssel = Id), Weltformat 4, alte Welten und alte Lese-Pfade entfernt; übersetzte Meldung mit Ausweg bei beschädigten Dateien; zentrale `QcEntityChecks` (Tests, `/worldinfo`, BENCH); Neugliederung der ganzen E-361-Liste, `QcServerState` als Klasse (Uhr und Client-Liste für Teile nur lesbar, Rest offen benannt in E-367); `Architektur.md` gekürzt, Protokoll-Verlauf in E-368; untätige kalte Maschinen übersprungen (E-369). Headless: Tick flach, Öffnen +6 %, erstes Speichern +7 %, zweites Speichern +35 % (2,66 ms, Dateien je Region; Prüfung in 31.3). Prüfung mit 1 Blocker und 6 Soll-Befunden, alle behoben; voller `verify.sh` grün (4056 s). Gerendert gemessen am 2026-10-14 (keine Regression, `docs/Performance-Verlauf.md`, „Etappe 31.2: gerenderte Messung“).

Offen für 31.3: `QcEntities::settle` nur bei Änderung der geladenen Chunks statt jeden Tick (0,01–0,03 ms), Nehmen/Zurücklegen je Chunk in `QcEntities`, Chunk-Index und Lesen bei Bedarf, Speichern über alle Dateien konsistent, engere Schnittstellen statt `QcServerState &`.
