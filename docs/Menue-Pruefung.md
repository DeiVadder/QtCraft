# Menü-Prüfung (Paket 76.1)

Stand 2026-10-28, `dev` 3de6ba0 (nach 55.1b Kartenvorschau, 79.13 Befehlsliste, 79.25 Boot). Prüfung aller Menüs und Bildschirme gegen den [UI-Leitfaden](UI-Leitfaden.md); Grundlage für die Umsetzung in 76.2. Der Bericht ändert keinen Code.

## 0 Methode

- **Bilder:** 63 Aufnahmen unter `docs/screenshots/76-1-*.jpg` (JPEG, 1280 px breit, die Aufnahmen im Fenster 2560 × 1440 mit 1600 px). Aufgenommen offscreen mit dem Debug-Build des Haupt-Checkouts und den vorhandenen Hooks (`QTCRAFT_OFFSCREEN`, `QTCRAFT_SCREENSHOT`, `QTCRAFT_WINDOW_SIZE`, `--overlay`, `open:`/`click:`/`text:`/`pad:`-Schritte, `--handbook`, `--join`, `--chat`, `QTCRAFT_THEME`, `QTCRAFT_COLOR_SCHEME`), auf Deutsch (die längeren Texte), Fenster 1280 × 720, Farbvariante C (Standard). Dazu: 1024 × 640 (kleinstes Fenster), Variante A (klassisch blau), Englisch, `uiScale` 200 im Fenster 1280 × 720 (wirksam 113 %, Dateien `…-200.jpg`) und im Fenster 2560 × 1440 (wirksam 225 %, `…-200-2560.jpg`), hohe Fenster (`…-hoch.jpg`) für ganze Formulare, Pad-Fokus über das geskriptete Pad (`…-pad.jpg`).
- **Automatische Prüfungen:** In allen 75 Läufen meldeten Layout-Check und Zugänglichkeits-Check 0 Elemente außerhalb des Fensters und 0 Steuerelemente ohne Namen. Die Befunde unten sind also keine Layout-Fehler im Sinn der Tests, sondern Fragen von Ordnung, Lesbarkeit und Einheitlichkeit.
- **Kriterien** (FORTSCHRITT, Etappe 76): Gruppierung (G), Reihenfolge (R), Beschriftung (B), Abstände und Raster (A), Hierarchie (H), Konsistenz zwischen den Seiten (K), Lesbarkeit (L), Wege zu häufigen Einstellungen (W); dazu Zugänglichkeit (Z: Kontrast, Fokus, Pad).
- **Schwere:** **hoch** – führt in die Irre, kostet Lesbarkeit oder bricht eine Regel des Leitfadens sichtbar; **mittel** – uneinheitlich oder umständlich; **niedrig** – Feinschliff.
- **Nicht aufgenommen:** Trennungs- und Meldungsbildschirm (`DisconnectScreen`, `MessageScreen`; kein Hook führt ohne Server-Abbruch dorthin), Rucksack (Etappe 68) und die Rückfrage „Welt löschen“ (der Pad-Weg Runter/Rechts/A aus der Startseite landete auf „Neue Welt“, siehe HM-6). Für 76.2 genügt dafür ein Hook `open:deleteWorldDialog` bzw. `--overlay message:<text>` (Teil von Paket 76.2-i).

## 1 Die wichtigsten Befunde

1. **Pad-Fokus landet auf dem Rückweg-Eintrag** (Einstellungen, Server verwalten): Nach dem ersten Pad-Druck steht der Fokus auf „‹ Hauptmenü“/„‹ Pausenmenü“ und bleibt dort, während LB/RB die Kategorie wechseln. Ein A verlässt dann den Bildschirm. Leitfaden §10 verlangt den Fokus auf der ersten Einstellung. → [Bild](screenshots/76-1-09-einstellungen-pad-start.jpg), [Bild](screenshots/76-1-30-admin-spieler.jpg)
2. **Handbuch wächst nicht mit der Oberfläche:** Im großen Fenster (225 %) wachsen Überschriften, die Einträge der Liste und die Werte der Tabelle bleiben klein (Labels ohne `Ui.type`-Stufe erben die Standardschrift). → [Bild](screenshots/76-1-08-handbuch-200-2560.jpg)
3. **Bildschirme über der Welt lassen HUD und Welt-Hinweise durchscheinen:** Inventar, Behälter, Pause, Handbuch und Einstellungen im Spiel zeigen Schnellleiste, Herzen, Chat-Zeilen mit Roh-IDs und „Klicken, um dich umzusehen …“ hinter oder neben dem Paneel; bei 225 % liegt „Zum Menü“ auf den Herzen. → [Bild](screenshots/76-1-12-inventar-200.jpg), [Bild](screenshots/76-1-10-pause-200-2560.jpg)
4. **Inventare und Maschinen sind noch nicht auf den Bausteinen** (76.2 laut Leitfaden): jede hat eigene Breite, Titelposition, Schließ-Weg und einen Hilfetext von 2–6 Zeilen; Zaubertisch und Amboss nutzen die Pixelschrift, die laut Leitfaden §4 nur dem Logo gehört. → [Bild](screenshots/76-1-18-amboss.jpg)
5. **Widersprüchliche Texte:** „Server verwalten → Server“ verweist auf „Speichern und zum Menü“ im Pausenmenü, dort heißt der Knopf „Zum Menü“; die Spielregeln sagen „gelten sofort“, haben aber Speichern/Verwerfen; „Umschalt-Klick“ neben „Shift-Klick“; „eins/eines/einen ablegen“. → [Bild](screenshots/76-1-30-admin-server.jpg), [Bild](screenshots/76-1-30-admin-einstellungen.jpg)
6. **„Neue Welt“: die Kartenvorschau schiebt den Spielmodus unter den Rand.** Bei 1280 × 720 sieht man Name, Typ, Seed und Karte, aber nicht „Spielmodus“ – die zweithäufigste Wahl. → [Bild](screenshots/76-1-03-neue-welt-karte.jpg)
7. **„‹ Zurück“ steht an drei verschiedenen Orten** (Seitenleiste, Fuß links, gar nicht/„Schließen“/„×“/„Abbrechen“), siehe Teil B.

## 2 Befunde je Bildschirm

Jede Zeile: Kennung, Schwere, Kriterium, Befund, Vorschlag. Die Kennungen tauchen in den Paketen (Teil E) wieder auf.

### 2.1 Hauptmenü, Startseite und Erststart

Bilder: [Start](screenshots/76-1-02-menu-start.jpg) · [Erststart](screenshots/76-1-01-menu-erststart.jpg) · [Pad](screenshots/76-1-02-menu-start-pad.jpg) · [1024 × 640](screenshots/76-1-02-menu-start-1024.jpg) · [200 %](screenshots/76-1-02-menu-start-200.jpg) · [Variante A](screenshots/76-1-02-menu-start-A.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| HM-1 | mittel | H, G | Die Karte „Zuletzt gespielt“ und die erste Zeile von „Deine Welten“ zeigen dieselbe Welt mit denselben Daten untereinander. | Die zuletzt gespielte Welt in der Liste weglassen oder nur markieren („zuletzt“); die Liste beginnt mit der zweitjüngsten. |
| HM-2 | mittel | Z, H | „Löschen“ ist ein leiser Textknopf gleich groß und gleich hell wie „Spielen“ daneben, in jeder Zeile. Ein Fehlklick öffnet zwar die Rückfrage, aber die Zeile wirkt wie zwei gleichwertige Aktionen. | Leitfaden 6.9 (geplant): ganze Zeile fokussierbar, A/Doppelklick spielt, „…“-Menü für Löschen, Umbenennen, Ordner öffnen. |
| HM-3 | niedrig | L | Kacheln zeigen nur den Anfangsbuchstaben; „Flach“ und „Flusstal“ sehen gleich aus („F“). | Kachel mit Welttyp-Symbol (Gras, flach, Leere, Schaukasten) oder einem kleinen Kartenausschnitt (55.1b liefert ihn) statt des Buchstabens. |
| HM-4 | niedrig | A | Erststart: eine Karte oben, darunter leere Fläche über zwei Drittel des Fensters. | Karte senkrecht mittig oder darunter drei kurze Einstiege („Handbuch: Erste Schritte“, „Mehrspieler“, „Charakter“). |
| HM-5 | niedrig | K | „Charakter“ steht zweimal in der Seitenleiste (Eintrag und Profilkarte „Charakter anpassen“). | Profilkarte behalten (zeigt die Figur), den Eintrag „Charakter“ streichen – oder umgekehrt; nicht beides. |
| HM-6 | mittel | Z | Pad: Runter von „Weiterspielen“ geht auf „Neue Welt …“, die Zeilen der Liste sind nur über ihre Knöpfe erreichbar; der Weg Runter-Runter-Runter-Rechts-Rechts-A öffnete „Neue Welt“ statt „Löschen“. | Mit HM-2: Zeilen als Fokusziele, Runter wandert Zeile für Zeile. |
| HM-7 | niedrig | K | Die Reihenfolge der Liste schwankte zwischen zwei Läufen bei Welten derselben Minute ([1280](screenshots/76-1-02-menu-start.jpg) gegen [1024](screenshots/76-1-02-menu-start-1024.jpg)). | Nach Zeit mit Sekunden, bei Gleichstand nach Name sortieren. |

### 2.2 Neue Welt, Weltoptionen, Kartenvorschau (55, 55.1b)

Bilder: [Neue Welt mit Karte](screenshots/76-1-03-neue-welt-karte.jpg) · [ganzes Formular](screenshots/76-1-03-neue-welt-karte-hoch.jpg) · [225 %, Karte noch im Bau](screenshots/76-1-03-neue-welt-200-2560.jpg) · [Weltoptionen](screenshots/76-1-04-weltoptionen.jpg) · [Weltoptionen ganz](screenshots/76-1-04-weltoptionen-hoch.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| NW-1 | hoch | R, W | Reihenfolge Welt → Weltgenerierung → Kartenvorschau → Regeln: Der Spielmodus liegt bei 1280 × 720 unter dem Rand, die große Karte davor. | Regeln (Spielmodus) direkt unter „Welt“; danach „Weltgenerierung“ mit der Karte. Siehe Entwurf D.3. |
| NW-2 | mittel | G | „Anderer Seed“ steht in der Kartengruppe am rechten Rand, das Seed-Feld zwei Gruppen höher. | Würfel-Knopf „Zufällig“ direkt rechts am Seed-Feld; die Karte zeigt immer den Seed des Felds. |
| NW-3 | mittel | K, A | „Inventar beim Tod behalten“ ist ein Kästchen links ohne Spalte, alle anderen Zeilen haben Beschriftung links und Steuerelement in der 60-%-Spalte. | `SettingRow` mit `SettingSwitch` (Leitfaden 6.5: Ein/Aus ist Schalter; Kästchen nur in Mehrfachauswahl). |
| NW-4 | mittel | L, H | Gruppe „Weltgenerierung“ mit der Zeile „Dörfer, Biome, Höhlen …“: Beschriftung mit Auslassungspunkten, Knopf schmal links in der Spalte. Die Zusammenfassung („Alle Optionen auf Standard“) ist die eigentliche Information. | Beschriftung „Weltoptionen“, Hilfetext die Zusammenfassung, Knopf „Anpassen …“ rechtsbündig. |
| NW-5 | mittel | A, L | Kartengruppe: Karte 400 px, Legende mit 17 Zeilen (Biome bis „< 1 %“), Zoom-Knöpfe unter der Karte, Schalter „Dörfer und Bauwerke zeigen“ in der Kartengruppe. Die Karte erscheint auf beiden Seiten (Neue Welt und Weltoptionen). | Auf „Neue Welt“ eine kleinere Karte mit den 6 häufigsten Biomen und „+ 11 weitere“; die volle Karte samt Legende nur auf „Weltoptionen“. |
| NW-6 | niedrig | L | Weltoptionen: Regler zeigen nicht, wo der Standard liegt (1,0×, 384 Blöcke, 65 %). | Kleine Marke auf der Spur am Standardwert (Baustein `SettingSlider`, Eigenschaft `defaultValue`). |
| NW-7 | mittel | K | „‹ Zurück“ im Fuß links (E-917 dokumentiert die Abweichung von Leitfaden §5.1). | Teil B.4: Regel vereinheitlichen. |
| NW-8 | niedrig | L | Während die Karte gezeichnet wird: graue Fläche „Karte wird gezeichnet …“ ohne Fortschritt (225 %-Bild). | Grobe Stufe zuerst zeigen (E-921 baut grob → fein) oder ein schmaler Fortschrittsbalken. |

### 2.3 Mehrspieler

Bilder: [leer](screenshots/76-1-05-mehrspieler.jpg) · [Ablehnung](screenshots/76-1-05-mehrspieler-abgelehnt.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| MP-1 | mittel | A, H | Ohne zuletzt genutzte Server nimmt die leere Liste zwei Drittel der Breite ein (ein Satz in der Mitte), das Formular steht schmal rechts. | Leerer Zustand: Formular links in voller Breite (`Ui.screen.m`), Liste erscheint erst mit Einträgen. |
| MP-2 | hoch | L, H | Die Ablehnung steht als roter Text ganz unten am Fensterrand, 350 px vom Knopf „Beitreten“, ohne Symbol. | `Hint` Fehler (mit „×“) direkt unter „Beitreten“ (Leitfaden 6.8: nie nur Farbe). |
| MP-3 | niedrig | B | Hilfetext unter „Passwort“ hat 3 Zeilen (Leitfaden §7: ≤ 2). | „Nur nötig, wenn der Server eines hat.“ Den Satz über Speichern und Verschlüsselung in die Hilfe im Handbuch. |
| MP-4 | niedrig | K | „Im lokalen Netzwerk“ ist nur ein Erklärtext; keine Liste gefundener LAN-Welten. | Unverändert lassen, bis es eine Suche gibt (kein 76.2-Thema), aber den Text auf 2 Zeilen kürzen. |

### 2.4 Charakter (Hauptmenü und im Spiel)

Bilder: [Seite](screenshots/76-1-06-charakter.jpg) · [Rückfrage](screenshots/76-1-06-charakter-ungespeichert.jpg) · [im Spiel](screenshots/76-1-27-charakter-spiel.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| CH-1 | mittel | G | Neun Zeilen ohne Gruppen: Kleidung, Frisur, Umhang, vier Farbreihen, dann „Voreinstellung“ mit Aufklappliste, „Löschen“, Feld „Neue Vorlage“ und „Vorlage speichern“. | Gruppen „Aussehen“ (Kleidung, Frisur, Umhang), „Farben“ (vier Reihen), „Vorlagen“ (Liste, Speichern, Löschen). |
| CH-2 | mittel | K | „Verwerfen“/„Speichern“ stehen mitten auf der Seite unter dem Inhalt, auf „Neue Welt“ ist der Fuß am unteren Rand angeheftet. Im Spiel kommt „Schließen“ (leise) dazu, im Menü nicht. | Fuß angeheftet wie „Neue Welt“: links „Zufall“, „Standardaussehen“; rechts „Verwerfen“, „Speichern“. „Schließen“ entfällt im Spiel (Esc/B, Rückfrage greift). |
| CH-3 | niedrig | L | „Mit V im Spiel siehst du deinen Charakter von hinten oder von vorne.“ steht als loser Satz zwischen Vorlagen und Knöpfen. | Hilfetext unter der Vorschau, mit `KeyBadge` „V“. |
| CH-4 | niedrig | A | Die Vorschau-Steuerung hat zwei Segmentreihen plus „Ansicht zurücksetzen“ im selben Raster; der Knopf sieht aus wie ein drittes Segment. | „Ansicht zurücksetzen“ als leiser Knopf unter den Segmenten. |
| CH-5 | gut | – | Rückfrage „Ungespeicherte Änderungen“ entspricht 6.0 genau (Fokus auf „Weiter bearbeiten“, destruktiv links, primär rechts). | Vorbild für alle Rückfragen. |

### 2.5 Mods & Pakete, Texturpakete

Bilder: [Mods](screenshots/76-1-07-mods.jpg) · [Texturpakete](screenshots/76-1-09-einstellungen-texturpakete.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| MO-1 | mittel | B | Leerer Zustand „Lege einen Mod-Ordner mit pack.json in das Mod-Verzeichnis“ – Fachbegriff, keine Aktion daneben; „Mod-Ordner öffnen“ steht gesperrt im Fuß. Warnung über fehlende Mods erscheint, obwohl keine Mods da sind. | Leerer Zustand nach §5: „Noch keine Mods. [Mod-Ordner öffnen]“, Link „Wie baue ich einen Mod?“ ins Handbuch; die Warnung nur mit mindestens einem Mod. |
| MO-2 | niedrig | K | Seitenleiste „Mods & Pakete“, Seitentitel „Mods“. | Titel „Mods & Pakete“ oder Eintrag „Mods“ (Texturpakete liegen in den Einstellungen, Entscheidung 8). |
| TP-1 | mittel | K | Texturpakete: „Texturpaket-Ordner öffnen“/„Neu laden“ stehen oben im Inhalt, auf der Mods-Seite die gleichen Aktionen im Fuß. | Beide Seiten: Seitenaktionen im Fuß links (§5.4). |
| TP-2 | mittel | L, B | Beschreibungen englisch auf deutscher Oberfläche, „Mitwirkende: … (Rendered at run time from content/*/textures/*.json)“ technisch. | Deutsche Texte in die Sprachdateien der eingebauten Pakete; die Quellangabe nur im Handbuch/Tooltip. |
| TP-3 | mittel | W | „Gelten von oben nach unten“, aber kein sichtbarer Weg, die Reihenfolge zu ändern; Schalter oben rechts statt mittig. | Aktive Pakete oben als eigene Gruppe mit „Nach oben/unten“ (Pad: Y/X), inaktive darunter; Schalter senkrecht mittig. |

### 2.6 Handbuch (Hauptmenü und im Spiel)

Bilder: [Menü](screenshots/76-1-08-handbuch-menu.jpg) · [Suche](screenshots/76-1-08-handbuch-suche.jpg) · [im Spiel](screenshots/76-1-21-handbuch-spiel.jpg) · [113 %](screenshots/76-1-08-handbuch-200.jpg) · [225 %](screenshots/76-1-08-handbuch-200-2560.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| HB-1 | hoch | L | Bei 225 % bleiben Eintragsnamen und Tabellenwerte klein, Überschriften wachsen (Labels ohne `font.pixelSize` in `HandbookEntryRow.qml` und `HandbookPage.qml`; feste `Ui.px(15/18/26)` statt Stufen). | Alle Texte auf `Ui.type`-Stufen; Handbuch in `menusUseOnlyTheSizeSteps` aufnehmen (war für 76.2 vorgesehen). |
| HB-2 | mittel | B | Kapitel „Spielen“ heißt wie der erste Eintrag des Hauptmenüs. | Kapitel „Grundlagen“ oder „Anleitung“. |
| HB-3 | mittel | L, B | Die Eigenschaften-Tabelle beginnt mit „ID qtcraft:furnace“ und endet mit „Zustände facing (north, east, south, west)“ (englische Rohwerte). | Zeilen ID, Zustände, Klang unter „Technisches“ (eingeklappt, Baustein 6.2 `collapsible`); Zustände übersetzen. |
| HB-4 | mittel | B, Z | Kopfzeile „Klicke auf einen Namen oder ein Item … Rücktaste geht zurück, Esc oder H schließt.“ – lang, nur für Maus und Tastatur. | Ersetzen durch `KeyBadge`-Zeile bzw. `PadHints` je Eingabegerät. |
| HB-5 | niedrig | L | Liste schneidet lange Namen ab („Magentafarbenes Betonp…“); manche Einträge ohne Symbol („Signalfackel (aus)“). | Liste 20 % breiter (`Ui.size.handbookEntries`) oder zweizeilig; fehlende Symbole in `export-handbook` als Problem melden. |
| HB-6 | mittel | L | Im Spiel scheint die Schnellleiste durch das Paneel (unten im Bild sichtbar); Leitfaden 8b verlangt „weder Welt noch HUD scheinen durch“. | HUD ausblenden, solange ein Vollbild-Bildschirm offen ist (gilt auch für Einstellungen im Spiel, Server verwalten). |

### 2.7 Einstellungen (sieben Seiten)

Bilder: [Grafik ganz](screenshots/76-1-09-einstellungen-grafik-hoch.jpg) · [Audio](screenshots/76-1-09-einstellungen-audio.jpg) · [Steuerung](screenshots/76-1-09-einstellungen-steuerung.jpg) · [Controller](screenshots/76-1-09-einstellungen-controller.jpg) · [Oberfläche](screenshots/76-1-09-einstellungen-oberflaeche.jpg) · [Texturpakete](screenshots/76-1-09-einstellungen-texturpakete.jpg) · [Erweitert](screenshots/76-1-09-einstellungen-erweitert.jpg) · [Pad-Start](screenshots/76-1-09-einstellungen-pad-start.jpg) · [113 %](screenshots/76-1-09-einstellungen-grafik-200.jpg) · [Variante A](screenshots/76-1-09-einstellungen-grafik-A.jpg) · [Englisch](screenshots/76-1-09-einstellungen-grafik-en.jpg) · [im Spiel](screenshots/76-1-11-einstellungen-spiel.jpg)

Die Einstellungen sind der am weitesten ausgearbeitete Teil: Raster 40/60, Gruppen, Schalter, Segmente und Hilfetexte stimmen auf allen Seiten überein, Englisch und Deutsch passen.

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| ES-1 | hoch | Z | Pad: der erste Druck setzt den Fokus auf „‹ Hauptmenü“; LB/RB wechseln die Seite, der Fokus bleibt in der Seitenleiste. Ein A führt aus den Einstellungen. | Nach dem Öffnen und nach jedem LB/RB den Fokus auf die erste Einstellung der Seite (Leitfaden §10); der Rückweg-Eintrag nur über Links/B. |
| ES-2 | mittel | B | „Mausempfindlichkeit 0,15“ und „Kennlinie 1,5“ als nackte Zahlen; „Belichtung 1.00×“ mit Punkt, „1,0×“ sonst mit Komma. | Empfindlichkeit in % (Leitfaden 6.4), Kennlinie mit benannten Enden („linear … fein in der Mitte“), Belichtung über `Qt.locale()` formatieren. |
| ES-3 | mittel | W, B | „Größe der Oberfläche 200 %“ wirkt im Fenster 1280 × 720 nur mit 113 % (E-725 begrenzt auf das, was passt); die Liste sagt das nicht. | Hilfetext „Wirksam: 113 % (begrenzt durch die Fenstergröße)“. |
| ES-4 | niedrig | G | Grafik: „Kamerawackeln“ in der Gruppe „Welt“ neben Sichtweite. „Pause“ auf „Oberfläche“ ist eine Gruppe mit einer Zeile. | „Kamerawackeln“ mit „Sichtfeld beim Sprinten“ in eine Gruppe „Bewegung und Komfort“; die Pause-Zeile in „Ansicht“. |
| ES-5 | niedrig | L | Hilfetexte mit 3 Zeilen („Realistische Qualität“, „In Menüs und im Hintergrund“) gegen §7 (≤ 2). | Kürzen; Einzelheiten ins Handbuch. |
| ES-6 | niedrig | B | Erweitert: Hinweis „Mit --stats <ms> gehen die Messwerte auch ins Log“ – Kommandozeile im Spiel. | Streichen oder in die Hilfe des Handbuchs. |
| ES-7 | niedrig | K | Steuerung: zweite Belegungsspalte überall „–“ ohne Kopf; der Satz „Klicke auf eine Taste …“ steht als Fließtext über der Tabelle. | Spaltenköpfe „Taste“, „Zweite Taste“; Hilfe als Hilfetext der Gruppe. |
| ES-8 | gut | – | „ab Weltstart“, „ab dem nächsten Start“ als Schild an der Einstellung (§5) und die Tasten-Schilder (fn+F11, L, P) sind gelungen. | – |

### 2.8 Pausenmenü

Bilder: [100 %](screenshots/76-1-10-pause.jpg) · [Pad](screenshots/76-1-10-pause-pad.jpg) · [225 %](screenshots/76-1-10-pause-200-2560.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| PA-1 | hoch | L, A | Herzen, Schnellleiste, Fadenkreuz und der Hinweis „Klicken, um dich umzusehen …“ bleiben sichtbar; bei 225 % liegt „Zum Menü“ auf den Herzen, die Pad-Hinweise auf der Schnellleiste. | HUD und Welt-Hinweis während der Pause ausblenden (wie HB-6); Pausenmenü auf `ScreenFrame` (Entwurf D.1). |
| PA-2 | mittel | G, H | Sieben gleich aussehende Knöpfe untereinander: Spielen, Einstellungen, Charakter, LAN, Server verwalten, Handbuch, Zum Menü. Das Verlassen ist nicht abgesetzt. | Drei Gruppen mit Abstand `xl`: Weiterspielen · (Einstellungen, Handbuch, Charakter) · (Im LAN öffnen, Server verwalten) · abgesetzt „Speichern und zum Menü“. Entwurf D.1. |
| PA-3 | mittel | B, K | „Zum Menü“, aber „Server verwalten → Server“ sagt „Speichern und zum Menü“; der Leitfaden nennt „Pause → Welt-Optionen“ (§8), den es nicht gibt. | Knopf „Speichern und zum Menü“; „Welt-Optionen“ entweder bauen (55-Folge) oder aus dem Leitfaden nehmen. |
| PA-4 | niedrig | A | Das Tasten-Schild „H“ verschiebt „Handbuch“ aus der Mitte. | Text mittig, Schild absolut rechts (wie im Hauptmenü). |

### 2.9 Inventar, Kreativ, Behälter und Maschinen

Bilder: [Inventar](screenshots/76-1-12-inventar.jpg) · [Pad](screenshots/76-1-12-inventar-pad.jpg) · [113 %](screenshots/76-1-12-inventar-200.jpg) · [Kreativ](screenshots/76-1-13-kreativ.jpg) · [Werkbank](screenshots/76-1-20-werkbank.jpg) · [Ofen](screenshots/76-1-14-ofen.jpg) · [Truhe](screenshots/76-1-15-truhe.jpg) · [Handel](screenshots/76-1-16-handel.jpg) · [Zaubertisch](screenshots/76-1-17-zaubertisch.jpg) · [Amboss](screenshots/76-1-18-amboss.jpg) · [Steinschneider](screenshots/76-1-19-steinsaege.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| IV-1 | hoch | K, H | Kein gemeinsamer Rahmen: Titel einmal oben links (Ofen, Truhe), einmal mitten neben dem Raster (Inventar, Werkbank), einmal rechts (Handel); Paneele 525–1080 px breit; das Spieler-Inventar steht mal zentriert, mal links. Nur Kreativ hat „×“. | Gemeinsamer `SlotScreen`-Rahmen (Entwurf D.2): Kopf mit Titel links und „Schließen“-Schild (E/Esc bzw. B) rechts, oben der Block-Bereich, darunter immer gleich das Spieler-Inventar mit Überschrift „Inventar“. |
| IV-2 | hoch | L | Hilfetexte von 2 bis 6 Zeilen Fließtext (Amboss 6, Zaubertisch 4) unter jedem Bildschirm; im Pad-Modus stehen Text und `PadHints` doppelt da. | Bedienung als eine `KeyBadge`-Zeile („Klick aufnehmen · Rechtsklick eins · Umschalt-Klick verschieben“), mit Pad die `PadHints`; Erklärungen der Maschine (was wohin) als Beschriftung an den Plätzen; Rest ins Handbuch (Link „Mehr im Handbuch“). |
| IV-3 | hoch | K | Zaubertisch und Amboss nutzen Pixelify Sans für Titel, Feld und Zustand (`WorkbenchTitle`, `ParchmentField`), Leitfaden §4: „kein anderer Text nutzt die Schrift“. | Entweder Regel bestätigen und die Pixelschrift dort entfernen (Titel wie alle anderen), oder den Leitfaden bewusst erweitern (Runen und Pergament als Spielgegenstand). Empfehlung: Runen behalten (sind Spielinhalt), Titel und Eingabefeld in Systemschrift. |
| IV-4 | mittel | B | „Umschalt-Klick“ (Inventar, Truhe, Ofen) gegen „Shift-Klick“ (Handel); „eins“/„eines“/„einen ablegen“. | Ein Wort: „Umschalt-Klick“, „eins ablegen“ (`translations/de.json`). |
| IV-5 | mittel | G | Truhe: Truhen-Raster und Spieler-Inventar sind zwei gleiche 9 × 3-Blöcke ohne Überschrift dazwischen. | Überschriften „Truhe“ und „Inventar“ (Teil von D.2). |
| IV-6 | mittel | L | Chat-Meldungen mit Roh-IDs („5 × qtcraft:raw_iron gegeben“, „Dorfbewohner 2 ist jetzt qtcraft:smith“) laufen neben/unter dem Paneel. | Server-Meldungen mit Anzeigenamen der Inhalte; Chat während Bildschirmen ausblenden oder über das Paneel heben. |
| IV-7 | mittel | K | Kreativ: Kategorien als zwei Reihen Knöpfe statt Segmenten, „Werkzeug und Rüstung“ (Handbuch: „Werkzeuge und Rüstung“); „Herstellen und Rüstung“ oben rechts sieht aus wie ein Reiter. | Kategorien als `NavList` links (wie Handbuch), Namen aus derselben Quelle wie das Handbuch; Umschalter „Inventar“ als Segment. |
| IV-8 | niedrig | L | Ofen/Handel: Plätze und Fortschritt ohne Beschriftung (Brennstoff, Material, Ergebnis; Handels-XP-Balken ohne Text). | Kleine Beschriftungen in `textMuted` an den Plätzen; Balken mit „Erfahrung des Händlers“. |
| IV-9 | niedrig | A | „Rüstung: 0“ unter den Rüstungsplätzen als einzelne Zeile; „Jetzt herstellbar“ ohne Rahmen, Symbole sehr klein (Steinknopf). | Rüstungswert als Schild an der Figur; Rezeptliste als `ListRow` mit `Ui.size.handbookIcon`. |

### 2.10 Chat, Spielerliste, Schild, Tod, LAN, Bildschirmtastatur

Bilder: [Chat](screenshots/76-1-24-chat.jpg) · [Tastatur](screenshots/76-1-24-chat-pad.jpg) · [Spielerliste](screenshots/76-1-23-spielerliste.jpg) · [Schild](screenshots/76-1-26-schild.jpg) · [Tod](screenshots/76-1-25-tod.jpg) · [LAN](screenshots/76-1-22-lan.jpg) · [HUD](screenshots/76-1-29-hud.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| CT-1 | mittel | A | Chat: Vorschlagszeile „/help listet …“ liegt über der linken Schnellleiste, der Welt-Hinweis unter der Eingabe scheint durch. | Eingabe und Vorschläge über der Schnellleiste (HUD-Abstand, E-725), Welt-Hinweis im Chat ausblenden. |
| CT-2 | mittel | Z, B | Bildschirmtastatur: Belegung QWERTY auch auf Deutsch (y oben, z unten); Hinweiszeile als Text („X: löschen Y: Leerzeichen LB/RB: Cursor …“) statt Pad-Symbolen. | QWERTZ für `de`; Hinweise über `PadHintRow` wie überall. |
| CT-3 | niedrig | B | Spielerliste: eigener Ping „? ms“, unbenannte Welt „Test World“ englisch; „Operator“ in Warnfarbe. | „–“ statt „? ms“ für den Gastgeber; Rolle in `textSecondary`. |
| SG-1 | mittel | K | Schild: „Fertig“ übernimmt (offen nach Leitfaden §5), Titel schwebt ohne Paneel, HUD sichtbar. | Muster 1 (Text gilt sofort, Esc schließt) mit `ScreenFrame` und Hilfetext; kein „Fertig“. |
| TD-1 | mittel | K | Tod: zwei verschieden breite Knöpfe ohne Fase, Titel sehr groß, kein Bausteinrahmen (76.2 laut Leitfaden). | `ScreenFrame` `Ui.screen.s`, „Wiederbeleben“ primär, „Speichern und zum Menü“ sekundär, gleich breit wie im Pausenmenü. |
| LA-1 | gut | – | LAN-Dialog: Form, Hilfetexte, Fuß (Abbrechen, primär rechts) stimmen. | Vorbild für Formulare in Dialogen. |

### 2.11 Server verwalten (E-863, 79.13)

Bilder: [Übersicht](screenshots/76-1-30-admin-uebersicht.jpg) · [Spieler](screenshots/76-1-30-admin-spieler.jpg) · [Chat](screenshots/76-1-30-admin-chat.jpg) · [Befehle](screenshots/76-1-30-admin-befehle.jpg) · [Listen](screenshots/76-1-30-admin-listen.jpg) · [Einstellungen](screenshots/76-1-30-admin-einstellungen.jpg) · [Log](screenshots/76-1-30-admin-log.jpg) · [Server](screenshots/76-1-30-admin-server.jpg)

| ID | Schwere | Krit. | Befund | Vorschlag |
|---|---|---|---|---|
| AD-1 | hoch | Z | Pad-Fokus auf „‹ Pausenmenü“ in allen Abschnitten (wie ES-1). | wie ES-1. |
| AD-2 | hoch | B | „Server“: „Zum Beenden ‚Speichern und zum Menü‘ im Pausenmenü wählen“ – der Knopf heißt „Zum Menü“. | Mit PA-3 einheitlich „Speichern und zum Menü“. |
| AD-3 | hoch | B, K | Spielregeln: Hinweis „Regeln und Grenzen gelten sofort“, unten aber „Verwerfen“/„Speichern“ (Muster 2). Seitenleiste „Einstellungen“, Seitentitel „Spielregeln und Grenzen“; „Einstellungen“ ist zudem der Name der Spiel-Einstellungen. | Hinweis „Änderungen gelten nach Speichern sofort für alle Spieler“; Eintrag und Titel „Spielregeln“. |
| AD-4 | mittel | Z | Spieler: die eigene Zeile (Gastgeber) bietet „Rechte entziehen“, „Rauswerfen …“, „Sperren …“. | Für die eigene Zeile nur Hinweis „Du stellst die Welt bereit“; Aktionen ausblenden. |
| AD-5 | mittel | L | Übersicht: solange kein Status da ist, zeigen alle Diagramme „0,0“, „0 MB“ mit leeren grünen Rahmen. | Bis zur ersten Messung „–“ und ein Platzhalter; Rahmen in `graphFrame` dezenter (heute fast `accent`). |
| AD-6 | niedrig | A | Log: Filter-Segmente unterschiedlich breit („Warnungen“ dreimal so breit wie „Info“). | `Segments` mit gleicher Breite je Segment. |
| AD-7 | niedrig | A | Befehle: jede Zeile der Liste ist eine Karte mit 55–75 px Höhe; die Liste hat rund 40 Befehle. | Kompakte Zeilen (Name fett, Beschreibung in derselben Zeile abgeschnitten, ganz bei Fokus). |
| AD-8 | niedrig | B | Spielregeln: „Schlüssel: keepInventory“ als erste Hilfezeile (E-863 so gewollt), Hilfen bis 3 Zeilen. | Schlüssel als kleines Schild rechts neben dem Namen (`badge`), Hilfe ≤ 2 Zeilen. |

## 3 Querschnitt: Uneinheitlichkeiten

### 3.1 Rahmen und Hintergrund

| Bildschirm | Rahmen | HUD/Welt dahinter |
|---|---|---|
| Hauptmenü, Einstellungen, Handbuch, Server verwalten (Menü) | Seitenleiste + Seite, deckend | – |
| Einstellungen, Handbuch, Server verwalten (im Spiel) | Seitenleiste + `panel` 94 % | Schnellleiste scheint durch |
| Pause, Tod | kein Rahmen, Knöpfe frei über der Welt | HUD voll sichtbar |
| Inventare, Maschinen | eigenes Paneel je Bildschirm | HUD, Chat und Welt-Hinweis sichtbar |
| LAN, Charakter (Spiel), Rückfragen | `ScreenFrame`/`ModalDialog` | abgedunkelt |

**Vorschlag:** Sobald ein Bildschirm offen ist, blendet das HUD Chat-Zeilen, Welt-Hinweis, Blockname oben und (bei Vollbild-Bildschirmen) die Schnellleiste aus; Pause und Tod bekommen `ScreenFrame`.

### 3.2 Schließen und Zurück

| Ort | Weg zurück |
|---|---|
| Einstellungen, Handbuch, Server verwalten | Rückweg-Eintrag in der Seitenleiste (8b) |
| Neue Welt, Weltoptionen | „‹ Zurück“ im Fuß links (E-917) |
| Mehrspieler, Mods, Charakter (Menü) | kein sichtbarer Weg, nur Seitenleiste/Esc |
| Charakter (Spiel) | „Schließen“ leise im Fuß |
| Kreativ | „×“ oben rechts |
| Inventar, Maschinen | nur Text „E oder Esc schließt“ |
| LAN | „Abbrechen“ |
| Schild | „Fertig“ |

**Vorschlag (Entscheidung nötig, siehe E.0):** zwei Regeln statt acht. (a) **Seiten im Hauptmenü** (Unterseiten wie Neue Welt, Weltoptionen): „‹ Zurück“ im Fuß links, angeheftet – den Leitfaden §5.1 entsprechend ändern, da Fuß und primäre Aktion zusammengehören und das Pad ohnehin B nimmt. (b) **Bildschirme über der Welt** (Inventare, Maschinen, Charakter, Schild): Kopf mit Titel links und Schild „Schließen“ mit Taste (E/Esc bzw. B-Symbol) rechts, klickbar; kein „×“, kein „Fertig“.

### 3.3 Knopfreihenfolge und Hervorhebung

- Rückfragen (Charakter, Welt löschen): destruktiv links, sicher (Fokus) Mitte, primär rechts – gut.
- Fuß der Seiten: „Neue Welt“ und „Weltoptionen“ angeheftet, Charakter mitten auf der Seite, Spielregeln angeheftet rechts, Mods links (gesperrt), Texturpakete oben im Inhalt. → Einheitlich angeheftet, Seitenaktionen links, Entwurf-Aktionen rechts.
- Pausenmenü und Tod: primär oben (Pause) bzw. oben (Tod) – stimmt; Verlassen nicht abgesetzt (PA-2).

### 3.4 Texte

- Bedienhilfen als Fließtext statt `KeyBadge`/`PadHints`: Handbuch-Kopf, alle Inventare, Schild, Bildschirmtastatur, Steuerung.
- Pad-Hinweise: „LB“ steht ohne Text vor „RB Reiter“ und wirkt wie ein eigener Hinweis; „Reiter“ statt „Kategorie“ (Leitfaden 6.11); die LB/RB-Hinweise erscheinen auch dort, wo es keine Kategorien gibt (Startseite, Neue Welt, Pause). Vorschlag: ein Eintrag „LB RB Kategorie“, nur auf `SidebarScreen`-Bildschirmen.
- Begriffe: „Umschalt-Klick“/„Shift-Klick“, „eins/eines/einen“, „Werkzeug und Rüstung“/„Werkzeuge und Rüstung“, „Zum Menü“/„Speichern und zum Menü“, „Einstellungen“ doppelt belegt, „Spielen“ doppelt belegt.
- Englische Inhalte auf Deutsch: Texturpaket-Beschreibungen, Handbuch-Zustände, unbenannte Welt „Test World“, Roh-IDs in Chat-Meldungen.
- Zahlenformat: „1.00×“ (Belichtung) gegen „1,0×“.

### 3.5 Schrift und Größen

- Pixelschrift außerhalb des Logos: Zaubertisch, Amboss (IV-3).
- Feste `Ui.px(…)` statt `Ui.type`-Stufen und Labels ohne Größe: Handbuch (HB-1); Inventare und Tod sind laut `test_ui_palette` noch ausgenommen.
- Uneinheitliche Titelgrößen: Tod (~48 px), Pause (24), Inventare (24), Zaubertisch/Amboss (Pixelschrift 20).

## 4 Zugänglichkeit

- **Kontrast:** Die Paletten sind geprüft (`test_ui_palette`); sichtbar schwach sind nur gedimmte Zeilen (Audio bei „stumm“, gesperrte Grafikzeilen – gewollt) und gesperrte Knöpfe „Mod-Ordner öffnen“, „Ausführen“, „Hinzufügen“ (`textDisabled` auf `buttonDisabled`, ≈ 2:1; für gesperrte Elemente nach WCAG erlaubt, aber man erkennt nicht, *warum* gesperrt). Vorschlag: gesperrte Aktion mit Grund im Hilfetext (§6.3) oder als Tooltip **und** Text.
- **Fokusreihenfolge/Pad:** ES-1/AD-1 (Fokus auf Rückweg), HM-6 (Weltliste), Fokusring gut sichtbar auf Grün und Grau ([Pause](screenshots/76-1-10-pause-pad.jpg), [Inventar](screenshots/76-1-12-inventar-pad.jpg)). Y = „auf Standard zurücksetzen“ (Leitfaden §10) erscheint in keinem Pad-Hinweis.
- **Nur Farbe:** Mehrspieler-Fehler rot ohne Symbol (MP-2); Spielerliste „Operator“ nur farbig abgesetzt.
- **Helles Schema:** `QTCRAFT_COLOR_SCHEME=light` ändert die Menüs nicht (Aufnahme identisch mit Variante C, daher nicht eingecheckt); eine helle Variante gibt es nicht, die Varianten A/B/C sind nur über `QTCRAFT_THEME` wählbar. Die Prüfung „hell und dunkel“ beschränkt sich deshalb auf C (dunkel) gegen A (blau): A funktioniert auf allen geprüften Seiten ohne Kontrastprobleme ([Start A](screenshots/76-1-02-menu-start-A.jpg), [Grafik A](screenshots/76-1-09-einstellungen-grafik-A.jpg)).

## 5 Entwürfe (nur für Bildschirme mit echtem Umbau)

### D.1 Pausenmenü

Heute: sieben gleiche Knöpfe frei über der Welt, HUD sichtbar ([Bild](screenshots/76-1-10-pause.jpg)).

**Variante A – Karte, gruppiert (Empfehlung):**

```
            ┌──────────────────────────────────┐
            │  Spiel pausiert                  │
            │  Flusstal · Überleben · Tag 3    │
            │                                  │
            │  [██ Weiterspielen ████████████] │
            │                                  │
            │  [ Einstellungen ] [ Handbuch H ]│
            │  [ Charakter     ] [ Im LAN …   ]│
            │  [ Server verwalten …          ] │
            │  ──────────────────────────────  │
            │  [ Speichern und zum Menü      ] │
            └──────────────────────────────────┘
   (Welt abgedunkelt mit scrimStrong, HUD ausgeblendet)
```

**Variante B – Seitenleiste wie Hauptmenü:** links die Seitenleiste mit „Weiterspielen“ (primär) und den Einträgen, rechts der Welt-Steckbrief (Name, Seed, Spielzeit, Spieler online). Einheitlich mit 8b, aber für einen kurzen Halt schwerer als nötig.

### D.2 Gemeinsamer Rahmen für Inventare und Maschinen

Heute: jede Maschine anders ([Ofen](screenshots/76-1-14-ofen.jpg), [Handel](screenshots/76-1-16-handel.jpg), [Amboss](screenshots/76-1-18-amboss.jpg)).

**Variante A – fester Rahmen (Empfehlung):**

```
┌───────────────────────────────────────────────────────┐
│ Ofen                                  [Schließen  E ] │  Kopf (Ui.type.title)
├───────────────────────────────────────────────────────┤
│        Material [■]                                   │
│                 (▲) Brennt   ───▶  [■] Ergebnis       │  Bereich der Maschine,
│     Brennstoff  [■]                                   │  Plätze beschriftet
│                                                       │
│ Inventar                                              │  Gruppenüberschrift
│ [■][■][■][■][■][■][■][■][■]                           │
│ [■][■][■][■][■][■][■][■][■]                           │
│ [■][■][■][■][■][■][■][■][■]                           │
│ [■][■][■][■][■][■][■][■][■]  Schnellleiste            │
├───────────────────────────────────────────────────────┤
│ Klick nehmen · Rechtsklick eins · ⇧-Klick verschieben │  KeyBadge-Zeile oder PadHints
│ Mehr im Handbuch ›                                    │
└───────────────────────────────────────────────────────┘
```

Breite fest aus dem Inventar-Raster (9 Plätze + Innenabstand), das Spieler-Inventar steht immer an derselben Stelle; Werkbank, Handel und Zaubertisch bekommen links bzw. rechts eine Spalte dazu (Rezepte, Angebote), ohne dass das Inventar wandert.

**Variante B – Inventar unten fest, Maschine als Karte darüber:** das Spieler-Inventar bleibt am unteren Rand angeheftet (wie die Schnellleiste), die Maschine erscheint als Karte darüber. Ruhiger beim Wechsel zwischen Bildschirmen, braucht aber mehr Höhe (bei 1024 × 640 knapp).

### D.3 Neue Welt

Heute: Welt → Weltgenerierung → Karte → Regeln ([Bild](screenshots/76-1-03-neue-welt-karte-hoch.jpg)).

**Variante A – Regeln nach oben, Karte rechts (Empfehlung):**

```
Neue Welt
┌ Welt ──────────────────────────────┐ ┌ Vorschau ───────────┐
│ Name          [Neue Welt         ] │ │ ┌─────────────────┐ │
│ Welttyp       [Normal          ▾] │ │ │   (Karte 256²)  │ │
│ Seed          [          ] [🎲]   │ │ └─────────────────┘ │
├ Regeln ────────────────────────────┤ │ Gebirge 29 %        │
│ Spielmodus    [Überleben       ▾] │ │ Meer 20 % · Wüste … │
│ Inventar beim Tod behalten  (◉ )  │ │ [Weltoptionen …]    │
└────────────────────────────────────┘ └─────────────────────┘
‹ Zurück                                  [Erstellen und spielen]
```

Ab 1280 px Breite zweispaltig, darunter einspaltig mit der Vorschau nach den Regeln. Die volle Karte mit Zoom, Legende und „Dörfer zeigen“ nur auf „Weltoptionen“.

**Variante B – einspaltig, Karte eingeklappt:** Reihenfolge Welt · Regeln · Weltgenerierung (Zusammenfassung + „Weltoptionen …“) · „▸ Kartenvorschau“ eingeklappt (Baustein 6.2 `collapsible`). Kleinster Umbau, verschenkt aber die Karte als Anreiz.

## 6 Priorisierte Vorschläge und Pakete für 76.2

### E.0 Entscheidungen vorab (Koordinator)

1. Rückweg-Regel (B.3.2): Fuß links für Unterseiten im Hauptmenü, Kopf rechts „Schließen“ für Bildschirme über der Welt – Leitfaden §5.1 anpassen. *Empfehlung: ja.*
2. Pixelschrift in Zaubertisch/Amboss (IV-3): Runen behalten, Titel und Felder in Systemschrift. *Empfehlung: ja.*
3. Entwürfe: D.1 A, D.2 A, D.3 A. *Empfehlung: diese.*
4. Helles Schema: als eigenes Paket nach 76.2 (L), nicht in 76.2. *Empfehlung: verschieben.*

### E.1 Pakete

| Paket | Größe | Was ändert sich | Warum | Befunde |
|---|---|---|---|---|
| **76.2-a Pad-Fokus und Pad-Hinweise** | S | Fokus beim Öffnen und nach LB/RB auf die erste Einstellung (Einstellungen, Server verwalten); Rückweg-Eintrag nur über Links/B; Weltliste zeilenweise; `PadHints`: „LB RB Kategorie“ als ein Eintrag, nur auf Seitenleisten-Bildschirmen, Y-Hinweis wo Zurücksetzen geht; Test in `test_menu_layout.sh` (Fokus nach RB nicht auf `sidebarBack`). | Ein A verlässt heute den Bildschirm; höchstes Risiko für Pad-Spieler. | ES-1, AD-1, HM-6, B.3.4 |
| **76.2-b Texte vereinheitlichen** | S | „Speichern und zum Menü“; Spielregeln-Hinweis und Titel „Spielregeln“; „Umschalt-Klick“, „eins ablegen“; Kreativ-Kategorien aus den Handbuch-Namen; Handbuch-Kapitel „Grundlagen“; Belichtung mit Komma; Empfindlichkeit in %; Kennlinie benannt; Hilfetexte ≤ 2 Zeilen; `--stats`-Hinweis streichen; Mods-Titel. Übersetzungen nach CLAUDE.md. | Widersprüche führen in die Irre (AD-2/AD-3) und sind billig zu beheben. | PA-3, AD-2, AD-3, IV-4, IV-7 (Namen), HB-2, ES-2, ES-5, ES-6, MO-2, MP-3, AD-8 |
| **76.2-c HUD hinter Bildschirmen** | S | Während ein Bildschirm offen ist: Chat-Zeilen, Welt-Hinweis, Blockname und Fadenkreuz aus; bei Vollbild-Bildschirmen (Einstellungen, Handbuch, Server verwalten im Spiel) auch Schnellleiste/Herzen; Test im HUD-Layout-Test. | Durchscheinendes HUD kostet Lesbarkeit und überlappt bei 200 %. | PA-1, HB-6, IV-6 (Anzeige), CT-1 |
| **76.2-d Pausenmenü, Tod, Schild** | M | Entwurf D.1 A; Tod auf `ScreenFrame`, gleiche Knöpfe; Schild nach Muster 1 ohne „Fertig“; Größenprüfung `menusUseOnlyTheSizeSteps` auf Tod, Trennung, Meldung ausdehnen; Hook `--overlay message:<text>` für Aufnahmen. | Häufigster Bildschirm im Spiel; Leitfaden-Rest aus 76.0. | PA-2, PA-4, TD-1, SG-1 |
| **76.2-e Inventare und Maschinen auf einen Rahmen** | L | Entwurf D.2 A für Inventar, Werkbank, Truhe, Ofen, Steinschneider, Handel, Zaubertisch, Amboss, Rucksack, Kreativ; Hilfetexte als `KeyBadge`-Zeile/`PadHints`, Platz-Beschriftungen; Pixelschrift nur Runen; Kreativ-Kategorien als `NavList`; Größenprüfung auf `inventory/`. Gegebenenfalls in zwei Teile (Rahmen + 4 Bildschirme, dann der Rest). | Größte Uneinheitlichkeit; lange Texte. | IV-1–IV-9, CT-2 (Hinweise) |
| **76.2-f Handbuch auf Stufen** | M | Alle Texte auf `Ui.type`; Tabelle mit eingeklapptem „Technisches“ (ID, Zustände, Klang); Kopfzeile als `KeyBadge`/`PadHints`; breitere Liste; fehlende Symbole melden; Größenprüfung auf `handbook/`. | Skaliert heute falsch (225 %), technische Rohwerte auf erster Ebene. | HB-1, HB-3, HB-4, HB-5 |
| **76.2-g Neue Welt und Weltoptionen** | M | Entwurf D.3 A; „Inventar beim Tod behalten“ als Schalter-Zeile; Würfel am Seed; Weltoptionen-Zeile umbenannt; Standard-Marke auf Reglern; Karte kleiner auf „Neue Welt“, voll auf „Weltoptionen“; Fortschritt beim Zeichnen. | Spielmodus heute unter dem Rand; Karte dominiert. | NW-1–NW-8 |
| **76.2-h Hauptmenü, Mehrspieler, Mods, Texturpakete** | M | Weltliste ohne Doppel, Zeile fokussierbar mit „…“-Menü (6.9), Kachel mit Welttyp-Symbol, stabile Sortierung; Mehrspieler-Leerzustand einspaltig, Fehler als `Hint` am Knopf; Mods-Leerzustand mit Aktion; Texturpakete mit Reihenfolge, Aktionen im Fuß, deutsche Beschreibungen; Charakter gruppiert mit angeheftetem Fuß; Profil oder Eintrag „Charakter“. | Feinschliff der in 76.0c gebauten Seiten. | HM-1–HM-5, HM-7, MP-1, MP-2, MP-4, MO-1, TP-1–TP-3, CH-1–CH-4 |
| **76.2-i Server verwalten und Kleinkram** | S | Eigene Zeile ohne Aktionen; Übersicht mit „–“ bis zur ersten Messung, Rahmen dezenter; Log-Segmente gleich breit; Befehlsliste kompakt; Spielerliste „–“ statt „? ms“; Bildschirmtastatur QWERTZ; Hook `open:deleteWorldDialog`; Hilfetext zur wirksamen UI-Größe; Grafik-Gruppen umsortieren. | Kleine Fehler, die sonst liegen bleiben. | AD-4–AD-7, CT-2, CT-3, ES-3, ES-4, ES-7 |
| **(später) Helles Schema / Theme-Einstellung** | L | Variante als Einstellung „Farbschema“ unter Oberfläche (A/C), helle Palette mit eigenen Kontrastpaaren. | Wunsch aus 76.0 (§2.3), heute nur per Umgebungsvariable. | Teil 4 |

**Reihenfolge:** a → b → c (zusammen ein halber Tag, sofort spürbar) → d → g → f → h → e (der große Umbau zuletzt, wenn die kleinen Regeln stehen) → i jederzeit dazwischen. Jedes Paket hält die Layout-Tests grün und ergänzt `test_ui_scale.sh`/`test_menu_layout.sh` um die angefassten Bildschirme; die Bilder dieses Berichts dienen als „vorher“.

## 7 Aufnahmen wiederholen

Die Läufe nutzen nur vorhandene Hooks (kein Code geändert). Ein Lauf sieht so aus (Daten in einem eigenen Ordner, `settings.ini` mit `uiScale`):

```sh
QTCRAFT_THEME=C QTCRAFT_WINDOW_SIZE=1280x720 QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_ACCESSIBILITY_CHECK=1 \
QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR=<daten> QTCRAFT_SCREENSHOT=<bild>.png \
QTCRAFT_SCREENSHOT_FRAMES=40 build/debug/src/app/qtcraft.app/Contents/MacOS/qtcraft \
  --no-audio --no-mods --screen builtin --background --language de <Schritte>
```

Schritte je Bildschirm: Startseite (Welten vorher mit `qtcraft-headless --script` wie in `test_menu_layout.sh`); `--overlay "@300 open:newWorldPage"` (Kartenvorschau mit `QTCRAFT_SCREENSHOT_FRAMES=400`), `open:worldOptionsPage`, `open:menuSettingsScreen` plus `pad:dpadUp` und n × `pad:rightShoulder` je Seite, `open:modsScreen`, `open:menuCharacterScreen`, `--overlay multiplayer [--join 127.0.0.1:1]`, `--handbook block:qtcraft:furnace`; im Spiel `--play --world-type flat --view-distance 2` mit `--overlay pause|settings|inventory|creative|furnace|chest|trade|enchanting|anvil|stonecutter|lan|players|chat|sign|character|admin` (Aufbau der Maschinen wie in `test_ui_scale.sh`; Werkbank mit `--chat "/setblock ~ ~-1 ~ crafting_table" --overlay "@2500 key:use"`), Server verwalten mit `--overlay admin --overlay "@2500 pad:dpadUp"` und n + 1 × `pad:rightShoulder`.
