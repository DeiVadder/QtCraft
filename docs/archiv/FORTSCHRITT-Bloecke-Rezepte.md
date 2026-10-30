# Fortschritt: Blöcke und Rezepte (Nebenstrang)

> **Archiv – abgeschlossener Nebenstrang** (`feature/blocks-recipes`, in `dev` gemergt; E-200 ff.). Wird nicht mehr nachgeführt; den aktuellen Stand nennt `docs/FORTSCHRITT.md`.

Wiederaufnahme-Dokument für diesen Nebenstrang. Ein frischer Agent (auch unter einem anderen Account) liest zuerst **dieses** Dokument, danach `QtCraft-Agent-Auftrag.md` (Regeln, Größengrenzen, Verifikation) und bei Bedarf `docs/Entscheidungen.md`. `docs/FORTSCHRITT.md` gehört dem Hauptstrang (Etappen 20 ff.) und wird hier **nicht** angefasst.

## Rahmen

- **Branch:** `feature/blocks-recipes`, Worktree `.claude/worktrees/blocks-recipes`, abgezweigt von `dev` d41bb29 (nach Etappe 19b) am 2026-10-09.
- **Parallel:** Auf `feature/20-polish` (+ `20a/20b/20c`) arbeitet ein anderer Agent an Etappe 20. Konflikte möglichst klein halten: nur Inhalt (`content/qtcraft`), Sprachdateien, Handbuch-Export und eng begrenzte C++-Stellen anfassen.
- **Merge:** **Nicht selbst nach `dev` mergen.** Branch fertigstellen, verifizieren, berichten; der Mensch entscheidet über den Merge (wegen der parallelen Etappe 20).
- **Entscheidungsnummern:** In diesem Strang **E-200 ff.**, damit sie nicht mit dem Hauptstrang (zuletzt E-089) kollidieren; beim Merge ggf. umnummerieren. Neue Entscheidungen hier unten im Abschnitt „Entscheidungen“ notieren und beim Abschluss nach `docs/Entscheidungen.md` übernehmen.
- **Regeln aus dem Auftrag gelten:** Texturen nur prozedural (`content/qtcraft/textures/*.json`), Commit-Messages ohne Attribution, Funktionen ≤ 80 Zeilen, Dateien ≤ 800 (Header 300), gezielte Verifikation mit `scripts/verify.sh --quick [--tests <regex>]`, voller `scripts/verify.sh` einmal am Ende, unabhängige Prüfung durch einen neuen Agenten (ohne eigene Subagenten).
- **Pro neuem Block/Item:** Block-/Item-JSON, Textur-Spec, `lang/en.json` + `lang/de.json`, Beschreibung fürs Handbuch (`description`, Texte in `lang/*.handbook.json`), Rezept; danach Handbuch-Export neu erzeugen (`qtcraft-headless --export-handbook docs/handbook` und `--language de` nach `docs/handbook/de`), weil `test_handbook` die eingecheckten Dateien vergleicht.

## Auftrag (vom Menschen, 2026-10-09)

Stufe 1 (reine Daten) und Stufe 2 (wenig Code) aus der Analyse umsetzen, mit diesen Vorgaben:

1. **Speicherblöcke und Nuggets:** neben Speicherblöcken (9 ↔ 1) auch Nuggets (1 Barren ↔ 9 Nuggets). Nuggets ermöglichen z. B. eine **Mini-Fackel** mit reduziertem Licht.
2. **Glühwürmchen-Glas:** muss **sehr schwach** leuchten. Nether und Glowstone kommen später und müssen eine Verbesserung bleiben. Idee: leicht leuchtender, transparenter Block.
3. **Leder aus Rotten Flesh:** **deutlich ineffizienter** als später Kühe (passive Mob-Zucht ist geplant; Rotten Flesh fällt in Massen an). Weg bleibt dem Agenten überlassen (viel Flesh pro Leder, Lederstreifen, lange Ofenzeit …).
4. **Schlamm:** langsamere Bewegung **und** reduzierte Sprunghöhe/-weite.
5. **Beton wie Vanilla:** erst Pulver (fällt), wird erst durch Wasser zu Beton.
6. **Ton wie Vanilla:** Tonblock droppt Tonklumpen, Klumpen → Ofen → Ziegel-Item, 4 Ziegel → Ziegelblock; Tonblock → Ofen → Terrakotta.
7. Rest der Analyse wie vorgeschlagen.

Ausgeklammert, weil eigene spätere Etappen: Rüstung (23), Landwirtschaft/Nahrung im großen Stil (27), Signal Dust (29), Dörfer (30) (alte Planung; heute Etappe 37), Nether/Glowstone. Stufe 3 (Schere, Bett, Schild, Gerüst, Effekte) ist **nicht** beauftragt.

## Stand für die Wiederaufnahme

- Stufe 1 und 2 sind umgesetzt, getestet (`test_new_blocks` + angepasste Tests, 55 CTest-Tests grün), dokumentiert (`docs/Modding.md`, Handbuch-Export) und per Screenshot geprüft.
- **Abgeschlossen (2026-10-10):** voller `scripts/verify.sh` ALL GREEN (1374 s, Debug/ASan/TSan/Release/Analyse/Smoke), unabhängige Prüfung FAIL → behoben → Nachprüfung PASS; Entscheidungen in `docs/Entscheidungen.md` übernommen. **Integriert in Etappe 20:** auf `feature/blocks-integration` mit Merge-Commit 9172c48 zusammengeführt (Protokoll 18, Entscheidungen E-200 bis E-217 nach E-104); die Prüfbefunde der Integration sind dort behoben (Netze ohne Stütze, wirksame Lichtdämpfung im Handbuch, kein Spawnen auf Magma und in Netzen, `scoops`/`bottles` schließen sich aus). Der Merge nach `dev` läuft mit Etappe 20. Bekannte kleine Lücke: kein Test für „Lava neben dem Standblock schadet nicht“ (Spieler im Test steht immer mittig).
- Screenshots neu erzeugen: Hilfsskript war nur lokal; Prinzip: `build/debug/src/app/qtcraft.app/Contents/MacOS/qtcraft --play --seed 42 --look 0,-22 --no-audio --chat "@200 /time set day" --chat "@220 /setblock ~0 ~ ~-6 lantern" …` mit `QTCRAFT_SCREEN=builtin QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR=<tmp> QTCRAFT_SCREENSHOT=<png> QTCRAFT_SCREENSHOT_FRAMES=240`.

## Plan und Status

Legende: `[ ]` offen, `[~]` angefangen, `[x]` erledigt (mit Commit).

### A. Erkundung
- [x] A1 Engine-Fähigkeiten prüfen: `movement` bei festen/nicht-flüssigen Blöcken, `contactDamage` bei festen Blöcken, `reactions` außerhalb von Flüssigkeiten, Container auf `facing_cube`, Block-Id-/Zustandsgrenzen der Registry, Textur-Muster (`QcTextureSynthesizer`), Handbuch-/Inhaltstests, die Anzahlen festschreiben.

### B. Reine Daten (Stufe 1)
- [x] B1 Nuggets: Eisen-, Gold-Nugget (1 Barren ↔ 9). Speicherblöcke: Eisen-, Gold-, Diamant-, Kohleblock (Brennwert), Schleimblock, Packeis (9 ↔ 1).
- [x] B2 Mini-Fackel (Nugget/Kohle + Stock o. ä., Licht deutlich unter Fackel 14), Laterne (Eisennuggets + Fackel, Licht 15, hängend/stehend falls einfach).
- [x] B3 Glühwürmchen-Glas: transparent, sehr schwaches Licht (Richtwert 5–6), teuer genug, dass Glowstone später besser ist.
- [x] B4 Goldwerkzeuge (Spitzhacke, Axt, Schaufel, Schwert; schnell, Haltbarkeit 32, niedrige Stufe).
- [x] B5 Holz: Birken-/Fichtentür und -falltür, Holzblock (Rinde rundum) je Holzart.
- [x] B6 Eisengitter (`pane`), Eisentür, Eisenfalltür.
- [x] B7 Ton wie Vanilla (Klumpen, Ziegel-Item, Ziegelblock aus 4, Lampengehäuse aus Klumpen), Terrakotta + gefärbte Terrakotta.
- [x] B8 Neue Blumen/Pflanzen: Maiglöckchen (weißer Farbstoff), Orchidee im Sumpf (hellblau), Zierlauch (magenta), toter Busch (Wüste, droppt Stöcke), Farn (Taiga). Neue Farbstoffe/Wollfarben bis zu den 16 Vanilla-Farben, soweit sinnvoll ableitbar.
- [x] B9 Gefärbtes Glas und gefärbte Scheiben für alle Farben.
- [x] B10 Sandstein: glatter (Ofen), gemeißelter Sandstein; glatter als Material mit Bauteilen.
- [x] B11 Äpfel aus Eichenlaub; Schüssel; Pilzsuppe; Papier aus Zuckerrohr; Buch; Bücherregal.
- [x] B12 Leder aus Rotten Flesh (ineffizient, siehe Vorgabe 3).
- [x] B13 Fass (Container), falls Container auf `facing_cube` ohne Code geht.

### C. Wenig Code (Stufe 2)
- [x] C1 Bewegungsfaktor in nicht-flüssigen Blöcken (Netz) und auf dem Untergrund (Schlamm), dazu Sprungfaktor (Schlamm).
- [x] C2 Netz in Höhlen (Feature), droppt Faden; 4 Faden → weiße Wolle.
- [x] C3 Magmablock (schwaches Licht, Kontaktschaden beim Draufstehen) am Rand von Lavaseen/-becken.
- [x] C4 Schlamm im Sumpf (Worldgen), Schlammziegel, Material mit Bauteilen.
- [-] C5 (zurückgestellt, E-213) Grobe Erde / Podsol für die Taiga (falls Biom-Oberfläche konfigurierbar).
- [x] C6 Wasserflasche (Glasflasche auf Wasser), Schlamm aus Erde + Wasserflasche.
- [x] C7 Betonpulver (fällt) → Beton bei Wasserkontakt (Nachbar-Reaktion für feste Blöcke).

### D. Abschluss
- [x] D1 Handbuch-Texte, Handbuch-Export neu, Screenshots (`docs/screenshots/br-*.jpg`).
- [x] D2 Tests ergänzen (Rezepte, Reaktion Beton, Bewegung Schlamm/Netz, Kontaktschaden Magma).
- [x] D3 Voller `scripts/verify.sh`, Performance-Check, unabhängige Prüfung, Bericht an den Menschen. **Kein Merge.**

## Erkundung (A1, erledigt)

- Bauteile entstehen **nur** für Materialien in `materials/*.json`; neue Blöcke ohne Material-Eintrag bekommen keine Bauteile (kein Eintrag in `excluded.json` nötig).
- `contactDamage` wirkt nur auf Blöcke **im** Körper (`QcServerWorld::contactDamageIn`, Box mit Inset) – Draufstehen schadet nicht. Für Magma muss der Block unter den Füßen dazukommen.
- Bewegungsfaktor gibt es nur für Flüssigkeiten (`flow.movement`, `QcPhysics::liquidFactorAt`). Physik läuft auf Server **und** Client (Vorhersage) ⇒ neue Felder müssen im Content-Serializer zum Client.
- `reactions` gibt es nur in `flow` von Flüssigkeiten (`QcLiquidFlow::reaction`). Block-Updates: `QcBlockSimulation::processUpdate` (jede Änderung reiht sich selbst + 6 Nachbarn ein); beim Laden von Chunkrändern nur Blöcke mit `reactsToNeighbour` (`QcBlockUpdateQueue.cpp`).
- Eimer: Item `scoops` + `flow.bucket`; Schöpfen entfernt die Quelle (`QcServerInteraction::scoopLiquid`); der Client castet bei `scoops` auch auf Flüssigkeiten (`QcClientSession.cpp`).
- Essen: `QcServerCombat::consume` entfernt das Item; `emptiesTo` wird beim Essen bisher nicht beachtet.
- Container hängen nur an `container.slots` (nicht an der Form) ⇒ Fass mit `facing_cube` geht ohne Code.
- Texturen: Muster in `QcTextureSpec.h` / `QcTextureSpec.cpp` (Namen) / `QcTextureSynthesizer.cpp` (545 Zeilen; neue Muster in eine eigene Datei, Grenze 800). Nur `glass`/`water` behalten Teiltransparenz.
- Sprachdateien `lang/*.json` sind nicht sortiert (neue Schlüssel ans Ende), `lang/*.handbook.json` sind sortiert. JSON-Format = `json.dumps(indent=4, ensure_ascii=False)` + Zeilenende.
- Grenzen: 65 536 Blockzustände, also kein Problem.

## Entscheidungen (E-200 ff.)

- **E-200 Nuggets und Kohlestücke:** `iron_nugget`, `gold_nugget` (1 Barren ↔ 9). Dazu `coal_chunk` (Kohlestück): 1 Kohle oder Holzkohle → 9, 9 → 1 Kohle; Brennwert 1/9 der Kohle. Speicherblöcke `iron_block`, `gold_block`, `diamond_block`, `coal_block` (Brennwert 9 × Kohle), `slime_block`, `packed_ice`, jeweils 9 ↔ 1 (Packeis nur 9 Eis → 1).
- **E-201 Kleine Fackel und Laterne:** `small_torch` aus Kohlestück + Stock (1 Stück), Licht 10 (Fackel 14), kleinere Form; pro Kohle 9 kleine statt 4 großer Fackeln. `lantern` aus 8 Eisennuggets um eine Fackel, Licht 15, Form wie Minenlampe (Boden/Decke/Wand).
- **E-202 Glühwürmchenglas:** durchscheinender Block, Licht **4**, lässt Licht durch; Rezept 4 Glas im Plus um eine Glühwürmchen-Flasche → 4. Droppt sich selbst. Glowstone (später, 15) bleibt deutlich besser.
- **E-203 Leder aus Rotten Flesh:** Ofen: Rotten Flesh → `leather_scrap` (Lederfetzen) in 15 s; 9 Fetzen (3 × 3) → 1 Leder. Pro Leder also 9 Rotten Flesh und 135 s Ofenzeit plus Brennstoff; Kühe (später) bleiben weit günstiger.
- **E-204 Ton wie Vanilla:** Tonblock droppt 4 `clay_ball`; 4 Klumpen → Tonblock; Klumpen im Ofen → `brick` (Item); 4 Ziegel → Ziegelblock; Tonblock im Ofen → `terracotta`. Ofenrezept Ton → Ziegelblock entfällt. Lampengehäuse aus Tonklumpen statt Tonblöcken.
- **E-205 16 Farben wie Vanilla:** neue Farbstoffe weiß (Maiglöckchen), hellblau (Orchidee), magenta (Zierlauch), braun (brauner Pilz; Vanilla: Kakao), rosa (rot + weiß), hellgrün (grün + weiß), türkis (blau + grün), grau (schwarz + weiß), hellgrau (grau + weiß). Neue Wollfarben dazu, Teppiche kommen über die Bauteil-Familie.
- **E-206 Farbige Familien ohne Bauteile:** gefärbtes Glas (8 Glas + Farbstoff → 8), gefärbte Scheiben (6 → 16), Terrakotta (8 + Farbstoff → 8), Betonpulver (4 Sand + 4 Kies + Farbstoff → 8), Beton (aus Pulver per Wasser, siehe E-208).
- **E-207 Bewegung in und auf Blöcken:** Blockfelder `slowsInside` (Faktor für Körper im Block, Netz: 0,25 waagrecht, Fallen stark gebremst) und `surface: {speed, jump}` (Untergrund, Schlamm: Tempo 0,6, Sprungkraft 0,93 ⇒ Sprunghöhe ≈ 1,06 statt 1,23 Blöcke, ein voller Block bleibt knapp überspringbar; die Sprungweite sinkt vor allem über das Tempo). Läuft in `QcPhysics` (Server und Client), Felder im Content-Serializer.
- **E-208 Aushärten:** Blockfeld `hardens: {with, into}`: liegt der Block an einem Block `with` (jeder Zustand, z. B. Wasser), wird er zu `into`. Geprüft in `QcBlockSimulation::processUpdate` vor dem Fallen (Pulver im Wasser härtet statt weiterzufallen) und beim Laden von Chunkrändern.
- **E-209 Magma:** `contactDamage` zählt auch den **festen** Block unter der Fußmitte (`damageFromBelow` in `QcServerCombat.cpp`); eine nach unten verlängerte Box hätte Lava neben dem Standblock mitgezählt (Prüfbefund). Magmablock: Licht 3, Schaden 1, als Erzvene tief im Stein (y 5–24). Kein Rezept (kein Magmacreme).
- **E-210 Schlamm:** Sumpf-Ufer (`beach`) wird Schlamm statt Gras; Zuckerrohr, hohes Gras und Orchidee wachsen auch auf Schlamm (sonst verschwand Zuckerrohr aus Sümpfen, Prüfbefund). Erde + Wasserflasche → Schlamm; Schlamm im Ofen → `packed_mud` (gestampfter Schlamm; Vanilla: + Weizen); 4 → 4 `mud_bricks`, Material mit Bauteilen.
- **E-211 Wasserflasche:** Liquid-Feld `flow.bottle`, Item-Feld `bottles` (Glasflasche; nicht zusammen mit `scoops`): füllt aus Wasser, ohne die Quelle zu entfernen. Rezept-Rückgaben gibt es nicht ⇒ die Flasche geht im Schlamm-Rezept auf.
- **E-212 Essen mit Rückgabe:** `consume` beachtet `emptiesTo` (Pilzsuppe → Schüssel).
- **E-214 Feature- und Erz-Seeds nach Namen:** `QcTerrainDefinitions` leitete die Seeds aus der Listenposition ab (`1000/2000/3000 + Index`, Erze `100 + Index`); jedes neue Feature verschob dadurch Bäume und Erze der ganzen Welt (aufgefallen an `test_survival_scenario`: Spawn plötzlich unter Laub). Jetzt `qcNameSalt(name)` (FNV-1a) + fester Versatz je Zweck. Bestehende Welten bekommen in neuen Chunks einmalig andere Bäume/Erze an Chunkgrenzen; danach sind Inhaltszugänge (auch Mods) stabil.
- **E-215 Schwarzer Farbstoff aus Kohlestück:** `#coals` allein im Raster ergibt jetzt 9 Kohlestücke; schwarzer Farbstoff kommt aus 1 Kohlestück (vorher 1 Kohle).
- **E-216 Netze als „Erz“ in Höhlenluft:** Es gibt keinen Feature-Typ für Höhlenwände; Netze kommen über `ores/cobweb.json` mit `"replaces": "air"` (y 10–50, rund 3 pro Chunk). Das Handbuch zeigt „Höhlenluft“ statt eines Links auf Luft; `test_terrain_generator` zählt Netze als ausgehöhlt. Netze haben `"support": "none"` (hängen frei), `lightOpacity` 0, bleiben `washable` (Begründung in `docs/Entscheidungen.md`).
- **E-217 Fallendes Pulver im Wasser:** landet Betonpulver in einer Zelle mit seinem `hardensWith`-Block, wird dort sofort `hardensInto` gesetzt (`QcBlockSimulation::landBlock`), sonst würde es das Wasser verdrängen und Pulver bleiben.
- **E-213 Grobe Erde / Podsol:** zurückgestellt (Biome kennen nur eine Oberfläche; Flecken bräuchten neues Worldgen).

## Log

- 2026-10-10: Integration in Etappe 20 (`feature/blocks-integration`, Merge 9172c48, Protokoll 18); Prüfbefunde der Integration behoben (Netz-Stütze und -Lichtdämpfung, Handbuch zeigt wirksame Lichtdämpfung von Teilformen, Mob-Spawns meiden `contactDamage`/`slowsInside`, Parser lehnt `scoops` + `bottles` ab, Beton-Test getrennt nach direkt/diagonal, Doku).

- 2026-10-10: voller `verify.sh` ALL GREEN; Nachprüfung PASS; Parser-Tests für `hardens`/`slowsInside`/`surface` ergänzt; Entscheidungen übernommen.

- 2026-10-10: Unabhängige Prüfung FAIL (keine Blocker): Lava-Randschaden durch Magma-Box, Zuckerrohr fehlte im Sumpf, Inhaltstest war für formlose Rezepte wirkungslos; Kleinigkeiten: `dyes`-Tag der neuen Farbstoffe, Name des `terracotta`-Tags, Falltest zu locker, Härten in sich selbst. Alles behoben. Akzeptiert: Netze können in Höhleneingängen sichtbar hängen; Schlamm wirkt nur unter der Fußmitte.

- 2026-10-09: 3e4cf7e neue Textur-Muster (`QcTextureCraftPatterns`, Hilfen in `QcTexturePaint.h`). 205ad32 Seeds nach Namen (E-214). 311ce2b Daten Stufe 1 (B1–B13 außer Pilzsuppen-Rückgabe, die Code braucht): rund 110 neue Blöcke, 30 Items, 150 Rezepte; Tests angepasst (`test_content_pack`, `test_building`, `test_world_details`, `test_handbook`), Handbuch-Export neu. Daten wurden mit einem Python-Skript erzeugt (nicht eingecheckt); maßgeblich sind die JSON-Dateien. Ungeprüft: Aussehen der neuen Texturen im Spiel (Screenshots folgen in D1).
- 2026-10-10: e23a73f Stufe 2 (Netz, Schlamm, Magma, Wasserflasche, Beton, Suppenschüssel; Protokoll 16 wegen neuer Blockfelder im Content-Sync). Danach neuer Test `test_new_blocks` (14 Fälle: Physik Schlamm/Netz, Beton am und im Wasser, Magma, Flasche, Suppe, Rezeptketten, 16 Farben), Schlamm-Sprungkraft 0,93, Landung von Pulver im Wasser (E-217). `verify.sh --quick` grün (55 Tests).
- 2026-10-10: Sichtprüfung per Spiel-Screenshots (`docs/screenshots/br-colours.jpg`, `br-blocks.jpg`; erzeugt mit `--chat "@<ms> /setblock …"`-Folgen): Farben, Lampen, Pflanzen, Netz, Magma passen; gefärbtes Glas war zu blass ⇒ eigenes Muster `stained_glass` (Alpha 120). Offen in D1: Item-Icons im Inventar ansehen.

- 2026-10-09: Worktree und Branch angelegt, Plan geschrieben.
