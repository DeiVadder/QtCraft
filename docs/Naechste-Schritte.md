# QtCraft – nächste Schritte (Sammelliste)

Stand: 2026-10-09. **Beauftragt als Etappen 20 bis 30** in `QtCraft-Agent-Auftrag.md` (Abschnitt 4; inzwischen bis Etappe 41, Nummern seit 2026-10-13 nach E-350); diese Datei enthält die ausführlichen Beschreibungen, Entscheidungen und Begründungen dazu. Die Reihenfolge und Nummern stehen im Abschnitt „C. Reihenfolge und Etappen-Zuordnung". Ob und wann eine Etappe startet, entscheidet der Mensch (siehe `docs/FORTSCHRITT.md`).

## A. Feinschliff und offene Kleinigkeiten

### A1. Vom Menschen beobachtet

Hinweis: Etappen 15 und 18 haben teilweise schon Lesbarkeit, Dunkelschema und Tasten angefasst. Der Agent prüft vor jeder Korrektur, ob der Mangel noch besteht, und belegt es mit einem Screenshot.

| Thema | Beobachtung | Zu prüfen / Ziel |
|---|---|---|
| Abbau-Animation | Der Arm schwingt von unten nach oben statt von oben nach unten. | Schwungrichtung und Kurve korrigieren (Ich-Perspektive und Dritte Person), Screenshot-Serie als Beleg. |
| Himmel | Sieht nicht schön aus. Laut Renderer-Code ist der Himmel nur die Clear-Farbe (`m_skyColor`), es gibt keine Sonne, keinen Mond, keine Sterne, keine Wolken. | Himmelskuppel mit Farbverlauf (Horizont/Zenit), sichtbare Sonne und Mond mit Phasen, Sterne bei Nacht, Wolken (einfach, bewegt), sanfter Übergang bei Sonnenauf-/-untergang. Nebelfarbe weiter passend zum Horizont. |
| Arm-Textur | Die Textur des Arms ist hässlich. | Hand/Arm in der Ich-Perspektive neu texturieren (Hautton passend zur Charakter-Anpassung, Ärmel, Schattierung), Beleuchtung nach Umgebungslicht. |
| UI-Lesbarkeit | Je nach Hintergrund (helle/dunkle Welt, Schnee, Wasser) ist die UI nicht oder schwer lesbar. | Kontrast sicherstellen: halbtransparente Hintergründe hinter HUD-Texten, Schatten/Outline, Mindestkontrast prüfen (z. B. per Screenshot auf hellem Schnee, Wüste, Nacht, Wasser), ggf. Einstellung für UI-Deckkraft/-Größe. |
| Hunger | Der Hunger sinkt zu schnell. | Verbrauchsraten prüfen und senken (Laufen, Sprinten, Springen, Abbauen, Kampf, Regeneration), Werte als Konstanten/Daten, dokumentieren und per Test festnageln. |
| Handbuch | Das Handbuch zeigt weiße Schrift auf weißem Hintergrund (unlesbar). | Zusammen mit der UI-Lesbarkeit beheben: Farben aus der Theme-Palette, Mindestkontrast, Screenshot jeder Handbuchseite auf dem Hintergrund, auf dem sie erscheint. Möglichst eine zentrale Prüfung (Kontrasttest für alle UI-Bildschirme), damit das Problem nicht in einzelnen Bildschirmen wiederkehrt. |
| Crafting: kein Feedback beim Aufnehmen | Nimmt man ein Item auf, um es umzusetzen, gibt es keine sichtbare Rückmeldung. | Aufgenommenen Stapel am Mauszeiger anzeigen (Item-Icon mit Anzahl, folgt dem Cursor), Quellfeld optisch abdunkeln oder leeren, beim Ablegen/Verwerfen sauber entfernen. Gilt für alle Inventar-, Crafting-, Truhen- und Ofen-Bildschirme einheitlich. |
| Maus nach dem Schließen | Nach dem Schließen des Crafting-Fensters ist die Maussteuerung (Umschauen) deaktiviert. Erst ein Linksklick bringt sie zurück. | Mausfang beim Schließen jedes Overlays (Crafting, Inventar, Truhe, Ofen, Pause, Handbuch, Einstellungen, Chat) automatisch wiederherstellen, ohne dass ein Klick nötig ist und ohne dass der Klick eine Aktion auslöst (kein versehentliches Abbauen/Setzen). Tests: Szenario „Overlay öffnen → schließen → Blick ändert sich sofort" für jedes Overlay, soweit headless prüfbar; sonst Screenshot-/Eingabe-Hook. |

### A2. Aus den „Bekannten Mängeln" der Doku (Auswahl, noch nicht bewertet)

Aus `docs/FORTSCHRITT.md`. Der Mensch entscheidet, was davon in einen Auftrag kommt:

- Chat-Antworten des Servers nur Englisch (keine übersetzbaren Meldungsschlüssel im Protokoll).
- Mobs: Wegfindung nur direkt auf den Spieler zu, sie meiden keine Abgründe oder Wasser; kein Mob-Mob-Ausweichen.
- Keine Sichtlinien-Prüfung beim Abbauen/Setzen/Angreifen; keine Ratenbegrenzung für UseItem/Chat (relevant für Multiplayer, siehe B1).
- Spawn kann auf Baumkronen liegen.
- Biome bilden eher rundliche Flecken, Gebirge sind selten.
- Spielerdateien: „Bob"/„bob" teilen sich eine Datei auf macOS; keine Sperre gegen zwei Prozesse auf derselben Welt (relevant für Multiplayer).
- Treppen ohne Innen-/Außenecken; Zauntore verbinden Zäune auf allen Seiten; nur Eichentür/-falltür; nur Einzeltruhen.
- Gehaltene Fackel und leuchtende Items erhellen die Umgebung nicht (kein bewegtes Licht).
- Treppen und Stufen lassen kein Himmelslicht durch.
- Flüssigkeiten fließen ohne Vorzug zum nächsten Abgrund; Zuckerrohr wächst nicht.
- Kein Umschalt-Klick (Shift) zum schnellen Verschieben in UI-Bildschirmen; UI-Klicks nur für Buttons.
- Klänge: nur per Rechnung geprüft, nicht angehört; keine Verdeckung, kein Doppler, kein Hall.
- Speicherverbrauch durch viele kurze Übersetzungseinträge (bis ca. 870 MB beim Laden, nur durch einen Mod an den Grenzen des Mod-Laders, nicht durch das Grundspiel; seit Etappe 43 nach Anzahl begrenzt, E-371).
- Steuerschnittstelle nur headless, keine Anbindung an das grafische Spiel, keine Screenshots darüber.
- Mods: nur komplette Ersetzung von Definitionen, global statt pro Welt, höchstens 64 Mods.

**Vorschlag zur Einteilung (zu bestätigen):** Alle A2-Punkte in einem Auftrag wäre zu viel; einige sind eigene größere Etappen. Daher drei Gruppen:

- **Jetzt (Feinschliff-Etappe, klein bis mittel):**
  - Gleichmäßiger Server-Takt mit Sollzeitpunkten und begrenztem Aufholen statt eines einfachen `QTimer` (vom Menschen am 2026-10-09 ergänzt; Details in `QtCraft-Agent-Auftrag.md`, Punkt 20).
  - Alle Punkte aus A1 (Animation, Himmel, Arm, UI-Lesbarkeit inklusive Handbuch, Hunger, Item-Feedback beim Aufnehmen, Mausfang nach dem Schließen von Overlays).
  - Multiplayer-Voraussetzungen: Ratenbegrenzung und Sichtlinien-/Reichweitenprüfung auf dem Server, Spielerdateien-Sperre und eindeutige Spielerdateinamen, übersetzbare Chat-Meldungen (Meldungsschlüssel im Protokoll).
  - Kleinigkeiten mit geringem Aufwand: Spawn nicht auf Baumkronen, Shift-Klick zum schnellen Verschieben in UI-Bildschirmen, UI-Klicks auch für Inventarfelder, Zuckerrohr-Wachstum, Flüssigkeiten bevorzugen den Weg zum nächsten Abgrund, Treppen/Stufen lassen Himmelslicht durch.
  - Biomverteilung: Gebirge häufiger, Biome weniger „rundlich" (Klimaverteilung anpassen, Determinismus der Generierung dabei bewusst ändern und dokumentieren).
- **Später (eigene Etappe oder bei passender Gelegenheit):**
  - Mob-Wegfindung (Abgründe/Wasser meiden, Mob-Mob-Ausweichen). Wird für Dorfbewohner ohnehin nötig und gehört in B4 (heute Teil-Etappe 37.1).
  - Bewegtes Licht (gehaltene Fackel, leuchtende Items, Glühwürmchen), Treppen mit Innen-/Außenecken, weitere Türen/Falltüren-Holzarten, Doppeltruhen, Zauntor-Verbindungslogik.
  - Speicherverbrauch der Übersetzungsdateien (Etappe 43, Grenzfall der Mods begrenzt, E-371), Steuerschnittstelle am grafischen Spiel (lokaler Socket, Screenshots), Mods: Teil-Patches, pro Welt (Vorschlag „Mods global statt pro Welt“, Abschnitt F), mehr als 64.
  - Disconnect-Gründe (`QcMsg::Disconnect`) noch nicht übersetzbar (nur der Chat hat Meldungsschlüssel, E-094).
  - Showcase-Welt: Hinzufügen eines Mods verschiebt das Layout unveränderter Chunks einer bestehenden Showcase-Welt (Vorschlag „feste Plätze pro Name“, Abschnitt F); Wesen in den Gehegen jagen den Spieler weiter durch das Glas (kein Schaden; gemessen unkritisch, 6 Wesen, Tick 0,23 ms).
  - Nahkampf der Mobs ohne Sichtlinie: Ein Zombie trifft den Spieler über eine Kante oder Stufe (bis 2 m Höhenunterschied), während Angriffe des Spielers seit E-092 eine Sichtlinie brauchen. Mob-Angriffe ebenfalls über `QcLineOfSight` prüfen (gefunden bei der Stabilisierung von `test_survival_scenario`, Etappe 20; heute Teil-Etappe 37.1).
- **Backlog / nur auf Wunsch:** Klang-Verdeckung, Doppler und Hall; Klänge von einem Menschen anhören und nachbessern.

## B. Neue Features

Für jedes Feature: Ziel, Abhängigkeiten, offene Fragen. Die Aufwandseinschätzung ist grob und dient nur der Reihenfolge.

### B0. Showcase-Welt (früh)
- **Ziel:** Eine eigene Welt (Weltvorlage „Showcase"), in der **alle Blöcke und Features auf einem Fleck** zu sehen und auszuprobieren sind. Sie dient dem Menschen zum schnellen Durchsehen und dem Agenten und den Tests als reproduzierbare Szene.
- **Aufbau:** Flache, ebene Fläche ohne Zufallsgelände, feste Tageszeit (einstellbar), kein Mob-Spawning von selbst, Creative-artiges Spielen (Fliegen, unbegrenzte Blöcke, sobald B3 da ist; bis dahin Admin-Befehle `/give`). Bereiche nebeneinander mit klarer Beschriftung:
  - **Blöcke nach Kategorien** (Gestein, Erde, Holz/Blätter, Erze, Glas, Flüssigkeiten, Pflanzen, Bauteile), jeweils **automatisch aus der Registry erzeugt**: Jeder Blocktyp und jeder Zustand (Treppe, Slab, Zaun in allen Ausrichtungen, Tür offen und zu usw.) steht auf einem Podest. Neue Blöcke, auch aus Mods, erscheinen ohne Handarbeit.
  - **Features als gebaute Demos** (aus Struktur-Daten, damit erweiterbar): Fackel-/Lampen-Reihe zum Prüfen des Blocklichts, Wasser-/Lava-Becken mit Fließen, Obsidian-Demo, fallende Blöcke, Bäume und Setzlinge, Ofen und Truhe, Crafting-Station, Charaktermodell-Ständer mit Animationen, später Felder (Landwirtschaft), Signal-Dust-Schaltungen, Dorfhäuser.
  - **Mobs/Wesen** in Gehegen (Zombie, Schwein, Schleim, Glühwürmchen und alles Spätere), Item-Beispiele in Truhen oder auf Podesten.
  - **Beschriftungen** über Namen aus der Registry (übersetzbar), als schwebender Text oder Schild-artiges Anzeigeelement; bei Bedarf blendbar.
- **Nutzen:** (1) schneller visueller Überblick und Abnahme jedes Features, (2) **Screenshot-Rundgang** mit festen Kamerapositionen als reproduzierbare Verifikation (und als Regression gegen unbeabsichtigte optische Änderungen, mit Toleranz), (3) feste Szene für den **Performance-Check pro Etappe**, (4) Vergleich von Texturpaketen (B2), (5) Testbett für Mods und Multiplayer (alle Clients sehen dasselbe).
- **Regel für alle späteren Etappen:** Jede neue Funktion/jeder neue Block erweitert die Showcase-Welt (Definition of Done). Der Agent trägt das in die Abnahme jeder Etappe ein.
- **Technik:** Eigener Weltgenerator-Typ aus Daten (Flach + Struktur-Plan), Plan wird beim Anlegen erzeugt, deterministisch; Menüeintrag „Showcase-Welt anlegen" und Startoption; der Struktur-Plan verwendet dieselbe Struktur-Mechanik, die später für Dörfer (B4) gebraucht wird, und wird dafür vorbereitet.
- **Aufwand:** klein bis mittel, hoher Nutzen. Wegen des Nutzens als **erste neue Etappe nach dem Feinschliff** vorgesehen.

### B1. Multiplayer
- **Ziel:** Mehrere Spieler über das Netzwerk (TCP) auf einem Server. Der Protokollrahmen ist schon vorbereitet (autoritativer Server, In-Process-Kanal und austauschbarer Transport). Host/Join-Menü, dedizierter Server ohne GUI (headless), Spielerliste, Chat, Spieler sehen sich mit Charaktermodell und Animationen.
- **Abhängigkeiten:** Sicherheits-Minimum (Ratenbegrenzung, Sichtlinien-/Reichweiten-Prüfung auf dem Server, Spielerdateien-Sperre, eindeutige Spielerkennung), Mod-Abgleich zwischen Server und Client (gleiche Mods/Versionen), Synchronisierung von Blockzuständen, Licht, Entities, Flüssigkeiten, Tag/Nacht.
- **Entschieden (2026-10-09):**
  - **Reichweite:** LAN **und** Internet (direkte Verbindung per Adresse/Port; Port-Freigabe/NAT-Durchquerung ist Sache des Hosters, keine Relay-/Vermittlungsserver).
  - **Authentifizierung auf das Minimum reduziert** (Hobby-/Feature-Proof-Projekt, nicht kommerziell): Spielername plus optionales Server-Passwort. Keine Konten, keine externen Login-Dienste. Ein stabiler, vom Client erzeugter zufälliger Spieler-Schlüssel (pro Installation) hält die Spieleridentität stabil, damit gleiche oder ähnliche Namen keine Spielerdatei teilen. Der Schutz vor Missbrauch beschränkt sich auf serverseitige Validierung, Ratenbegrenzung und Größenlimits (Absicherung gegen fehlerhafte Eingaben, nicht gegen gezielte Angriffe). Diese Grenzen werden in `docs/Multiplayer.md` ehrlich festgehalten (keine Verschlüsselung zugesichert).
  - **Maximale Spielerzahl:** Beim Server einstellbar (Konfigurationsdatei/Startoption, Standardwert z. B. 8), damit Nutzer sie an ihre Hardware anpassen. Weitere Server-Einstellungen im selben Zug: Obergrenze der Sichtweite, Port, Passwort, Welt, Standard-Spielmodus.
- **Entschieden (2026-10-09): Admin-Befehle und Wartungswerkzeuge** (Operator-Rechte über eine Operator-Liste in der Serverkonfiguration; Befehle über Server-Konsole und Chat). **Sie sind auch im Einzelspieler verfügbar**, weil der Einzelspieler-Server dieselbe Befehlsschicht nutzt. Sie dienen dort als Notfallwerkzeuge (festhängender Spieler, kaputter Chunk, überlaufende Entities, Welt-Reparatur). Im Einzelspieler ist der lokale Spieler automatisch Operator; für Spielmodi mit Fortschritt bleibt die Nutzung bewusst (kein versehentliches Auslösen: Bestätigung bei zerstörenden Befehlen).
  - Spielerverwaltung: Kick, Ban/Unban, Whitelist, Operator vergeben/entziehen, Spielerliste.
  - Aufräumen und Reparieren: **Chunk neu generieren** (einzeln oder Bereich, mit Sicherheitsabfrage; verwirft die Änderungen im Chunk), **Entities/Mobs löschen** (nach Typ, Radius, alle; Items eingeschlossen), Item-Entities aufräumen, Blockbereiche füllen/ersetzen/löschen mit Obergrenze, Welt sofort speichern, Backup der Welt.
  - Steuerung: Teleport, Zeit/Tageszeit setzen, Spielmodus setzen, Mob-Spawning an/aus, Weltinfos (geladene Chunks, Entity-Zahl, Tick-Zeit, Speicher).
  - Alle Befehle sind aus Daten/Registry erweiterbar (Mods können Befehle ergänzen), protokolliert und mit Schutz gegen versehentliche große Eingriffe (Bestätigung, Größenlimit).
- **Offene Fragen:** Autoritäts-Modell für Bewegung (Client-Vorhersage + Server-Korrektur)? Mobs/KI-Last mit mehreren Spielern?
- **Risiko:** hoch (Latenz, Desync, Sicherheit). Aufwand: groß.

### B2. HD-Texturpaket
- **Ziel:** Optionales Texturpaket mit höherer Auflösung (z. B. 32×32, 64×64 oder 128×128 pro Block statt 16×16), das sich in den Einstellungen wählen lässt. Gilt für Blöcke, Items, Mobs, Charaktermodell, UI.
- **Technik:** Texturpaket-Mechanismus (Resource-Pack-Ordner mit Manifest, Reihenfolge, Fallback auf die Basistextur je fehlender Datei), Atlas mit variabler Kachelgröße (Obergrenze der GPU-Texturgröße beachten, Mipmaps, Anti-Aliasing an Kachelrändern), Speicherbedarf und Ladezeit messen.
- **Entschieden (2026-10-09):** Freie Quellen sind für diese Etappe **erlaubt**, selbst erstellte Texturen ebenfalls. Es gelten wieder die Lizenz- und Quellenregeln aus dem ursprünglichen Auftrag (nur CC0/Public Domain oder freizügige Lizenzen wie CC-BY, keine Minecraft-Texturen oder daraus abgeleitete Packs, nur Bilddateien, kein Ausführen heruntergeladener Inhalte, Nachweis in `docs/ASSETS.md` mit Quelle, Urheber, Lizenz, Datum, Namensnennung in `CREDITS.md`). Die Entscheidung ersetzt die Festlegung „keine Downloads" von 2026-10-07 nur für Texturen dieser Etappe; sie muss in `docs/FORTSCHRITT.md` (Festlegungen) vermerkt werden.
- **Auflösung (Recherchestand 2026-10-09):**
  - Der Basisstandard von Minecraft ist **16×16** pro Block. Offizielle HD-Texturpakete von Mojang gibt es laut Recherche nicht; Pakete mit höherer Auflösung stammen aus der Community und liegen üblicherweise bei **32×, 64×, 128×, 256×, 512×** (jeweils Zweierpotenzen).
  - Mojangs „Vibrant Visuals" ist ein eigener Grafikmodus (PBR, Beleuchtung/Nebel/Reflexionen), **keine** Texturauflösung. Er kann später als eigene Idee geprüft werden (PBR-Texturen als zusätzliche Kanäle) und gehört nicht in diese Etappe.
  - Vorschlag: Der Mechanismus unterstützt **jede Zweierpotenz von 16× bis 512×** (beschränkt durch GPU-Texturgröße und Speicher, mit begrenzter Obergrenze und sauberer Fehlermeldung). Als mitgelieferte Stufen werden **32× und 64×** angestrebt, **128×** optional. Höhere Stufen (256×/512×) bleiben nutzbar, werden aber nicht mitgeliefert.
  - Der Agent darf vor der Umsetzung **kurz recherchieren** (freie, kompatibel lizenzierte Pakete und Konventionen für die Pack-Struktur), ohne etwas zu installieren.
- **Offene Fragen:**
  - Wird der Mechanismus zugleich für Mod-Texturen genutzt?
  - Stilbruch vermeiden: Passen fremde Texturen stilistisch zu den vorhandenen Blöcken, Mobs und dem Charaktermodell? Ein einheitliches Paket ist besser als eine Mischung aus Quellen.

### B3. Creative-Modus
- **Ziel:** Spielmodus ohne Gesundheit/Hunger-Zwang, Fliegen (Doppel-Sprung), unbegrenzte Blöcke aus einem Creative-Inventar (alle Blöcke/Items, Suche, Kategorien), sofortiges Abbauen, Blockwahl per Mittelklick, optional unverwundbar. Moduswahl bei Weltanlage und per Befehl (`/gamemode`).
- **Abhängigkeiten:** Spielmodus als Teil des Server-Zustands (Multiplayer-tauglich), Registry liefert alle Einträge inkl. Mods; Creative-Inventar-UI.
- **Offene Fragen:** Auch Adventure/Spectator? Nutzung in Welten, die später auf Survival wechseln (Truhen-Inhalte, Fortschritt)?
- **Aufwand:** klein bis mittel.

### B4. Dörfer und Dorfbewohner
- **Ziel:** Dörfer als Generator-Strukturen (Häuser, Wege, Brunnen, Felder, Beleuchtung), aus Daten definierbar (Mods können Bauteile/Strukturen ergänzen). Dorfbewohner (NPC) mit Berufen (Landwirt, Schmied, Bibliothekar u. a.), Tagesablauf (arbeiten, schlafen), Handel (Smaragd/Währungs-Item gegen Waren), eigene Modelle und Sounds.
- **Abhängigkeiten:** Strukturgenerator mit Platzierungsregeln (Biome, Gelände-Anpassung), Betten (fehlt bisher), Türen, Pfadfindung (bisher einfach, muss für NPC besser werden), Farming (B8), Handels-UI, Erkundung/Speichern von Strukturen über Chunkgrenzen.
- **Entschieden (2026-10-09):** **Keine Raids** und keine Plünderer. Umfang: **automatisch generierte Dörfer** mit Häusern (mehrere Varianten), **Kirche**, **Farmen** und **Dorfbewohnern**, die **handeln** (Trading-UI mit Angebotsliste, Preis in einem Währungs-Item). Zombie-Dörfer entfallen. Strukturen sind aus Daten definiert (Mods können Häuser, Berufe und Angebote ergänzen).
- **Dem Agenten überlassen (2026-10-09):** Berufsvielfalt am Anfang (Vorschlag: Landwirt, Schmied, Bibliothekar/Priester; weitere per Daten nachrüstbar), Wegfindung im Dorf (ggf. als eigene Teiletappe, siehe A2; heute Teil-Etappe 37.1, Wegfindung allgemein für alle Mobs) und Handelsmodell (feste Angebote oder Preisanpassung nach Nachfrage). Der Agent entscheidet und dokumentiert in `docs/Entscheidungen.md`.
- **Aufwand:** groß (Struktur + NPC-KI + Handel).
- **Baumhaus-Dorf im Dschungel (geprüft in 37.2a, E-403; Vorschlag, nicht beauftragt):** Heute nicht machbar mit vertretbarem Aufwand: Es gibt weder ein Dschungel-Biom noch große Dschungelbäume (Stämme 2 × 2, Lianen), und die Wegfindung (E-400 bis E-402) klettert keine Leitern und kennt keine Stege in der Höhe. Vorschlag in drei Schritten: (1) Biom `jungle` mit großen Bäumen (neue Baumform in `QcTreeShape`, Lianen als Block); (2) ein Dorf-Stil mit eigener Platzierung „an Bäumen“: statt Häusern am Boden entlang von Wegen planen `QcVillagePlanner`-Varianten Plattformen in Kronenhöhe an den Stämmen (die Bäume selbst stammen dann aus dem Plan des Dorfs, nicht aus den Merkmalen des Bioms, damit sie über Chunkgrenzen gleich herauskommen), Stege als Wege in der Höhe und Leitern an den Stämmen; (3) eine Zugart „Leiter hoch/runter“ in der Wegfindung. Das Design von 37.2a lässt das ohne Umbau zu: der Dorf-Zustand kennt Mittelpunkt, Gebiet (mit Höhe), Betten und Arbeitsplätze in drei Dimensionen, ein Stil ist Daten (`villages/*.json`), und der Planer ist eine eigene Klasse, die eine zweite Platzierungsart (`"layout": "treetops"`) bekommen kann.

### B5. Signal Dust (Schaltungen)
- **Entschieden (2026-10-09):** Umfang = der **erste Redstone-Release** (der frühe Alpha-/Beta-Stand der Schaltmechanik) **plus Trichter und Komparator**. Konkret: Signalleitung (Staub), Fackel als Inverter, Hebel, Knopf, Druckplatte, Repeater, Kolben (normal und klebrig), Lampe sowie schaltbare Türen, Falltüren und Zauntore, soweit sie in diesem frühen Stand schon schaltbar waren. Dazu **Trichter** (Items bewegen, Behälter) und **Komparator** (Signalstärke aus Behältern/Brennofen messen und vergleichen). **Erweitert (2026-10-12):** auch **Beobachter, Spender/Werfer und Schienen mit Loren** (normale, angetriebene, Detektor- und Aktivierungsschienen); Anlass: automatische Farmen (Saat pflanzt sich auf Ackerboden selbst, Etappe 27; Spender düngen und setzen Wasser; Saat werfen Spender und Werfer nur aus, gepflanzt wird ausschließlich über das Selbstpflanzen gedroppter Saat, bewusst so entschieden). Geteilt in 29.1 und 29.2 (abgeschlossen); Schienen und Loren (früher Teil 29.3) sind heute Etappe 32 (E-350). **Kolben schieben auch Kisten und andere Entities (vom Menschen 2026-10-12 gewünscht):** Blöcke mit Inhalt (Truhen, Öfen, Trichter, Spender/Werfer, Fässer) werden samt Inhalt geschoben und gezogen (wie in der Bedrock-Edition, anders als in Java), ohne Item-Verlust oder Duplikation, auch über Chunk-Grenzen und beim Speichern mitten in der Bewegung; Wesen, Spieler, Items, Rüstungsständer und Loren werden vom ausfahrenden Kolbenkopf und vom geschobenen Block mitgeschoben (Server-autoritativ, ohne Vorhersage-Korrekturen beim Client). Unbewegliche Blöcke (Grundgestein, Obsidian u. a.) bleiben in Daten festgelegt. **BUD/Quasi-Konnektivität abschaltbar (vom Menschen 2026-10-12 gewünscht, Ideen für später):** Die Eigenheit wird nicht fest verdrahtet, sondern als eigene, klar abgegrenzte Regel umgesetzt, die sich abschalten lässt – als Spielregel pro Welt (z. B. `quasiConnectivity`, Standard an) und zusätzlich pro Block in den Daten (Kolben, Spender/Werfer usw. können sie einzeln an- oder abwählen, Mods ebenso); beide Zustände sind getestet und dokumentiert, damit später weitere Varianten (z. B. eigene Blöcke ohne BUD) leicht dazukommen.
- **Ziel:** Signalstärke 0–15, Updates mit Tick-Reihenfolge und Verzögerungen, deterministisch.
- **Entschieden (2026-10-12): Performance und bekannte Eigenheiten.** Die Signalleitung wird **performanter als im Vorbild** umgesetzt, vor allem beim Aktualisieren langer Staublinien: keine kaskadierenden Einzel-Updates je Staubblock, sondern eine Neuberechnung des betroffenen Leitungsnetzes in einem Durchgang (z. B. Breitensuche von den Quellen aus, Stärke fällt je Block um 1), die nur die Blöcke meldet, deren Stärke sich wirklich geändert hat. Das Ergebnis (Signalstärken, wer wann schaltet) ist deterministisch und hängt nicht von der Lage in der Welt ab. **Die bekannten Eigenheiten, auf denen Spieler-Schaltungen beruhen, bleiben erhalten** und bekommen je einen Test und eine Showcase-Demo: **BUD / Quasi-Konnektivität** (Kolben werden auch von Signal an dem Block über ihnen versorgt, schalten aber erst beim nächsten Block-Update in der Nähe; ein so „indirekt versorgter“ Kolben wartet auf ein Update), **0-Tick-Kolben** (ein Puls von 0 Ticks fährt den Kolben sofort aus und ein; Klebkolben lassen bei zu kurzem Puls ihren Block los, „Block spitting“), das Durchbrennen der Signal Torch bei zu schnellem Takt und die feste Verzögerung der Repeater-Stufen. Welche weiteren Eigenheiten erhalten bleiben, recherchiert und entscheidet der Agent beim Start der Etappe; was bewusst wegfällt, wird als Entscheidung dokumentiert. **Entschieden (2026-10-12): weder lage- noch richtungsabhängig.** Eine Schaltung verhält sich überall in der Welt und in jeder Drehung gleich (anders als im Vorbild, wo die Hash-Reihenfolge der Updates und die feste Himmelsrichtungs-Reihenfolge der Nachbar-Updates das Ergebnis ändern). Verbraucher reagieren in der Reihenfolge, in der das Signal sie erreicht (Abstand zur Quelle, Verzögerungen); was im selben Schritt gleichzeitig geschaltet wird, wirkt gleichzeitig. Konflikte (z. B. zwei Kolben wollen im selben Tick in denselben Block) löst eine drehungsunabhängige Regel, die der Agent festlegt, testet (dieselbe Schaltung in allen vier Drehungen und an verschiedenen Koordinaten) und dokumentiert.
- **Abhängigkeiten:** Block-Zustandssystem (vorhanden), Block-Entity-Ticks, Nachbar-Update-Kaskade mit Begrenzung, Kolben brauchen bewegte Blöcke (neue Entity-Art), Netzwerk-Synchronisierung (B1).
- **Offene Fragen (Agent entscheidet):** Performance-Budget für große Schaltungen (Obergrenze der Signal-Updates pro Tick)? Die exakte Liste des „ersten Releases" beim Start der Etappe kurz recherchieren und festschreiben.
- **Entschieden (2026-10-09): Name = „Signal Dust" (deutsch „Signalstaub")**, abgeleitet von Qts Signal-/Slot-Mechanismus. Die Familie: Signal Dust (Leitung), Signal Torch (Inverter), Signal Repeater, Signal Lamp, Signal Piston/Sticky Piston, Signal Hopper, Signal Comparator; Verbraucher heißen „Slots" in der Doku. Alles englisch als Basis mit deutscher Übersetzung. Nicht „Redstone" verwenden.
- **Verworfene Namensvorschläge** (zur Dokumentation, nicht umsetzen):
  1. **Quartz Dust / Quarzstaub** (Q wie Qt): einprägsam, aber „Quarz" ist bereits Minecraft-Inhalt (Nether-Quarz).
  2. **Moc Dust / Mocstaub**: Augenzwinkern zum Meta-Object-Compiler von Qt, für Insider.
  3. **Cutestone / Kutstein**: Qt wird „cute" ausgesprochen; Name klingt nach Stein, ist aber niedlich. Hat Wiedererkennung, leicht scherzhaft.
  4. **Spark Dust / Funkenstaub**: neutral, falls keine Qt-Anspielung gewünscht.
- **Risiko:** mittel (Determinismus, Update-Reihenfolge, Lag-Maschinen). Aufwand: groß.

### B6. Landwirtschaft, Nahrung, Kürbisse und Melonen
- **Ziel:** Ackerboden (Hacke), Saat und Wachstumsstufen (Weizen, Karotten, Kartoffeln), Bewässerung (Wasser in Reichweite), Ernte und Drops, Brot und weitere Nahrung (gebackene Kartoffel, gekochtes Fleisch über Ofen), Kürbis und Melone mit Stängel-Wachstum, Melonenscheiben, Kürbiskuchen/Suppe, Tierzucht (Füttern, Nachwuchs) optional.
- **Abhängigkeiten:** Hunger-Balancing (A1), Ofen (vorhanden), Block-Wachstumsticks (wie Setzlinge), Teilformen/Zustände (Wachstumsstufe), Dorfbewohner-Landwirte (B4).
- **Offene Fragen:** Tierzucht in diesem Block oder separat? Vegetation-Wachstum nur in geladenen Chunks (Zeit nachholen beim Laden)?
- **Aufwand:** mittel.

### B7. Charakter-Vorschau und Rüstung
Wichtig für Multiplayer: Spieler sehen sich gegenseitig, also muss die Anpassung gut bedienbar sein und die Ausrüstung sichtbar werden. Zwei Teile, die zusammen oder als zwei Etappen laufen können (Reihenfolge siehe C).

**B7a. Live-Vorschau des Charakters im UI**
- **Ziel:** Ein **Mini-Live-Render** des Charaktermodells als UI-Element (drehbar per Maus, optional zoombar, Idle-Animation, blickt dem Mauszeiger nach). Es erscheint:
  - im **Anpassungsbildschirm**: Jede Auswahl (Körperfarbe, Haare, Kleidung, Skin-Varianten) ist sofort in der Vorschau sichtbar, dazu Zufall und Zurücksetzen, Presets und Speichern; Wechsel zwischen Ansichten (Vorder-/Rückseite) und Animationen (Stehen, Gehen, Abbauen) zur Prüfung;
  - im **Inventar** neben den Rüstungsslots (zeigt Ausrüstung live);
  - im Multiplayer-Beitrittsbildschirm (eigenes Profil) und später in der Spielerliste.
- **Technik:** Eigener Renderpfad für das UI-Element (Offscreen-Rendertarget oder zweites QRhi-Item) mit denselben Meshes, Texturen und Animationen wie im Spiel, damit die Vorschau **genau das zeigt, was andere Spieler sehen**. Gerendert wird nur, solange das UI-Element sichtbar ist, mit begrenzter Bildrate, damit die Vorschau im Spiel keine Dauerlast erzeugt (im Performance-Check pro Etappe messen). Das Profil wird vor der Übernahme serverseitig validiert (Größenlimits, nur bekannte Registry-Einträge).
- **Tests und Belege:** Screenshot der Vorschau je Auswahlmöglichkeit, Vergleich Vorschau gegen In-Game-Darstellung (gleiche Pose, gleiches Profil), Persistenz des Profils (Round-Trip).

**B7b. Rüstung (Items, Slots, Darstellung)**
- **Ziel:** Rüstung als Items mit **vier Ausrüstungsslots** im Inventar (Kopf, Brust, Beine, Füße) neben der Charaktervorschau (B7a). Rüstungsanzeige im HUD (Leiste mit Rüstungspunkten).
- **Spielmechanik:** Rüstungswert und Haltbarkeit pro Item aus Daten (mehrere Materialstufen passend zu den vorhandenen Werkzeugstufen, z. B. Eisen, Gold, Diamant; ob eine Stufe unterhalb Eisen sinnvoll ist, entscheidet der Agent). Schadensreduktion **serverseitig**, Haltbarkeitsverlust beim Treffer, Rüstung kann brechen, Reparatur über Crafting-Kombination (der Agent legt das einfachste sinnvolle Verfahren fest). Rezepte im normalen Handwerksraster aus den Barren (Helm, Brustplatte, Beinschutz, Stiefel). Beim Tod droppt die Rüstung wie das Inventar (Regel `keepInventory` gilt).
- **Bedienung:** Anlegen per Ziehen in den Slot, per Shift-Klick (siehe A2) und per Rechtsklick vom Hotbar-Item. Eine Rüstungsdarstellung ohne Slot-Feedback (Tooltip mit Wert und Haltbarkeit) ist nicht ausreichend.
- **Darstellung:** Rüstung wird als **Schichten am Charaktermodell** gerendert (je Körperteil eigene Textur/Modell aus Daten, damit Mods Rüstungen ergänzen können), sichtbar in dritter Person, in der Vorschau und **für andere Spieler** (Ausrüstungszustand im Protokoll synchronisiert). Eigene Darstellung der Rüstung in der Ich-Perspektive entfällt (Arm/Hand bleiben wie bisher).
- **Abhängigkeiten:** Charaktermodell und Animation (vorhanden), Crafting und Ofen (vorhanden), Inventar-UI, Protokoll für Ausrüstungszustand, HD-Texturpaket später mit Rüstungstexturen.
- **Offene Fragen (Agent entscheidet):** Materialstufen und Werte, Haltbarkeitsmodell, Reparaturverfahren, ob Mobs ebenfalls Rüstung tragen können (später).
- **Showcase (B0):** Charakterständer zeigen jeden Rüstungssatz.

## C. Reihenfolge und Etappen-Zuordnung


**Zuordnung zu den Etappen im Auftrag** (Reihenfolge bestätigt; Etappe 19 „Aufteilen zu großer Dateien" läuft **vor** den neuen Features (vom Menschen vorgezogen), weil sie die großen Dateien entlastet). Seit 2026-10-13 gelten feste ganze Etappen-Nummern (E-350): Die Reihenfolge der Arbeit ist die Reihenfolge der Zeilen, nicht die der Nummern; ein späterer Einschub bekommt die nächste freie Nummer und wird hier einsortiert. Die Spalte „Früher“ nennt die alte geplante Nummer (vollständige Liste in `docs/FORTSCHRITT.md`, „Alte und neue Etappen-Nummern“).

| Etappe | Inhalt | Abschnitt hier | Früher |
|---|---|---|---|
| 20 | Feinschliff und Multiplayer-Voraussetzungen | A1, A2 „Jetzt" | |
| 21 | Showcase-Welt | B0 | |
| 22 | Charakter-Vorschau im UI | B7a | |
| 23 | Rüstung | B7b | |
| 24 | Server-Befehle und Wartungswerkzeuge (auch im Einzelspieler) | B1 (Admin-Befehle) | |
| 25 | Multiplayer | B1 | |
| 26 | Creative-Modus | B3 | |
| 27 | Landwirtschaft, Nahrung, Kürbisse und Melonen | B6 | |
| 28 | HD-Texturpaket | B2 | |
| 29 | Signal Dust (Schaltungen), 29.1 und 29.2 (abgeschlossen) | B5 | |
| 30 | Fehler im Benchmark `crowd` (und fehlende Saat in `farm-seeds`) (abgeschlossen) | Auftrag Punkt 30 | neu |
| 31.1, 31.2 | Umbau: Block-Entities, Entities (abgeschlossen) | Auftrag Punkt 31 | neu |
| 43 | Speicher der Übersetzungen (seit 2026-10-14 beauftragt, vor 31.3; Grenze für Übersetzungen statt Laufzeit-Namen, E-371) | Auftrag Punkt 43, F | neu |
| 31.3 | Chunks auf dem Server entladen (inkl. Grenzwerte in `QcServerLimits` abschließen, `static_cast`-Downcasts entscheiden) | Auftrag Punkt 31 | 32 |
| 32 | Schienen und Loren | B5, Auftrag Punkt 32 | 29.3 |
| 33 | Licht auf dem Server | Auftrag Punkt 33 | neu |
| 34 | Feuer | Auftrag Punkt 34 | 29b |
| 35 | Wetter | Auftrag Punkt 35 | 29c |
| 36 | Erfahrung und Lapislazuli | Auftrag Punkt 36 | 29d |
| 37 | Wegfindung (37.1), Dörfer und Dorfbewohner (37.2) | B4, A2 „Später“ | 30 |
| 38 | Monster-Spawner und Minenschächte | Auftrag Punkt 38 | 30b |
| 46 | Client nach Zuständigkeit gliedern (46.1), Session-Tick auf eigenem Thread (46.2, früher Vorschlag 44) | Auftrag Punkt 46, F | neu |
| 40 | Gamepad-Unterstützung | Auftrag Punkt 40 | 33 |
| 41 | Verzauberung (41.1) und Amboss (41.2) | Auftrag Punkt 41 | 34 |
| 47 | Feinschliff Darstellung (Beschriftungen, anisotrope Filterung, Arm, Mindesthelligkeit, Laub) | Auftrag Punkt 47 | neu |
| 45 | Realistischer Render-Modus (Wasser und Licht), 45.1–45.4 | Auftrag Punkt 45 | neu |
| 39 | Veröffentlichungs-Vorbereitung (GitHub, GPLv3), am Ende | Auftrag Punkt 39 | 31 |
| 42 | Zustandsbudget (Vorschlag) | F | neu |
| 48 | TLS-Verschlüsselung im Mehrspieler (beauftragt 2026-10-20, vor 39) | F | neu |
| 44 | stillgelegt: zusammengelegt in 46.2 | F | neu |

**Anpassung vom 2026-10-14 (Projektzeugnis, E-370):** Reihenfolge ab jetzt 43, 31.3, 32, 33, 34, 35, 36, 37 (37.1, 37.2), 38, 46 (46.1, 46.2), 40, 41, 47, 45, 39; Vorschläge 42 und 48; die Nummer 44 ist stillgelegt (nie wieder vergeben). Abhängigkeiten: 38 nach 32, 41 nach 36, 37.2 nach 36 und 37.1, 35 nach 34, 45 nach 33, 46.2 vor 40, 43 vor 31.3.

Die Aufzählung unten ist die ursprüngliche Begründung der Reihenfolge (ältere Nummerierung, maßgeblich ist die Tabelle oben).

Entscheidung des Menschen (2026-10-09): **Multiplayer kommt früh.** Daraus folgt die Reihenfolge:

1. **A1 und ausgewählte Punkte aus A2** (Feinschliff): klein, sofort sichtbar. Darin zuerst die Multiplayer-Voraussetzungen aus A2 (Ratenbegrenzung, Reichweiten-/Sichtlinienprüfung auf dem Server, Spielerdateien-Sperre, übersetzbare Chat-Meldungen).
2. **B0 Showcase-Welt:** direkt nach dem Feinschliff, noch vor dem Multiplayer: Sie ist klein und liefert die reproduzierbare Szene für Screenshot-Verifikation, Performance-Messung und Mehrspieler-Tests.
3. **B7 Charakter-Vorschau und Rüstung:** vor dem Multiplayer, weil Spieler sich dort gegenseitig sehen. Die Vorschau (B7a) zuerst; die Rüstung (B7b) darf als zweite Teiletappe folgen. Der Ausrüstungszustand wird von Anfang an im Protokoll vorgesehen, damit der Multiplayer ihn nur noch überträgt.
4. **B1 Multiplayer:** direkt danach. Alle späteren Features (Creative, Landwirtschaft, Redstone, Dörfer) werden dann von Anfang an synchron entworfen und gegen Server+Client getestet, statt später alles auf einmal nachzurüsten. Mehrspieler-Szenariotests laufen über die Steuerschnittstelle mit mehreren Bot-Clients.
5. **B3 Creative-Modus:** klein, nützlich zum Testen der übrigen Features, Spielmodus als Server-Zustand pro Spieler.
6. **B6 Landwirtschaft und Nahrung:** Voraussetzung für Hunger-Balancing und Dörfer.
7. **B2 HD-Texturpaket:** unabhängig; der Mechanismus hilft auch Mods und kann an jeder Stelle eingeschoben werden. Das Paket muss beim Multiplayer **clientseitig** sein (jeder Spieler wählt sein Paket, der Server liefert nur Namen/Definitionen).
8. **B5 Signal Dust:** großer, in sich geschlossener Block; Synchronisierung der Signalzustände wird über das vorhandene Blockzustands-Protokoll abgebildet.
9. **B4 Dörfer/Dorfbewohner:** setzt Landwirtschaft, Betten, bessere Wegfindung voraus.

## D. Gemeinsame Regeln für alle neuen Etappen

Es gelten unverändert die Regeln aus `QtCraft-Agent-Auftrag.md`: Feature-Branch je Etappe, Squash-Merge nach `dev` nach grüner unabhängiger Verifikation, kein Merge nach `main`/`master`, Unit-/statische/dynamische Tests, Performance-Check pro Etappe (Schwelle 10 %), keine Installation ohne Rückfrage, `-fno-exceptions` und `-Werror`, kein Boost, Screenshot-Beleg für sichtbare Änderungen, Übersetzbarkeit aller Texte.

Zusätzlich für die neuen Etappen:
- Jede neue Funktion bleibt **datenbasiert und per Mod erweiterbar** (Registry, Zustände, Formen).
- Jede neue Funktion berücksichtigt von Anfang an die **Server-Autorität** (kein Client-Sonderweg), damit Multiplayer (B1) nicht nachträglich alles umbauen muss.
- Abwärtskompatibilität alter Welten und alter Mods ist Pflicht (Weltformat-Version, Migrationstest).
- Jede neue Funktion erweitert die **Showcase-Welt** (B0) und ist dort per Screenshot-Rundgang abgenommen.

## E. Offene Fragen an den Menschen (gesammelt)

Entschieden (2026-10-09): HD-Quellen frei oder selbst erstellt, Auflösung nach Community-Stufen (Vorschlag 32×/64×, optional 128×), Multiplayer früh, LAN und Internet, minimale Authentifizierung, Spielerzahl einstellbar, Redstone-Umfang (erster Release plus Trichter und Komparator), Dörfer ohne Raids (Häuser, Kirche, Farmen, Dorfbewohner, Handel).

Ebenfalls entschieden: Reihenfolge aus Abschnitt C ist bestätigt; A2-Einteilung (Jetzt/Später/Backlog) bestätigt; Schaltmaterial heißt „Signal Dust"; Admin-Befehle und Wartungswerkzeuge kommen mit dem Multiplayer (B1) und gelten auch im Einzelspieler; Dorf-Handel und Berufe entscheidet der Agent.

Es sind keine Fragen mehr offen. Die Liste ist als Etappen 20 bis 30 beauftragt.

## F. Vorschläge aus dem Umbau-Auftrag (Teil 3, vom Menschen 2026-10-13)

Prüfen und entscheiden; umsetzen nur bei geringem Aufwand, sonst als Vorschlag mit eigener Nummer (ab 42) in der Etappentabelle (Abschnitt C). Ob und wann, entscheidet der Mensch. Stand 2026-10-14 (E-370): 43 ist beauftragt, 44 in 46.2 aufgegangen, 48 neu als Vorschlag; 42 und 48 sind nicht beauftragt.

- **Zustandsbudget (Vorschlag Etappe 42):** Zustands-Ids sind `uint16` (65 536 Zustände gesamt, `QcBlockStateTable::maxStatesPerBlock = 2048`). Signal Dust allein hat 1296 Zustände; Zäune, Mauern und Scheiben je Material zahlen für ihre Verbindungen ebenso. Prüfen, ob Verbindungen, die nur der Darstellung dienen (Staubseiten, Zäune, Mauern, Scheiben), beim Meshen aus den Nachbarn abgeleitet statt gespeichert werden können (Folgen für Kollision, Raycast, Speicherformat und Protokoll klären; Migration alter Welten). **Mindestens** (klein, kann in jeder Etappe mitgehen): Budget-Meldung beim Laden mit der Zahl der verbrauchten Zustände (Log, Debug-Anzeige, Handbuch-Übersicht), Warnung ab einem Schwellwert. Die Angabe „höchstens 1024 je Block“ in `docs/Architektur.md` ist auf 2048 korrigiert.
- **Speicher der Übersetzungen (Etappe 43, seit 2026-10-14 beauftragt, E-370; vor 31.3):** Ursprünglicher Befund: rund 3 Mio. Einträge belegen beim Laden 700 bis 870 MB. Namen der Teile-Familien (Bauteile je Material) zur Laufzeit aus den `%1`-Vorlagen zusammensetzen statt jede Kombination auszuschreiben; Handbuch-Suche und Creative-Suche müssen dieselben Namen finden. Messung vorher/nachher (Speicher, Ladezeit) headless. **Ergebnis der Messung (E-371):** Der Befund war ein Grenzfall für Mods, nicht das Grundspiel (2 × 1306 Einträge, 0,7 MB; davon 100 Teile-Namen); begrenzt wird die Zahl der Übersetzungen, die Teile-Namen bleiben ausgeschrieben (vom Menschen am 2026-10-14 bestätigt).
- **Client-Tick auf dem GUI-Thread (früher Vorschlag 44, seit 2026-10-14 Teil-Etappe 46.2, E-370; die Nummer 44 ist stillgelegt):** `QcClientSession` tickt mit einem 20-Hz-`QTimer` auf dem GUI-Thread; ein langer Frame verzögert damit Vorhersage und Netz. Aufwand und Nutzen eines eigenen Session-Threads abschätzen (Übergaben an QML und Renderer, Sperren, TSan); Kandidat für vor dem Gamepad (Etappe 40), weil analoge Eingabe einen gleichmäßigen Takt braucht.
- **TLS-Verschlüsselung im Mehrspieler (Vorschlag Etappe 48, aus dem Projektzeugnis 2026-10-14):** Chat und Spieldaten gehen im Klartext, die erste Anmeldung schickt die Identität im Klartext (`docs/Multiplayer.md`). Vorschlag: TLS über `QSslSocket` (Qt Network, keine neue Abhängigkeit), Server-Zertifikat selbst erzeugt und vom Client beim ersten Verbinden festgehalten (wie SSH), Login und Ratenbegrenzung unverändert darüber; Kosten (Handshake, CPU je Spieler) headless messen. Dazu der IPv6-Hinweis: Ein Server oder eine fürs LAN geöffnete Welt auf allen Schnittstellen ist mit IPv6 ohne Firewall aus dem Internet erreichbar; in 39 sichtbarer machen, mit 48 den Standard entscheiden. Vom Menschen am 2026-10-20 beauftragt: vor dem Release.
- **Showcase-Layout (ohne eigene Etappe):** Ein neuer Mod verschiebt Chunks einer bestehenden Showcase-Welt, weil die Plätze nach Register-Reihenfolge vergeben werden. Feste Plätze pro Name (z. B. stabile Zuordnung Name → Platz, in der Weltinfo gespeichert, neue Einträge hinten). Gering genug, um in der nächsten Etappe mitzugehen, die die Showcase-Welt ohnehin anfasst; sonst als eigene Nummer einsortieren.
- **Mods global statt pro Welt (nur Vorschlag, nicht umsetzen):** Mods gelten heute global für alle Welten. Vorschlag: Mod-Auswahl pro Welt (in `world.json`, beim Öffnen fehlende Mods melden, Server gibt die Liste vor). Keine Nummer, bis der Mensch es beauftragt.
