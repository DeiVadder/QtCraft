# Plan: Das Ende (Release 1.2.0)

Inhaltspaket für das Release **1.2.0**. Geplant im Gespräch mit dem Menschen am 2026-11-10, Stand des Codes 1.1.0 (`main` be0ee8d); noch nicht beauftragt. Etappen 90–94 waren beim Anlegen frei (88 und 89 tragen Qt 6.12, Linux und die Musik), die Entscheidungsnummern ab E-1160 ebenfalls; beim Beauftragen im Etappen-Plan und im Index neu prüfen.

## Festlegungen des Menschen (2026-11-10)

- **Release:** alles kommt zusammen als 1.2.0, keine Zwischen-Releases.
- **Umfang:** vollständig, gestaffelt wie der Nether: zuerst der Kern (Festung, Endauge, Endportal, Hauptinsel, Drache, Rückweg), dann äußere Inseln mit Endtransitportalen, Chorus, Purpur und Endstädten, dann Shulker und Elytren, dazu die Verweiltränke mit Drachenatem (offen seit 85.3).
- **Vorbild:** Mechanik, Rezepte und Zahlen wie im Vorbild; Wesen mit eigenem Namen und eigenem Aussehen, Werte und Verhalten vom Vorbild, je Wesen eine Liste alt/neu/Quelle (Regel der Wesen).
- **Festungen:** brauchen eine neue **Oberwelt-Generator-Version 4** (Version 3 ist mit 1.1.0 eingefroren). Neue Welten bekommen Festungen; alte Welten erst nach dem Generator-Umstieg aus Etappe 87 (`hard` oder `blend`), vorher führt aus ihnen kein Weg ins Ende. In Version 4 kommt auch der Fix der Flussdelta-Arme (Zeile 79.97).
- **Generator des Endes:** eigene Serie, Version 1, eingefroren mit 1.2.0.
- **Kein Start im Ende:** die Weltoption `startDimension` bietet das Ende nicht an.
- **Drache wie im Vorbild:** nach dem ersten Sieg mit vier Endkristallen auf dem Ausgangsportal wiederbelebbar; das Drachenei nur beim ersten Sieg; jeder Sieg öffnet ein Endtransitportal (höchstens 20).

## Was es schon gibt (Stand 1.1.0)

- **Dimensionen und Portale aus Daten** (Etappe 83): Himmel-Art `end`, Boden `void` mit Leere-Schaden, fester Ankunftspunkt mit erzeugter Plattform, Portal-Form „waagrechter Ring mit Einsätzen“, Verknüpfung `fixed`, Rückkehr `spawn`; die Testdimension „endartig“ in `test_dimensions` beweist die Formate.
- **Generator-Version je Dimension** (E-922 erweitert), Index der erzeugten Chunks (79.52), Generator-Umstieg `hard`/`blend` mit Vorab-Umwandlung (Etappe 87).
- **Schattengänger** (`shadewalker`, Gegenstück des Vorbilds): Blick-Zorn, Teleport, weicht Geschossen aus, nimmt Wasserschaden, trägt Blöcke; **Schattenperle** (`shade_pearl`) als Wurf-Item mit Projektil.
- **Silberfischchen** und befallene Steinblöcke, Steinziegel-Familie (rissig, bemoost, gemeißelt): Bausteine der Festung.
- **Bossleiste** (86.1) allgemein, Flieger-Boss mit Schädel-Geschossen (Verkohlte Hydra), Spielregel `mobGriefing`.
- **Brauen** (85.2/85.3): Trank-Arten aus Daten, die Registry erzeugt Trink- und Wurftränke; getränkte Pfeile aus Wurftränken. Verweiltränke fehlen (warten auf Drachenatem).
- **Effekte** u. a. Sanfter Fall, Unsichtbarkeit; **Rucksack** (Etappe 68) mit Inhalt in den Item-Daten (Grundlage der Shulkerkiste).
- Zutaten: Lohenrute und -staub (`ember_rod`, `ember_dust`), Ghastträne (`wailer_tear`), Schwarzpulver, Papier, Glas, Obsidian, Grundgestein.
- **Erreichbarkeitsprüfung** `test_progression_reachability` (84.7a) mit Zielen als Daten.

## Was fehlt

- Festung im Oberwelt-Generator (Netz-Planer, Ringe), Endauge, Endportalrahmen und Endportal.
- Generator des Endes: Hauptinsel, Obsidiansäulen mit Kristallen, Ausgangsportal, äußere Inseln ab 1000 Blöcken mit eigenen Biomen.
- Drache als Boss mit Flugphasen, Kristall-Heilung, Atem und Wiederbelebung.
- Endtransitportal, Chorus-Pflanze, Purpur, Endstab, Endstein-Familie, Endstädte mit Schiffen.
- Shulker, Schwebe-Effekt, Shulkerkiste, Leere-Membran, Elytren mit Gleitflug, Feuerwerksrakete als Antrieb, Endertruhe.
- Musik, Klang, Partikel und Himmel des Endes; Abspann nach dem Sieg (eigener kurzer Text).

## Architektur

### Festung (Oberwelt-Version 4)

- Lage wie im Vorbild: 128 Festungen in 8 Ringen um den Ursprung (erster Ring 3 Festungen in 1280–2816 Blöcken), Winkel und Abstand aus dem Seed, auf Biome ohne Ozean verschoben. Erzeugung als eigener Generator neben Minenschacht und Netherfestung (`src/worldgen`), Netz-Planer nach deren Muster.
- Räume wie im Vorbild: Wendeltreppe am Einstieg, Korridore, Kreuzungen, Kerker, Brunnenraum, Bibliothek (ein- und zweistöckig), Lagerraum, Portalraum mit Lavabecken, Silberfischchen-Spawner und zwölf Endportalrahmen (je 10 % mit Auge). Befallene Blöcke in den Wänden.
- Version 4 ist eine ganze Oberwelt-Version: alle Änderungen an der Oberwelt für 1.2.0 (Festung, Delta-Fix 79.97, Endstab-Deko o. Ä.) gehören hinein, Prüfsummen offen bis zum Release.

### Endauge und Endportal

- Endauge: Schattenperle + Lohenstaub. Geworfen fliegt es zur nächsten Festung (Server sucht über die Lagen der Festungen, nicht über erzeugte Chunks), schwebt kurz, fällt mit 80 % zurück, zerbricht mit 20 %.
- Endportalrahmen als Block mit Blickrichtung und Zustand „Auge eingesetzt“; der Rahmen aus zwölf Blöcken mit Augen nach innen füllt die 3 × 3 Mitte mit Endportal-Blöcken. Portal-Daten: Form „waagrechter Ring mit Einsätzen“, Verknüpfung `fixed` zur Plattform des Endes, Rückkehr `spawn` durch das Ausgangsportal.

### Generator des Endes (Serie `end`, Version 1)

- Leere bis auf Inseln aus Endstein; Hauptinsel um den Ursprung (Radius rund 100 Blöcke), zehn Obsidiansäulen im Kreis (Radius 42, Höhen aus dem Seed), Endkristall auf jeder, zwei mit Eisengitter-Käfig.
- Ausgangsportal aus Grundgestein am Ursprung, inaktiv bis zum Sieg; Plattform aus Obsidian bei 100/49/0 für die Ankunft (Daten aus 83).
- Äußere Inseln ab 1000 Blöcken Abstand vom Ursprung (Rauschen wie im Vorbild, Leere-Ring dazwischen), Biome Hochland, Mittelland, Barren, kleine Inseln; Chorus-Pflanzen im Hochland; Endstädte.
- Himmel-Art `end` (dunkel, Sternenfeld, kein Tageslauf, kein Wetter), Grundlicht wie im Vorbild.

### Drache

- Eigenes Wesen (eigener Name und Aussehen), mehrteilige Trefferbox (Kopf, Hals, Körper, Flügel, Schwanz), Leben 200, Schaden nach Vorbild.
- Zustandsautomat der Phasen wie im Vorbild: Kreisen um die Säulen, Anflug mit Feuerball (Drachenatem-Wolke am Boden), Sturzflug auf Spieler, Landen auf dem Ausgangsportal, Atem auf dem Portal, Abheben. Endkristalle heilen ihn mit einem Strahl; zerstörte Kristalle explodieren.
- Sieg: Todesanimation, Erfahrung 12000 beim ersten, 500 bei jedem weiteren Sieg, Drachenei beim ersten Sieg, Ausgangsportal aktiv, ein Endtransitportal je Sieg (20 feste Plätze auf einem Kreis von Radius 96).
- Zustand des Kampfs in den Weltdaten des Endes (erster Sieg, Kristalle, Transitportale), nicht im Wesen.
- Bossleiste aus 86.1, sichtbar für Spieler auf der Hauptinsel.

### Elytren

- Gleitflug nach den Gleichungen des Vorbilds (Auftrieb über Blickwinkel, Luftwiderstand, Sturzschaden an Wänden), Haltbarkeit 432, verbraucht 1 je Sekunde Flug.
- Feuerwerksrakete (Papier + Schwarzpulver, Flugdauer 1–3) als Schub im Flug und als Wurf-Feuerwerk ohne Stern zunächst.
- Server autoritativ wie die übrige Bewegung; der Client sagt den Flug voraus (Prediction wie beim Laufen), Kamera folgt dem Flug.

## Definition of Done je Paket

Wie beim Nether-Plan (`docs/Plan-Nether-Brauen.md`, Abschnitt „Definition of Done je Paket“): Inhalt als JSON, Texte in `en.json`/`de.json` und Handbuch, Fortschritte wie im Vorbild, Showcase-Station (ab 90.3 ein funktionierendes Endportal der Vorführwelt mit Stationen am Ankunftsort), prozedurale Texturen und Klänge, Tests, Entscheidungen als `## E-<Nummer>`. Zentrales TSan für Pakete mit Server-Host, Threads, Netz oder Meshing (90.3 neue Dimension, 93.2 Flug-Vorhersage); Headless-Messung `@server` für den Drachen und das Ende mit Endstädten.

## Etappen und Arbeitspakete

Größe je Paket: ein Agent, höchstens rund 400k Tokens. Größen S/M/L wie in `docs/FORTSCHRITT.md`.

### Etappe 90: Weg ins Ende

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 90.1 | Oberwelt-Version 4: Festung (Lage in Ringen, Netz-Planer, Räume, Portalraum mit Rahmen, Silberfischchen-Spawner, Bibliothek mit Büchern, Truhen-Beute nach Vorbild); Delta-Fix 79.97 in Version 4; `/locate stronghold`; Kartenvorschau zeigt Festungen nicht (unterirdisch) | – | L |
| 90.2 | Endauge (Rezept, Flug zur nächsten Festung, Zerbrechen), Endportalrahmen-Block, Endportal-Block und Portal-Daten `end`, Rückweg über Ausgangsportal; Fortschritte „Augen auf“, „Festung“ | 90.1, 90.3 (Ziel) | M |
| 90.3 | Generator des Endes Version 1: Hauptinsel, Säulen mit Kristallen und Käfigen, Ausgangsportal inaktiv, Ankunftsplattform; Endstein; Himmel, Licht und Nebel des Endes; Schattengänger spawnen dort in Mengen; Kartenvorschau-Umschalter um das Ende erweitern | – | L |

### Etappe 91: Drache

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 91.1 | Drache: Wesen, Trefferbox aus Teilen, Phasen-Automat, Kristall-Heilung, Feuerball und Atem-Wolke (Schaden), Sieg (Erfahrung, Ei, Ausgangsportal, eigener kurzer Abspann, überspringbar), Zerstörung unter `mobGriefing`, Bossleiste, Kampf-Zustand in den Weltdaten; Drachenkopf als Beute der Endstädte (Block, mit 92.3) | 90.3 | L |
| 91.2 | Wiederbelebung mit vier Endkristallen (Rezept Glas + Endauge + Ghastträne), Säulen und Kristalle neu aufbauen; Endtransitportale je Sieg (Block, Teleport zu den äußeren Inseln, Plattform am Ziel erzeugen); Drachenei (teleportiert beim Anklicken) | 91.1 | M |
| 91.3 | Verweiltränke: Drachenatem in Flaschen (aus der Atem-Wolke), Verweiltrank-Items für alle Trank-Arten aus der Registry, Wolke als Entität (Radius, Dauer, Wirkung je Kontakt), getränkte Pfeile aus Verweiltränken wie im Vorbild (ersetzt den Weg aus 85.3 oder ergänzt ihn) | 91.1 | M |

### Etappe 92: Äußere Inseln

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 92.1 | Äußere Inseln im Generator des Endes (Rauschen ab 1000 Blöcken, Biome Hochland, Mittelland, Barren, kleine Inseln), Kartenvorschau des Endes | 90.3, 91.2 (Weg dorthin) | M |
| 92.2 | Chorus-Pflanze (Wachstum nach Vorbild, nur auf Endstein), Chorusfrucht (zufälliger Teleport), geplatzte Chorusfrucht, Purpur-Familie (Block, Säule, Stufe, Treppe), Endstab (Licht), Endstein-Ziegel-Familie | 92.1 | M |
| 92.3 | Endstädte: Türme in Teilen nach Vorbild, Endschiffe mit einem Paar Elytren im Rahmen und Drachenkopf, Truhen-Beute (Diamant, Eisen, Gold, verzauberte Ausrüstung, Sattel), Shulker in den Türmen (mit 93.1) | 92.2, 93.1 | L |

### Etappe 93: Shulker, Elytren, Endertruhe

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 93.1 | Shulker (eigener Name und Aussehen; Schale, öffnet sich, Geschoss mit Schwebe-Effekt, teleportiert bei Schaden, Schale als Beute), Effekt Schweben; Milbe (5 % je Perlenwurf, Beute Leerstaub); Leere-Membran (Chorus + Leerstaub, Brau-Zutat für den Trank Sanfter Fall); Shulkerkiste nicht in Kiste oder Rucksack; Shulkerkiste (Rezept, behält Inhalt beim Abbauen in den Item-Daten, Färben, 16 Farben) | 90.3 | M |
| 93.2 | Elytren: Gleitflug (Server und Vorhersage), Haltbarkeit, Reparatur am Amboss mit Leere-Membran, Sturzschaden, Feuerwerksrakete als Schub; Kamera; Fortschritte „Höhenflug“; Steuerung mit Gamepad | 92.3 (Fundort) | L |
| 93.3 | Endertruhe (Obsidian + Endauge): ein Inventar je Spieler über alle Endertruhen und Dimensionen, in der Spielerdatei | 90.2 | S |

### Etappe 94: Atmosphäre und Release

| Paket | Inhalt | Hängt ab von | Größe |
|-------|--------|--------------|-------|
| 94.1 | Atmosphäre des Endes: erzeugte Musik (nach 88.3) für das Ende und den Drachenkampf, Umgebungsgeräusche, Partikel (Portal, Kristallstrahl, Atem), Klänge der neuen Blöcke und Wesen nachziehen | 91.1, 92.2 | M |
| 94.2 | Erreichbarkeit „von Null bis zum Drachen“ in `test_progression_reachability` (Festung, Augen, Portal, Sieg, Elytren); Fortschritts-Baum „Das Ende“ vervollständigen; Showcase; Texturpaket HD und realistischer Modus für alle neuen Blöcke gesammelt | alle | M |
| 94.4 | Fortschritte in Reitern (Oberwelt, Nether, Ende …): Reiter als Datenfeld, Standard-Reiter für Mods ohne Feld, eigene Reiter für Mods, Oberfläche mit Reitern (Bildabnahme), Namen stabil | 94.2 | M |
| 94.3 | Release 1.2.0: Benchmark gegen 1.1.0, voller Verify `--release`, Prüfsummen Oberwelt 4 und Ende 1 auf den VMs, `lastFrozen` setzen, Release-Notizen | alle | M |

## Reihenfolge und Parallelität

Höchstens drei Agenten zugleich (Regel des Menschen).

1. **Start:** 90.1 (Oberwelt) und 90.3 (Ende) parallel, getrennte Generatoren.
2. 90.2 nach beiden; parallel 93.1 (braucht nur das Ende) und 93.3.
3. 91.1, dann 91.2 und 91.3 parallel.
4. 92.1, 92.2, dann 92.3 (mit 93.1).
5. 93.2, 94.1, 94.2.
6. Meilenstein 94.3: Release 1.2.0. Pakete landen in `dev`, `main` bekommt erst 1.2.0.

Riskant sind 91.1 (Boss mit mehrteiliger Trefferbox und Flugphasen) und 93.2 (Flug mit Vorhersage über Netz): beide beginnen mit einer kurzen Karte der berührten Stellen (Trefferbox und Kollision bei 91.1, Bewegung und Vorhersage bei 93.2) und teilen bei Bedarf weiter.

## Entscheidungen des Menschen (2026-11-10, zweite Runde)

1. **Kein Phantom.** Elytren repariert eine Ersatz-Zutat aus dem Ende: die **Leere-Membran** (neues Item, Rezept: geplatzte Chorusfrucht + Leerstaub, ergibt 2; Leerstaub siehe 10). Sie repariert Elytren am Amboss wie die Phantomhaut im Vorbild und ist die Brau-Zutat für den Trank „Sanfter Fall“ (fehlt bisher als Trank). Shulker lassen nur ihre Schale fallen.
2. **Eigener kurzer Abspann** nach dem Drachensieg: eigener Text, dann die Namen des Projekts und der Musik (Scott Buckley), überspringbar; danach Rückkehr zum Spawn wie im Vorbild.
3. **Zerstörung durch den Drachen unter `mobGriefing`** wie die Hydra; Endstein, Obsidian, Grundgestein und Eisengitter bleiben immer.
4. **Ein Paar Elytren je Endschiff**, auch im Mehrspieler (wer zuerst kommt).
5. **Texturen gesammelt in 94.2** (HD-Paket und realistischer Modus), wie beim Nether.
6. **Feuerwerk:** 93.2 bringt nur die Rakete (Papier + Schwarzpulver, Flugdauer 1–3) mit einfachem Effekt; Feuerwerkssterne (Farbe, Form, Spur, Funkeln) als eigenes kleines Paket nach 1.2.0.
7. **Flugtempo wie im Vorbild** (rund 30 Blöcke/s mit Raketen). 93.2 misst headless und im Fenster, ob Chunk-Laden und Meshing mitkommen; reicht es nicht, folgt ein eigenes Leistungspaket (Laden in Flugrichtung bevorzugt), das Spieltempo bleibt.
8. **Fortschritte in Reitern** (Oberwelt, Nether, Ende, eventuell Abenteuer und Landwirtschaft) ab 1.2.0, eigenes Paket 94.4; für 1.1.0 hängt 79.98 den Nether-Zweig in den einen Baum. **Mod-Kompatibilität:** der Reiter ist ein Datenfeld des Fortschritts (bzw. der Wurzel); Mods ohne das Feld landen in einem Standard-Reiter, Mods können eigene Reiter anlegen; die Namen der Fortschritte bleiben stabil (gespeicherter Fortschritt).
9. **Reihenfolge:** zuerst die Speicher-Pakete aus 79.96 (Klangbank int16, Menü-Registry, Partikel-Abschnitte), dann das Ende; QDoc 72.x und das Aufräumen 79.47/79.49 verschränkt nebenher.
10. **Milbe** (Gegenstück der Endermilbe, eigener Name und Aussehen): entsteht mit 5 % beim Werfen einer Schattenperle, Werte nach Vorbild, Schattengänger greifen sie an; Beute **Leerstaub** (Zutat der Leere-Membran). Chorus lässt sich nach dem ersten Besuch zu Hause pflanzen, danach geht die Reparatur ohne weitere Reisen ins Ende.
11. **Nichts ineinander:** keine Shulkerkiste in einer Shulkerkiste, kein Rucksack in einer Shulkerkiste und keine Shulkerkiste im Rucksack.
12. **Vorführwelt mit Endportal:** fertiges Portal, Ankunft mit Stationen (Drache als Schaustück ohne Kampf, Shulker, Chorus, Endstadt-Ausschnitt); im Kreativmodus zusätzlich `/tp … in end`.
13. **Endertruhe je Spieler je Welt**, in der Spielerdatei der Welt.
14. **Kleinere Punkte wie im Vorbild** (Mensch 2026-11-10: „passt so, mach wie im Vorbild“): geworfene Perlen reisen durch Endtransitportale; der Drache hat feste Werte unabhängig von der Spielerzahl, die Bossleiste sehen alle auf der Hauptinsel; Welten aus 1.1.0 bekommen das Ende nach dem Umstieg ganz neu erzeugt; die Kartenvorschau des Endes ist eine Draufsicht auf die Inseln.
