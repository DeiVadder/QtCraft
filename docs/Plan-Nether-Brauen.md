# Plan: Nether, Brauen, Wither und Leuchtfeuer (Release 1.1.0)

Inhaltspaket für das Release **1.1.0**. Bei der Umsetzung ist 1.0.0 bereits veröffentlicht; dieser Plan baut darauf auf (Mensch 2026-10-30). Geplant im Gespräch mit dem Menschen am 2026-10-30, Stand des Codes 0.1.0; noch nicht beauftragt. Etappen 83–87 (umnummeriert vom Koordinator, weil 81 Ladebildschirme und 82 Handbuch-Icons für 1.0.0 vergeben sind) waren beim Anlegen frei; E-941 bis E-943 sind inzwischen belegt oder reserviert; beim Beauftragen Nummern im Etappen-Plan und im Index neu prüfen und den Abschnitt „Was es schon gibt“ gegen den Stand von 1.0.0 abgleichen.

## Festlegungen des Menschen (2026-10-30)

- **Release:** alles kommt zusammen als 1.1.0, keine Zwischen-Releases.
- **Thema:** Nether und Brauen zusammen; dazu Wither, Nether-Stern und Leuchtfeuer (Beacon) mit ihren Effekten.
- **Vorbild:** nah am Vorbild (Mechanik, Rezepte, Zahlen wie bekannt; eigene Namen nur, wo das Projekt sie schon hat).
- **Umfang:** groß; der Nether vollständig, aber gestaffelt: klassischer Kern zuerst, dann Wälder und Basaltdeltas, Piglins und Bastionen, Netherite.
- **Alte Welten:** Welten aus 1.0.0 (und älteren Releases) bekommen den Nether. Die Generator-Version gilt je Dimension; die Oberwelt behält ihre eingefrorenen Versionen unverändert, der Nether beginnt mit seiner eigenen Version 1, die mit 1.1.0 eingefroren wird.
- **Trank-Items:** Trank-Arten als Daten, die Registry erzeugt daraus die Items (Trinken, Wurf, Verweil) wie die Teile-Familien. Die IDs aus 0.1.0/1.0.0 (`healing_potion`, `harming_splash_potion`, `poison_splash_potion`, `water_bottle`) bleiben gültig, keine Migration.
- **Im Umfang:** Verweiltränke und getränkte Pfeile; Seelenanker und explodierende Betten; Nether-Klang, -Musik und -Partikel; Tageslichtsensor und Quarz-Deko; Wither, Nether-Stern, Leuchtfeuer.
- **Start im Nether:** Weltoption „Startdimension“. Ein Spieler muss vom Nether aus ohne Hilfe bis in die Oberwelt kommen („von Null zur Oberwelt“); Rezepte, Beute, Drops und die erzeugten Blöcke müssen das tragen. Nur Mechaniken des Vorbilds (Obsidian über Tauschhandel mit Piglins), dazu eine Spawn-Hilfe: der Startpunkt im Nether wird in oder nahe einem Karmesin- oder Wirrwald gesucht, auf festem Boden und nicht über Lava.
- **Generator-Umstieg alter Welten** (Etappe 87): ausdrücklich durch den Spieler, als Vorab-Umwandlung vor dem Spielen, Modus `hard` (harte Chunk-Grenzen wie im Vorbild) oder `blend` (Übergangsring).
- **Index der erzeugten Chunks schon vor 1.0.0** (Mensch 2026-10-30): Welten ab 1.0.0 halten fest, welche Chunks je erzeugt wurden, damit Etappe 87 die erkundete Fläche sicher kennt. Das ist ein eigenes Vorpaket für die koordinierende Sitzung (siehe „Vorpaket vor 1.0.0“), nicht Teil von 1.1.0.
- **Das Ende kommt später**, die Architektur der Dimensionen und Portale berücksichtigt es von Anfang an (siehe „Vorsorge für das Ende“). Das Ende selbst ist nicht Teil dieses Plans.

## Was es schon gibt (Stand 0.1.0, vor der Umsetzung gegen 1.0.0 abgleichen)

- Obsidian, Weinender Obsidian, Feuerzeug, Feuer, Lava, Magmablock, Spawner, Kessel, Glasflasche, Wasserflasche.
- Ruinenportale in drei Formen (E-631), rein dekorativ; der Schutt aus roter Terrakotta steht für Netherrack.
- Status-Effekte aus Daten (E-450): Gift, Langsamkeit, Hunger; Milch entfernt alle Effekte (E-500); `/effect`.
- Wurftränke als Projektil mit Treffer-Wirkung `splash` (E-624); Hexe mit Bogenschützen-KI, wirft nach Ziel-Effekt, trinkt Heiltrank (E-623); Hexe nur an ihrer Hütte (E-871).
- Item-Stapel mit optionalen Daten (Etappe 68), Verzauberungen aus Daten (E-864), Schleim (Grundlage für den Magmawürfel), fliegende Wesen (Glühwürmchen), Reiten (Boote, E-920), Angel, Netz-Planer der Minenschächte (E-570, Grundlage der Festung), Weltoptionen (E-917), Fortschritte (E-923/E-924).
- Berufe: Bauer, Fischer, Schmied, arbeitslos (kein Kleriker).
- Versionierter Weltgenerator (E-922): **eine** globale Version, Version 1 eingefroren mit 0.1.0 (E-940); bis 1.0.0 kommen eventuell weitere Oberwelt-Versionen hinzu.

## Was grundlegend fehlt

- **Dimensionen:** Der Server kennt genau eine Welt (`QcServerWorld`). Tick-Phasen, Chunk-Laden, Wesen, Speicherordner, Spielerdatei und Protokoll setzen das voraus. Der Client kennt nur einen Himmel mit Tageslauf, Wetter und Himmelslicht; der realistische Render-Modus (Etappe 45) setzt eine Sonne voraus.
- **Portale:** Kein Block reagiert auf einen Rahmen; es gibt keinen Weg zwischen Welten.
- **Generator-Version je Dimension:** `QcGeneratorVersion` ist global; `world.json` speichert eine Zahl.
- **Erreichbarkeit:** Es gibt keine Prüfung, ob ein Ziel (Werkbank, Ofen, Obsidian …) aus dem, was eine Welt hergibt, erreichbar ist.

## Architektur

### Dimensionen aus Daten

- `dimensions/*.json` je Pack, Name mit Namensraum (`qtcraft:overworld`, `qtcraft:nether`; Mods können eigene ergänzen oder ersetzen).
- Felder (Vorschlag): Generator-Art, Höhe und Decke (Nether: Grundgestein unten und oben bei 127, Welt bleibt 256 hoch), Koordinaten-Maßstab gegenüber der Oberwelt (Nether 8), Himmel-Art (`overworld`, `nether`, `end`) mit Nebelfarbe je Biom, Grundlicht, Himmelslicht an/aus, Tageslauf an/aus, Wetter an/aus, Wasser verdampft, Lava fließt schneller und weiter, Betten explodieren, Kompass und Uhr drehen sich, Boden unten (`bedrock` oder `void` mit Leere-Schaden), fester Ankunftspunkt mit erzeugter Plattform (für das Ende).
- Server: eine `QcServerWorld` je geladener Dimension mit eigenen Chunks, Wesen, Items, Block-Updates, Spawn-Regeln und Tick-Phasen; Chunks einer Dimension laden nur um Spieler darin (Spawn-Chunks nur in der Startdimension). Speicherordner je Dimension (`dimensions/<name>/…` neben dem heutigen Ordner der Oberwelt, die am alten Ort bleibt), Spielerdatei mit Dimension (fehlt sie, Oberwelt). Welten aus 1.0.0 und älter laden ohne Umbau.
- Protokoll: Nachricht für den Dimensionswechsel (Client leert Welt, Meshes, Licht und Wesen, zeigt einen Ladebildschirm), neue Protokoll-Version.
- Befehle: `/tp … in <dimension>`, `/execute in <dimension>`, Dimension in `/locate`, Steuerschnittstelle und Bots kennen die Dimension.

### Portale aus Daten

- `portals/*.json`: Rahmenblock, Form (senkrechtes Rechteck mit Mindest- und Höchstmaß, z. B. 4 × 5 bis 23 × 23; oder waagrechter Ring mit Einsätzen für das Ende), Auslöser (Feuer im Rahmen, Item auf dem Rahmen), Portalblock, Zieldimension, Verknüpfung (`scaled`: Maßstab beider Dimensionen, Suchradius, neues Portal am Ziel, wenn keines gefunden; `fixed`: fester Ankunftspunkt), Rückkehr (`linked` oder `spawn`), Wartezeit (Überleben 4 s, Kreativ sofort), Abklingzeit.
- Nether-Portal wie im Vorbild: Rahmen bricht, Portal erlischt; Wesen, Items und Loren reisen mit; Ghast-Feuerkugeln und Feuer entzünden ebenfalls.
- Die Ruinenportale der Oberwelt werden entzündbar, sobald ihr Rahmen vollständig ist. Das ist eine Regel des Portals, keine Änderung der Struktur, also bleiben die eingefrorenen Oberwelt-Versionen gleich.

### Generator-Version je Dimension (Erweiterung von E-922)

- `world.json` speichert eine Version je Dimension (`generatorVersions: {"qtcraft:overworld": 1, "qtcraft:nether": 1}`); eine Welt aus 1.0.0 oder älter hat nur die Oberwelt-Zahl und bekommt beim ersten Betreten einer neuen Dimension deren aktuelle eingefrorene Version, sonst die offene (vor ihrem ersten Release).
- `QcGeneratorVersion` wird je Dimension geführt (`current`, `lastFrozen`, `oldest`), `test_generator_versions` und `tests/data/generator-versions.json` je Dimension.
- Der Nether ist bis zum Release, das ihn ausliefert, offen: 84.2, 84.3, 84.5a und 84.6b dürfen seine Ausgabe ändern und nehmen die Werte neu auf. Danach gilt die Checkliste von E-922 auch für ihn.
- Abgrenzung zu E-922: dort ist „eine Version je Teil (Gelände, Dörfer, Strukturen)“ verworfen. Eine Version je Dimension ist etwas anderes (getrennte Welten ohne gemeinsame Chunks) und braucht eine eigene Entscheidung in 83.1a.
- Oberwelt-Änderungen im Rahmen dieses Plans (z. B. Netherrack statt Terrakotta im Ruinen-Schutt) brauchen Oberwelt-Version 2; das ist nicht vorgesehen und nur eine offene Frage.

### Startdimension und Erreichbarkeit

- Weltoption `startDimension` im Schema von E-917 (Standard Oberwelt; Welten aus 1.0.0 und älter unverändert). Gehört zur einmaligen Einrichtung einer neuen Welt und damit zur Generator-Version der Startdimension.
- Spawn-Suche im Nether: fester Boden mit zwei Blöcken Luft darüber, nicht über Lava, kein Feuer in der Nähe, bevorzugt in oder am Rand eines Karmesin- oder Wirrwalds; Respawn ohne Seelenanker am Welt-Spawn dieser Dimension.
- Weg im Vorbild ohne Wasser und ohne Eisenerz: Holz aus Karmesin- und Wirrstämmen (Werkbank, Stöcke, Holzwerkzeuge), Schwarzstein zählt als Stein (Steinwerkzeuge, Ofen), Nethergold-Erz zu Goldnuggets und -barren, Tauschhandel mit Piglins (Obsidian, Weinender Obsidian, Eisennuggets, Feuerkugeln, Wasserflaschen, Feuerresistenz-Tränke, Kies, Seelensand, Faden, Leder), Feuer zum Entzünden von natürlichem Feuer auf Netherrack, Lava oder Feuerkugeln, Essen von Hoglins sowie roten und braunen Pilzen (Pilzsuppe).
- **Erreichbarkeitsprüfung** (`test_progression_reachability`): berechnet aus der Inhalts-Registry (Rezepte, Schmelzen, Steinschneider, Block-Drops, Wesen-Beute, Truhen-Beute, Tauschhandel mit Piglins und Dorfbewohnern) und den Blöcken, Wesen und Strukturen, die der Generator einer Dimension erzeugt, ob Schlüsselziele erreichbar sind: Werkbank, Holz- und Steinwerkzeug, Ofen, Feuer, 10 Obsidian bzw. Portal, Nahrung, Weg zur Oberwelt. Läuft je Startdimension (Oberwelt, Nether, später das Ende) und meldet den fehlenden Schritt. Mods können eigene Ziele ergänzen.

### Umstieg alter Welten auf einen neuen Generator (Mensch 2026-10-30)

**Heute:** Eine Welt ist fest an ihre Generator-Version gebunden (E-922); der Server öffnet sie nur mit genau dieser Version. Gespeichert werden die Blöcke nur von Chunks, die sich verändert haben (`QcChunkLoading::departuresForSave`); unveränderte Chunks erzeugt der Generator bei jedem Laden neu, nur ihre Wesen-Daten (`populated`) bleiben. Ein bloßes Umstellen der Version wäre darum fatal: jeder schon besuchte, aber unveränderte Chunk sähe nach dem Update anders aus, neben veränderten Chunks und gespeicherten Wesen, die zum alten Gelände gehören, und halb geschriebene Bauwerke (Dörfer, Minenschächte) bekämen einen anderen oder keinen Plan.

**Ziel:** Eine bestehende Welt kann der Spieler ausdrücklich auf die neue Generator-Version umstellen (nie automatisch; Behalten bleibt der Standard). Alles schon Erzeugte bleibt genau wie es war; nur noch nie erzeugte Chunks bekommen die neue Version.

- **Version je Chunk statt Kopie der Blöcke:** Beim Umstieg wird die erkundete Fläche als „alte Version“ markiert (eine Versionskarte je Region im Speicher der Welt). Der Generator erzeugt je Chunk mit der Version aus dieser Karte, sonst mit der Version der Welt. So wachsen die Region-Dateien nicht, und unveränderte alte Chunks bleiben weiterhin reproduzierbar. Planer, die über Regionen arbeiten (Dörfer, Minenschächte, Verliese, Türme, Oberflächen-Bauwerke), planen je Version; ein Bauwerk, das ein alter Chunk schon angefangen hat, behält seinen alten Plan auch in neuen Chunks, die neue Version setzt dort keine eigenen Bauwerke, die mit ihm überlappen.
- **Erkundete Fläche bestimmen:** Gebraucht wird die Menge aller jemals erzeugten Chunks. Heute kennt die Welt sicher nur die geänderten Chunks und die mit Wesen-Datensatz (`populated`); ob jeder erzeugte Chunk einen solchen Datensatz bekommt, prüft das Paket zuerst. Deshalb kommt schon vor 1.0.0 ein Index der erzeugten Chunks (Vorpaket unten); für Welten aus 0.1.0 leitet 87.1 die Fläche aus geänderten Chunks und Wesen-Datensätzen ab.
- **Zwei Arten des Umstiegs:**
  - `hard`: harte Chunk-Grenzen wie im Vorbild – alt bleibt alt, neu beginnt am nächsten nie erzeugten Chunk. Nähte sind erlaubt.
  - `blend`: ein Ring von N Chunks (Einstellung, Vorschlag 4–8) außerhalb der erkundeten Fläche wird mit beiden Versionen erzeugt und vom alten (innen) zum neuen (außen) überblendet: Höhe und Dichte des Geländes interpoliert, Oberflächen- und Biom-Blöcke nach Anteil gewählt, Flüsse und Seen auf die Höhe der Mischung gelegt, Erze und Höhlen aus der näheren Seite, Bauwerke im Ring nur aus dem alten Plan. Die Ring-Chunks werden dabei fest gespeichert (sie sind von keiner Version allein reproduzierbar).
- **Vorab-Umwandlung statt live:** Der Umstieg läuft vor dem Spielen, nicht während: beim ersten Öffnen einer Welt mit älterer Version fragt das Spiel (Behalten / Umstellen hart / Umstellen mit Übergang), legt vorher eine Sicherung an (vorhandene Backups), wandelt mit Fortschrittsanzeige um und öffnet die Welt erst danach. Abbrechen lässt die Welt unverändert. Headless: `qtcraft-headless --convert-generator <welt> --to <version> --mode hard|blend [--blend-chunks N]`, für dedizierte Server und Tests.
- **Je Dimension:** Umstieg gilt je Dimension (Oberwelt und Nether getrennt), zusammen mit der Generator-Version je Dimension.
- **Was nicht dazugehört:** veränderte Chunks bleiben unangetastet; die Vorführwelt braucht keinen Umstieg (immer aktuell); Mods, die den Generator ändern, liegen beim Spieler (wie E-922).

### Vorsorge für das Ende

- Himmel-Art `end`, Boden `void` mit Leere-Schaden, fester Ankunftspunkt mit erzeugter Plattform, Portal-Form „waagrechter Ring mit Einsätzen“, Verknüpfung `fixed` und Rückkehr `spawn` sind Teil der Formate von 83.
- Eine Testdimension „endartig“ (leer, feste Plattform, Einweg-Portal) in den Tests von 83 beweist, dass die Formate tragen, ohne Inhalt des Endes.
- Die Bossleiste aus 86.1 ist allgemein (Name, Anteil, Farbe, Sichtbarkeit nach Entfernung bzw. Dimension) und später auch für den Drachen.
- Generator-Version je Dimension: ein späteres Ende startet mit eigener Version 1.
- Startdimension ist allgemein; ob man im Ende starten kann, entscheidet die Etappe des Endes.

## Definition of Done je Paket

- Inhalt als JSON unter `content/qtcraft/`, Texte in `en.json`/`de.json` (Handbuch in `*.handbook.json`), `scripts/export-handbook.sh`.
- Handbuch-Kapitel bzw. -Abschnitt; Fortschritte, wo das Vorbild sie hat.
- Showcase-Station in der Oberwelt der Vorführwelt (Blöcke, Wesen in Gehegen, Brau- und Leuchtfeuer-Station); ab 83.3 ein funktionierendes Portal der Vorführwelt in ihren Nether, dessen Ankunft ebenfalls Stationen hat (Vorführwelt ist von der Versionsbindung ausgenommen, E-922).
- Prozedurale Texturen und Klänge wie bisher, Texturpaket-Schlüssel dokumentiert.
- Tests, Verify nach `CLAUDE.md`; Pakete mit Server-Host, Threads, Netz oder Meshing (83.1a, 83.1b, 83.2, 83.3) mit zentralem TSan; Headless-Messung `@server` für 83.1b (zwei Dimensionen geladen) und für die neuen Wesen.
- Entscheidungen als `## E-<Nummer>` in `docs/Entscheidungen.md`.

## Etappen und Arbeitspakete

Größe je Paket: ein Agent, höchstens rund 400k Tokens. Größen S/M/L wie in `docs/FORTSCHRITT.md`.

### Etappe 83: Dimensionen (Technik)

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 83.1a | Server-Kern: Format `dimensions/*.json` (Felder oben), Registry und Parser, mehrere `QcServerWorld` im Server-Zustand, Speicherordner je Dimension, Spielerdatei mit Dimension, Generator-Version je Dimension in `world.json` und `QcGeneratorVersion`, Laden von Welten aus 1.0.0 und älter ohne Umbau; Testdimensionen (leer, endartig) nur in Tests | – | L |
| 83.1b | Tick-Phasen, Chunk-Laden, Wesen, Items, Block-Updates, Spawn-Regeln und Wetter je Dimension; Spieler zwischen Dimensionen versetzen (Server-Seite); `/tp … in`, `/execute in`, `/locate`, Steuerschnittstelle, Bots, Admin-Ansicht; Headless-Messung mit zwei geladenen Dimensionen | 83.1a | L |
| 83.2 | Client: Protokoll-Nachricht Dimensionswechsel, Welt leeren und Ladebildschirm, Himmel-Art und Nebel aus Daten, Licht ohne Himmelslicht, kein Tageslauf und Wetter, realistischer Modus ohne Sonne (keine Schatten-Kaskaden, eigenes Umgebungslicht), Kompass und Uhr drehen sich, Karten je Dimension, Mehrspieler mit Spielern in verschiedenen Dimensionen | 83.1b | L |
| 83.3 | Portale: Format `portals/*.json`, Rahmen erkennen und entzünden, Portalblock (Achse, Licht 11, Partikel, Ton), Reisen mit Wartezeit, Verknüpfung `scaled` (Suchen, Erzeugen, Plattform aus Obsidian über Lava/Leere) und `fixed`, Wesen, Items und Loren reisen mit; Ruinenportale entzündbar; endartige Testdimension mit Einweg-Portal; Showcase-Portal; Handbuch „Portale“. Ziel-Nether bis 84.2: leere Testdimension mit Netherrack-Boden | 83.1b, 83.2 | L |

### Etappe 84: Nether

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 84.1 | Blöcke: Netherrack (Feuer brennt ewig), Seelensand (verlangsamt), Seelenerde, Glowstone (+ Staub), Netherquarz-Erz und Quarz-Familie (Block, Säule, gemeißelt, glatt, Ziegel; Stufe, Treppe), Tageslichtsensor (umkehrbar, Signalquelle nach Tageszeit), Nethergold-Erz, Netherziegel-Familie (rot und normal; Stufe, Treppe, Mauer, Zaun), Basalt (poliert, glatt), Schwarzstein-Familie (poliert, Ziegel, gemeißelt, vergoldet; zählt als Stein für Werkzeuge und Ofen), Seelenfeuer, Seelenfackel, Seelenlaterne, Netherwarze (wächst nur auf Seelensand); Showcase-Station. Kann parallel zu 83 laufen (in der Oberwelt testbar) | – | M–L |
| 84.2 | Nether-Generator: Gelände aus 3D-Rauschen (Höhlen, Decke, Grundgestein bei 0 und 127), Lavasee bei 31, Biome Netherwüste und Seelensandtal (Biom-Karte des Nethers ohne Klima der Oberwelt), Glowstone-Trauben an der Decke, Quarz- und Nethergold-Erz, Lavaquellen, natürliche Feuer, Kies- und Seelensand-Flecken; Ruinenportale im Nether; Nether-Version 1 offen, Referenzwerte aufnehmen | 83.3, 84.1 | L |
| 84.3 | Karmesin- und Wirrwald, Basaltdeltas: Nylium (zwei Arten), Stämme und Hyphen samt entrindeten Varianten, Holzfamilien (Bretter, Stufe, Treppe, Zaun, Zauntor, Tür, Falltür, Knopf, Druckplatte, Schild; nicht brennbar), Pilze (mit Knochenmehl zu großen Pilzen), Warzenblöcke, Pilzlicht, Ranken (hängend und aufsteigend), Wurzeln und Sprossen; Basalt-Säulen, Schwarzstein, Magma; Biome im Nether-Generator | 84.2 | L |
| 84.4 | Wesen I: zombifizierter Piglin (neutral, Gruppenzorn, Goldbeute), Magmawürfel (Schleim-Variante, Magmacreme), Ghast (Flieger, Feuerkugel als Projektil, zurückschlagbar, Ghastträne, Schwarzpulver), Schreiter (läuft auf Lava, Reiten mit Sattel und Wirrpilz an der Angel, Faden); Spawn-Regeln je Nether-Biom; Gehege in der Vorführwelt | 84.2 | L |
| 84.5a | Netherfestung: Netz-Planer nach dem Muster der Minenschächte (Brücken, Korridore, Kreuzungen, Treppen, Treppenhäuser, Lohen-Plattform mit Spawner, Netherwarzen-Beet auf Seelensand), Truhen-Beute (u. a. Sattel, Gold, Diamant, Obsidian, Netherwarze), Spawn-Gebiet der Festung | 84.2 | L |
| 84.5b | Lohe (schwebt, Feuerkugeln in Salven, Lohenrute, nur am Spawner und in der Festung) und Witherskelett (Steinschwert, Wither-Effekt aus 85.1, Kohle, Knochen, selten Witherskelett-Schädel); Lohenstaub; Feuerkugel als Item (Lohenstaub, Kohle, Schwarzpulver) | 84.5a, 84.4 | M |
| 84.6a | Piglins und Hoglins: Piglin (feindlich ohne Goldrüstung, Gold-Abbau und Truhen erzürnen, nimmt Gold auf), Tauschhandel mit Goldbarren nach Beutetabelle wie im Vorbild (Obsidian, Weinender Obsidian, Eisennuggets, Feuerkugel, Wasserflasche, Feuerresistenz-Trank, Kies, Seelensand, Faden, Leder, Netherziegel …), Piglin-Kinder; Hoglin (Angriff, Zucht mit Karmesinpilz, Schweinefleisch, Leder, fürchtet Wirrpilze) | 84.3, 84.4 | L |
| 84.6b | Bastion-Ruine: Struktur in mehreren Formen (Brücke, Stallungen, Schatzkammer, Wohnbereich), Schwarzstein-Familie, Gold, Piglin-Rohlinge, Truhen-Beute (Netherite-Schrott optional nach 84.8, Schmiedevorlage), Lava-Brücken | 84.6a | L |
| 84.7a | Erreichbarkeitsprüfung `test_progression_reachability` (Abschnitt oben): Graph aus Registry und Generator-Ausgabe, Ziele als Daten, Meldung des fehlenden Schritts; zunächst für die Oberwelt (muss heute schon grün sein) und den Nether | 84.6a | M |
| 84.7b | Nether-Start: Weltoption `startDimension`, Spawn-Suche im Nether (Spawn-Hilfe), Respawn ohne Anker, Seelenanker (Weinender Obsidian + Glowstone, Laden mit Glowstone, explodiert in der Oberwelt), explodierende Betten in Dimensionen mit dem Feld; Erreichbarkeitsprüfung für den Nether-Start grün; Handbuch „Im Nether beginnen“; Fortschritte | 84.7a | M |
| 84.8 | Netherite: Antiker Schrott (tief, explosionsfest), Netherite-Schrott und -Barren, Schmiedetisch mit Schmiedevorlage (Diamant → Netherite für Werkzeuge und Rüstung), Netherite-Block (für 86.2), Items brennen nicht in Lava; Beute in Bastionen | 84.6b | M |
| 84.9 | Atmosphäre: prozedurale Umgebungsgeräusche und Musik je Nether-Biom, Partikel (Asche, Sporen, Seelen, tropfende Lava und Tränen am Weinenden Obsidian), Nebel je Biom, Klänge der neuen Blöcke und Wesen nachziehen | 84.3, 84.4 | M |

### Etappe 85: Brauen

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 85.1 | Effekte II als Daten mit ihrer Wirkung im Server bzw. Client: Schnelligkeit, Stärke, Schwäche, Regeneration, Sofortheilung, Sofortschaden (bei Untoten umgekehrt), Feuerresistenz (Feuer, Lava, Magma), Wasseratmung, Nachtsicht (Client-Licht), Unsichtbarkeit (Zielwahl der Wesen, Darstellung), Sprungkraft, Sanfter Fall, Eile (Abbau-Tempo), Resistenz, Wither, Absorption; Effekt-Anzeige im HUD; `/effect`. Kann parallel zu 83 laufen | – | L |
| 85.1c | Sättigung (Mensch 2026-11-04: ohne sie war der Survival-Playtest eine Qual): verborgener Sättigungs-Puffer wie im Vorbild – jedes Essen gibt Nahrung und Sättigung (Werte je Item als Daten), Erschöpfung zehrt erst die Sättigung, dann die Nahrung; schnelle Regeneration bei voller Leiste mit Sättigung; ersetzt den Ausgleich ohne Puffer aus E-100 (Erschöpfungswerte prüfen); Sättigung als Effekt (85.1, sofort Nahrung und Sättigung); Spielerdatei (nächste Version), Handbuch „Hunger“; optional Anzeige im Debug-Overlay | 85.1 | M |
| 85.2 | Braustand: Block-Objekt mit drei Flaschenplätzen, Zutat, Lohenstaub als Brennstoff (20 Vorgänge), 20 s Brauzeit, Bildschirm über das Protokoll, Trichter und Komparator; Trank-Arten als Daten (`potions/*.json`: Effekte, Dauer, Stärke, Farbe), aus denen die Registry Trinken-, Wurf- und Verweil-Items erzeugt (IDs aus 0.1.0/1.0.0 bleiben); Brau-Rezepte als Daten (`brewing/*.json`: Basis + Zutat → Ergebnis): Wasserflasche + Netherwarze → seltsamer Trank, Zutaten des Vorbilds (u. a. Zucker, Glitzernde Melonenscheibe, Spinnenauge, Fermentiertes Spinnenauge, Magmacreme, Ghastträne, Goldene Karotte), Glowstone → Stufe II, Signalstaub → länger, Schwarzpulver → Wurf, Fermentiertes Spinnenauge kehrt um; Wasserflasche aus dem Kessel; Showcase-Brauerei; Handbuch „Brauen“ | 85.1, 84.5b (Lohenstaub; Netherwarze aus 84.1) | L |
| 85.3 | Verweiltränke als Wolke (Entity mit Radius und Dauer, schrumpft je Wirkung; Zutat siehe offene Frage 2) und getränkte Pfeile (8 Pfeile um einen Verweiltrank, Effekt beim Treffer, Bogen und Spender) | 85.2 | M |
| 85.4 | Hexe vollständig (E-623 erweitern): wirft Langsamkeit, Schwäche, Gift, Schaden je nach Ziel; trinkt Feuerresistenz, Wasseratmung, Schnelligkeit, Heilung bei Bedarf; natürlicher Spawn wieder (73.2 rückgängig), Beute des Vorbilds (Glowstone-Staub, Zucker, Spinnenauge, Glasflasche, Signalstaub, Stöcke, Schwarzpulver); Beruf Kleriker mit Braustand als Arbeitsplatz und Handel; Fortschritte (Brauen, Tränke); Handbuch | 85.2 | M |

### Etappe 86: Wither und Leuchtfeuer

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 86.1 | Wither: Beschwörung aus Seelensand/-erde (T-Form) und drei Witherskelett-Schädeln, Aufladen mit Explosion, Flieger mit drei Köpfen und Zielwahl, Wither-Schädel als Projektil (blau: stark), zerstört Blöcke (Spielregel), zweite Phase ab halber Gesundheit (Pfeile prallen ab), Wither-Effekt, Nether-Stern (brennt nicht, explosionsfest); allgemeine Bossleiste (Protokoll, HUD, für den späteren Drachen); Fortschritte; Showcase als Beschwörungs-Arena ohne Auto-Start | 84.5b, 85.1 | L |
| 86.2 | Leuchtfeuer: Block aus Nether-Stern, Obsidian und Glas, Pyramide in 4 Stufen aus Eisen-, Gold-, Smaragd-, Diamant- und Netherite-Blöcken, Reichweite je Stufe, Strahl (Darstellung, durch gefärbtes Glas gefärbt), Bildschirm zur Wahl von Haupt- und Zweiteffekt (Schnelligkeit, Eile, Resistenz, Sprungkraft, Stärke; Regeneration bzw. Stufe II ab Stufe 4), Bezahlung mit Eisenbarren, Goldbarren, Smaragd, Diamant oder Netherite-Barren; Fortschritte; Showcase | 86.1, 84.8 | M–L |

### Vorpaket vor 1.0.0: Index der erzeugten Chunks

Beschlossen vom Menschen am 2026-10-30; gehört in den Etappen-Plan der koordinierenden Sitzung vor dem Release 1.0.0, nicht zu 1.1.0.

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| V.1 | Index der erzeugten Chunks: jede Welt merkt sich dauerhaft, welche Chunks der Generator je erzeugt hat (z. B. ein Bit je Chunk im Index der Region-Dateien oder eine eigene kleine Datei je Region), auch wenn der Chunk unverändert bleibt und seine Blöcke nicht gespeichert werden. Schreiben mit dem normalen Speichern und dem Speicher-Journal (E-373), kein Einfluss auf die Ausgabe des Generators (Generator-Versionen bleiben grün), Welten ohne Index lesen weiter (Index beginnt leer). Test: Fläche erkunden ohne Änderung, speichern, neu laden, Index enthält genau die erzeugten Chunks; Abbruch während des Speicherns verliert keinen Eintrag, der vorher gespeichert war. Messung: Speicherzeit und Plattenplatz headless (`@server`). Vorher prüfen, ob `populated` schon jeden erzeugten Chunk abdeckt; dann reicht es eventuell, diesen Datensatz verlässlich für jeden erzeugten Chunk zu schreiben | – | S–M |

### Etappe 87: Generator-Umstieg alter Welten

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 87.1 | Grundlage: erkundete Fläche aus dem Index der erzeugten Chunks (Vorpaket), für Welten aus 0.1.0 ohne Index abgeleitet aus geänderten Chunks, `populated` und Karten; Versionskarte je Region im Weltspeicher; Generator und Planer erzeugen je Chunk mit der Version aus der Karte; Test: eine Welt der alten Version, Fläche erkunden und teils ändern, Umstieg, alle alten Chunks bitgleich wie vorher (auch unveränderte), neue Chunks mit der neuen Version | 83.1a | L |
| 87.2 | Umstieg `hard` und Vorab-Umwandlung: Rückfrage beim Öffnen (Behalten / hart / mit Übergang), Sicherung vorher, Fortschrittsanzeige, Abbruch ohne Änderung, `world.json` mit neuer Version und Versionskarte; Bauwerke über die Grenze behalten ihren alten Plan; `--convert-generator` in `qtcraft-headless`; Handbuch „Welt auf neuen Generator umstellen“ | 87.1 | M |
| 87.3 | Umstieg `blend`: Ring von N Chunks aus beiden Versionen erzeugen und überblenden (Höhe/Dichte, Oberfläche, Biom-Blöcke, Wasser, Höhlen und Erze, Bauwerke nur alt), Ring fest speichern; Bilder vorher/nachher zur Freigabe durch den Menschen; Messung der Umwandlungszeit headless (Ziel: ein Ring um eine typische Welt in unter 1–2 min) | 87.2 | L |

Zum Prüfen braucht 87 eine zweite Oberwelt-Version. Gibt es bis dahin keine echte, bekommt der Test eine Testversion (ein kleiner, absichtlicher Unterschied nur in Tests), damit der Umstieg ohne geänderten Spielinhalt geprüft werden kann.

## Reihenfolge und Parallelität

Höchstens drei Agenten zugleich (Regel des Menschen).

1. **Start:** 83.1a, parallel 84.1 und 85.1 (alle drei ohne Abhängigkeit, getrennte Dateien).
2. 83.1b, danach 83.2, danach 83.3. Parallel dazu 85.1 und 84.1 zu Ende.
3. Nach 83.3: 84.2, dann 84.3 und 84.4 (parallel möglich, beide brauchen nur den Generator), 84.5a.
4. 84.5b, dann 85.2 (Brauen hängt am Lohenstaub). Parallel: 84.6a.
5. 84.7a, 84.7b (Nether-Start), parallel 85.3 und 84.6b.
6. 85.4, 84.8, 84.9.
7. 86.1, dann 86.2. Etappe 87 kann ab 83.1a parallel laufen (87.1 nach 83.1a, da beide Weltspeicher und Generator-Version anfassen; nicht gleichzeitig mit 83.1a).
8. Meilenstein: Benchmark, dann voller Verify mit `--release`, Release 1.1.0; dabei `lastFrozen` des Nethers auf `current` setzen. Vorher kein Zwischen-Release: Pakete landen in `dev`, `main` bekommt erst 1.1.0.

Etappe 83 ist die riskante: die Umstellung auf mehrere Welten berührt Server-Zustand, Tick, Speichern, Netz und Client. 83.1a beginnt mit einer kurzen Karte der Stellen, die heute „die Welt“ voraussetzen (rund 73 Dateien nennen `QcServerWorld`), und teilt bei Bedarf weiter.

## Offene Fragen

1. **Einordnung gegen die übrigen Arbeiten nach dem Release** (72.5–72.12c QDoc, 79.47/79.48/79.49 Aufräumen, offene Nebenfunde 79.x): vorher, verschränkt oder danach? Entscheidet der Mensch mit der koordinierenden Sitzung.
2. **Verweiltränke ohne Ende:** Im Vorbild braucht der Verweiltrank Drachenatem aus dem Ende. Ersatz-Zutat bis zum Ende (Vorschlag: Ghastträne auf einen Wurftrank) oder 85.3 erst mit dem Ende?
3. **Ruinen-Schutt:** Terrakotta bleibt (eingefrorene Oberwelt-Versionen). Soll eine neue Oberwelt-Version in 1.1.0 echten Netherrack in die Ruinen legen?
4. **Zutaten, die es noch nicht gibt:** Kugelfisch (Wasseratmung), Kaninchenpfote (Sprungkraft), Schildkrötenpanzer (Schildkrötenmeister), Phantommembran (Sanfter Fall). Ersatz-Zutaten, eigene kleine Pakete für die Wesen, oder diese Tränke zunächst weglassen?
5. **Wither-Zerstörung im Mehrspieler:** eigene Spielregel `witherGriefing` neben der vorhandenen Regel für Wesen, die Blöcke verändern?
6. **Lava und Leistung:** Der Nether hat große Lavaseen; ob die Flüssigkeits-Ticks reichen, misst 84.2 headless. Bei Bedarf ein Folgepaket.
7. **Texturpakete:** Neue Schlüssel für Nether-Blöcke im HD-Paket und im realistischen Modus (Material-Karten) – jeweils im Block-Paket oder in einem eigenen Texturpaket am Ende?
8. **Ring-Breite und Standard bei `blend`:** Vorschlag 6 Chunks; im Menü einstellbar oder fest?
