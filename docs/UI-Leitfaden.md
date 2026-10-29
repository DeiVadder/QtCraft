# UI-Leitfaden

Stand 2026-10-22 (Etappe 76.0, entschieden vom Menschen am 2026-10-22, Entscheidungen E-845 und E-852). Gilt für jeden Bildschirm außerhalb der Welt (Hauptmenü, Pause, Einstellungen, Mehrspieler, Mods, Charakter, Handbuch, Dialoge) und für die Paneele über der Welt (Inventar, Behälter, Handel). Das HUD hat eigene Regeln (`Ui.hudPx`, E-725) und ist hier nur am Rand betroffen. Neue Menüs (41 Zaubertisch/Amboss, 45 Render-Modus, 55 Welt- und Server-Optionen, 67 Fortschritte, 77 Server-Oberfläche) bauen auf diesem Leitfaden auf; 76.2 bringt die übrigen Bildschirme (Handbuch, Inventare, Tod, Trennung, Meldung) auf diesen Stand. Umgesetzt sind seit 76.0b die Bausteine (Abschnitt 6), die zentralen Werte (Abschnitt 0), der eigene Steuerelement-Stil und die Einstellungen mit Seitenleiste (Abschnitt 8) samt Pausenmenü, seit 76.0c das Hauptmenü in Variante C (Abschnitt 8a) mit dem Logo in Pixelify Sans und Weltliste, Neue Welt, Mehrspieler, LAN, Mods und Charakter, Einstellungen und Handbuch eine Ebene tiefer in derselben Seitenleiste (Abschnitt 8b) und fensterweit modale Rückfragen (6.0).

## 0 Wo man was zentral ändert

Alle Werte stehen an genau **einer** Stelle im Code; Seiten und Bausteine schreiben keine eigenen Farben, Abstände, Größen oder Schriftgrößen.

| Was | Wo | Name in QML |
|---|---|---|
| Farben (Rollen mit Kontrastprüfung) | `src/clientcore/settings/QcUiPalette.cpp`, Tabelle `palette` (Rolle, QML-Name, ARGB) | `Theme.accent`, `Theme.panel`, … |
| Farben der Qt-Steuerelemente (Basic-Stil: Listen, Felder, Kästchen) | dieselbe Datei, `controlPalette()` (aus den Rollen abgeleitet) | – |
| Abstände, auch zum Fensterrand | `src/app/qml/controls/Ui.qml`, `space`, `screen.margin`, `screen.marginVertical` | `Ui.space.xs` … `Ui.space.xxl`, `Ui.screen.margin` |
| Größen der Steuerelemente | `Ui.qml`, `size` | `Ui.size.row`, `Ui.size.button`, `Ui.size.menuNav`, … |
| Bildschirmbreiten | `Ui.qml`, `screen` | `Ui.screen.s`, `.m`, `.l` |
| Radius | `Ui.qml` | `Ui.radius` |
| Schriftgrößen | `Ui.qml`, `type` | `Ui.type.title`, `.section`, `.group`, `.body`, `.help` |

Werte in `Ui.qml` sind für 100 % in 1024 × 640 geschrieben und laufen durch `Ui.px()` (E-725), stehen also schon in der Größe des Spielers bereit. Eine neue Farbe ist eine neue Rolle in `QcUiPalette` (Enum `Role`, Tabelle, Q_PROPERTY in `QcUiTheme.h`) und bekommt ihr Text-Hintergrund-Paar in `textPairs()`; ein neuer Abstand oder eine neue Größe kommt als benannter Wert nach `Ui.qml`. `test_ui_palette` prüft: keine Farbe außerhalb der Palette in irgendeiner QML-Datei (`screensUseOnlyThePalette`), keine eigene Größe in den Bausteinen (`src/app/qml/controls`), den Einstellungen, dem Hauptmenü mit seinen Seiten und Bildschirmen und dem Pausenmenü, Tod, Trennung und Meldung (`src/app/qml/menus`), dem Schild (`hud/SignEditor.qml`) und im Charakter-Bildschirm (`menusUseOnlyTheSizeSteps`: kein Zahlwert in `px()`/`hudPx()` oder `Qt.size()` und keine Zahl über 1 in einer Eigenschaft oder Deklaration, außer Faktoren wie `2 * Ui.space.l` und Eigenschaften ohne Größe wie `from`, `to`, `value`, `stepSize`, `opacity`, `z`, `model`, Zählungen (`columns`, `columnSpan`, `maximumLength`, `maximumLineCount`), Grenzen eines Validators, Bildraten, Zeiten in ms; `ItemSlot` nimmt den Faktor des HUD oder des Bildschirms und ist ausgenommen; Handbuch-Seite, Inventare und das übrige HUD kommen mit 76.2 dazu).

## 1 Grundsätze

1. **Ein Baustein, ein Aussehen.** Jede Art Bedienelement gibt es genau einmal als Komponente in `src/app/qml/controls/` (Abschnitt 6). Seiten setzen Bausteine zusammen; sie legen keine eigenen Farben, Schriftgrößen oder Abstände fest.
2. **Häufiges zuerst, Seltenes eingeklappt.** Was viele Spieler in den ersten Minuten suchen (Sichtweite, Lautstärke, Maus, Sprache, Oberflächengröße, Anzeigemodus), steht oben auf der ersten Seite seiner Kategorie. Debug und Diagnose stehen unter „Erweitert“.
3. **Gruppieren statt aufzählen.** Mehr als 5 Einstellungen auf einer Seite werden in benannte Gruppen geteilt (Gruppenüberschrift, Abschnitt 6.2).
4. **Spiel-Optik, aber ruhig.** Blockige Formen (kleine Radien, harte Kanten, angedeutete Fase) in der vorhandenen Palette; keine Effekte, die Lesbarkeit kosten.
5. **Jede Seite geht mit Maus, Tastatur und Gamepad**, bei 1024 × 640 und bei 200 % Oberflächengröße, auf Englisch und Deutsch.

## 2 Farben

Alle Farben kommen aus `Theme` (`QcUiPalette`, E-097, E-279). Die vorhandenen Rollen bleiben; der Leitfaden legt fest, wofür sie in Menüs stehen, und ergänzt nur, was für Schaltflächen und Regler fehlt. Jede neue Rolle bekommt ihr Text-Hintergrund-Paar in `textPairs()` (Kontrastprüfung `test_ui_palette`).

### 2.1 Vorhandene Rollen und ihre Bedeutung in Menüs

| Rolle | Wert | Verwendung in Menüs |
|---|---|---|
| `text` | #FFFFFF | Beschriftungen, Werte, Schaltflächentext |
| `textSecondary` | #E2E2E2 | Untertitel von Listenzeilen, Beschreibung unter Auswahl |
| `textMuted` | #B4B4B4 | Hilfetext unter einer Einstellung, Fußnoten (7,5:1 auf dem Paneel) |
| `heading` | #FFD27A | Gruppenüberschriften (Handbuch nutzt es schon für Abschnitte) |
| `link` | #A8D4FF | Verweise, „Mehr dazu im Handbuch“ |
| `warning` | #FFB060 | Hinweis „Warnung“ (Text und linker Balken) |
| `error` | #FF9A90 | Fehlermeldungen, ungültige Eingaben |
| `statusGood`/`statusWarn`/`statusBad` | #50E650 / #E0C030 / #FFB4AC | Zustand als Grafik (gut, Warnung, schlecht; mindestens 3:1 über `hudBackdrop`), z. B. die Signalbalken der Spielerliste (E-886); Werkzeuge behalten ihre eigenen `durability…`-Farben |
| `panel` | #262626 (94 %) | Fläche der Spielbildschirme über der Welt (Inventar, Handbuch) |
| `panelSolid` | #262626 (deckend) | Fläche der Menübildschirme und Dialoge (`ScreenFrame`), damit der Bildschirm darunter nicht durchscheint |
| `panelRaised` | #303030 (63 %) | Gruppenkarte, Listenzeile |
| `panelBorder` | Weiß 50 % | Rand des Bildschirms |
| `separator` | Weiß 25 % | Trennlinie zwischen Gruppen, unter dem Kopf |
| `rowHighlight` | Weiß 13 % | Zeile unter der Maus |
| `focusRing` | #FFD27A | Fokus von Tastatur und Gamepad (überall derselbe Ring) |
| `menuTop`/`menuBottom` | #3D6F9E → #22384F | Hintergrund des Hauptmenüs |
| `scrim`/`scrimStrong` | Schwarz 56/69 % | abgedunkelte Welt hinter Pause und Bildschirmen |

### 2.2 Rollen der Steuerelemente (E-845)

| Rolle | Wert | Zweck | Kontrast des Texts |
|---|---|---|---|
| `accent` | #41CD52 (Qt-Grün) | primäre Schaltfläche, Regler- und Schalter-Füllung, gewählte Kategorie und Segment | `accentText` darauf 8,6:1 (Weiß nur 2,1:1) |
| `accentHover` | #5AD96A | primär unter der Maus | 9,9:1 |
| `accentPressed` | #34A843 | primär gedrückt | 5,8:1 |
| `accentText` | #0B1A0E | Text auf der Hervorhebungsfarbe (fast schwarz, grünstichig) | – |
| `button` | #3A3D42 | Fläche der normalen (sekundären) Schaltfläche | Weiß 10,9:1 |
| `buttonHover` | #4A4F57 | Maus darüber | 8,2:1 |
| `buttonPressed` | #2C2E32 | gedrückt | 13,6:1 |
| `danger` | #A8372E | zerstörende Schaltfläche | 6,5:1 |
| `dangerHover` | #8E2C24 | zerstörend unter der Maus | 8,3:1 |
| `field` | #1A1A1A | Fläche von Auswahllisten, Segmenten, Feldern („eingelassen“) | Weiß 17,4:1 |
| `track` | #7C7C7C | leere Spur von Regler und Schalter | ≥ 3:1 zum Paneel über jeder Welt (WCAG 1.4.11) |
| `knob` | #E8E8E8 | Griff von Regler und Schalter | 3,4:1 zur Spur |
| `knobOutline` | #1A1A1A | dunkler Rand um den Griff, damit er auch auf der Füllung (`accent`) absteht | ≥ 3:1 zu `accent` und `track` |
| `bevelLight` | Weiß 19 % | obere/linke Kante (Fase) | – |
| `bevelDark` | Schwarz 50 % | untere/rechte Kante | – |

**Qt-Grün statt Grasgrün (Entscheidung 4):** Weiß auf #41CD52 erreicht nur 2,1:1 und fällt durch. Gewählt ist darum dunkler Text (`accentText`) auf dem echten Qt-Grün statt eines abgedunkelten Grüns mit weißem Text: die Marke bleibt erkennbar, und auch Hover (heller) und Gedrückt (dunkler) bleiben über 4,5:1. Gold (`heading`/`focusRing`) bleibt Fokusring und Gruppenüberschrift.

Die Fläche `button` hebt sich vom Paneel kaum ab; die Grenze einer Schaltfläche zeigt deshalb die Fase (`bevelLight` oben/links, `bevelDark` unten/rechts, Baustein `Bevel`), nicht die Fläche allein. Die Qt-Steuerelemente des Basic-Stils nehmen die Rollen über `controlPalette()`: `Dark` ist `accent`, `BrightText` ist `accentText` (eine hervorgehobene Basic-Schaltfläche sieht damit wie die primäre aus), `Button` ist `button`, `Base` ist `field`. `HighlightedText` steht auf zwei Flächen – markierter Text auf `Highlight` und die hervorgehobene Zeile einer Liste (`ItemDelegate`) auf `Light` –, für die keine Farbe zugleich auf dem hellen Akzent lesbar ist: `HighlightedText` ist deshalb `text` und `Highlight` ein dunkles Grün (#268032). Aufklapplisten sind `ThemedComboBox`: die hervorgehobene Zeile in `accent` mit `accentText`.

### 2.3 Farbvarianten (E-852)

Drei Varianten (noch keine Einstellung), gewählt beim Start mit `QTCRAFT_THEME=A|B|C` (`QcUiPalette::Preset`, Tabellen `qtDark` und `qtNeon` in `QcUiPalette.cpp`, die nur einzelne Rollen der klassischen Palette ändern). **Standard ist C** (Mensch 2026-10-22, `QcUiPalette::defaultPreset`); C wirkt sehr dunkel und gilt als Dunkelmodus, eine hellere bzw. helle Variante als Einstellung ist Kandidat für 76.2. `test_ui_palette` prüft alle Kontrastpaare für jede Variante.

- **A klassisch:** blauer Verlauf (`menuTop` #3D6F9E → `menuBottom` #22384F), Qt-Grün #41CD52.
- **B wie qt.io:** fast schwarzer Hintergrund (#0C121C → #000000), neutrale Grautöne ohne Blaustich (`button` #3C3C3C, `buttonHover` #4E4E4E, `buttonPressed` #2C2C2C, `buttonDisabled` #303030, `linkHover` grau), Qt-Grün #41CD52.
- **C wie B mit „Qt Neon“** #2CDE85 (`accentHover` #5AE8A2, `accentPressed` #22B36B, dunkler Text #0B1A0E auf allen drei, mindestens 6:1), markierter Text der Qt-Steuerelemente auf Petrol #00414A.

## 3 Abstände und Raster

Alle Maße als benannte Werte aus `Ui` (Abschnitt 0), geschrieben für 100 % in 1024 × 640 (E-725); das HUD weiter mit `Ui.hudPx`.

| Name | Wert | Wofür |
|---|---|---|
| `Ui.space.xs` | 4 | Abstand Label – Hilfetext, Symbol – Text, Zeilen in einer Gruppe |
| `Ui.space.s` | 8 | zwischen Steuerelementen in einer Zeile, Listenzeilen untereinander, Knöpfe im Pausenmenü |
| `Ui.space.m` | 12 | zwischen Label- und Steuerelementspalte, Innenabstand von Listenzeilen und Hinweisen |
| `Ui.space.l` | 16 | Innenabstand von Gruppenkarten, Fuß, Abstand der Seitenleiste |
| `Ui.space.xl` | 24 | zwischen Gruppen, Innenabstand einer Seite |
| `Ui.space.xxl` | 32 | Rand des Hauptmenüs zum Fenster |

- **Zeilenhöhe** einer Einstellung mindestens `Ui.size.row` (40, Fokusziel für Gamepad und Maus); Schaltflächen `Ui.size.button` (32), im Fuß und im Pausenmenü `Ui.size.buttonLarge` (40); Auswahllisten und Segmente `Ui.size.field` (30).
- **Radius:** `Ui.radius` (2) für Schaltflächen, Felder, Karten (blockig); 0 für Slots.
- **Spalten einer Einstellungszeile:** Beschriftung 40 %, Steuerelement 60 % der Inhaltsbreite (`SettingRow`); Wert eines Reglers rechts in fester Breite `Ui.size.value` (72). Dieselbe Teilung auf allen Seiten, damit die Regler über Seitenwechsel hinweg an derselben Stelle stehen.
- **Bildschirmbreiten:** drei Stufen
  - `Ui.screen.s` 480 – Dialoge, Bestätigungen, „Im LAN öffnen“
  - `Ui.screen.m` 760 – Formulare und Listen (Mehrspieler, Mods, Weltoptionen)
  - `Ui.screen.l` 960 – Einstellungen mit Seitenleiste, Charakter, Handbuch (Handbuch darf das Fenster füllen)

  Jeweils `Math.min(fenster − 2 × Ui.screen.margin, Stufe)` (Rand 32), Höhe bis Fenster − 2 × `Ui.screen.marginVertical` (16).

## 4 Schrift

Systemschrift. **Pixelschrift nur für das Logo (Entscheidung 5):** „Pixelify Sans“ (SIL Open Font License 1.1, darf eingebettet und mit dem Spiel weitergegeben werden), eingebettet über die Ressourcen: `src/app/fonts/PixelifySans.ttf` mit `OFL.txt` daneben (auch im macOS-Bundle unter `licenses/`, `THIRD_PARTY.md`, `CREDITS.md`). Das Logo ist der Baustein `GameLogo` (lädt die Schrift selbst, schrumpft auf seine Breite); kein anderer Text nutzt die Schrift. Größen bei 100 %:

| Stufe | Name | Größe | Gewicht | Farbe | Wofür |
|---|---|---|---|---|---|
| Logo | `Ui.type.logo` / `logoSmall` | 64 / 48 | normal (Regular), Pixelify Sans | `text` | Seitenleiste (`logoSmall`): Hauptmenü, Einstellungen, Handbuch |
| Titel | `Ui.type.title` | 24 | fett | `text` | Titel eines Bildschirms (Einstellungen, Pause) |
| Abschnitt | `Ui.type.section` | 18 | fett | `text` | Seitentitel in den Einstellungen, Spaltenüberschrift |
| Gruppe | `Ui.type.group` | 14 | fett | `heading` | Gruppenüberschrift in Einstellungen |
| Text | `Ui.type.body` | 14 | normal | `text` | Beschriftungen, Werte, Schaltflächen |
| Hilfe | `Ui.type.help` | 12 | normal | `textMuted` | Hilfetext, Fußnoten (nie kleiner) |
| Schild | `Ui.type.badge` | 11 | fett | `textSecondary`/`heading` | Tasten-Schild, „ab Weltstart“ |

## 5 Seitenvorlage

Jeder Bildschirm hat dieselben vier Teile:

1. **Kopf:** Titel links (Stufe „Titel“), rechts „Schließen“ mit der Taste als Schild (`KeyBadge`: „Esc“, mit Pad das B-Symbol), klickbar; kein „×“, kein „Fertig“ (Baustein `ScreenHeader`, Regel für alle Bildschirme über der Welt, Menue-Pruefung E.0 Nr. 1). Bei Unterseiten links „‹ Zurück“. Pausenmenü und Tod haben keinen „Schließen“-Knopf: „Weiterspielen“ (mit dem Schild „Esc“/B) bzw. „Wiederbeleben“ sind ihr Weg zurück (8e). Einstellungen und Handbuch haben keinen Kopf: Titel und Rückweg stehen in der Seitenleiste (8b).
2. **Navigation** (nur bei mehreren Seiten): Seitenleiste links (Entscheidung 2), bei Einstellungen und Handbuch eine Ebene tiefer in derselben Seitenleiste (8b).
3. **Inhalt:** scrollt als Ganzes (`ThemedScrollBar`), Kopf und Fuß bleiben stehen. Gruppen untereinander, Abstand `xl`.
4. **Fuß:** Aktionen rechtsbündig, die primäre ganz rechts; seitenbezogene Aktionen („Standard wiederherstellen“, „Ordner öffnen“) links. Darunter, wenn ein Pad benutzt wird, die Pad-Hinweise (`PadHints`).

Regeln:

- **Nur zwei Muster für Änderungen** (Mensch 2026-10-22, E-852):
  1. **Sofort wirksam, ohne Bestätigungsknopf:** jede Änderung gilt sofort (Einstellungen; Zurücksetzen je Wert), der Bildschirm schließt mit dem Rückweg-Eintrag der Seitenleiste („‹ Hauptmenü“, „‹ Pausenmenü“, 8b) bzw. Esc/B. Ausnahme: Anzeigemodus und Monitor (74.3) – nach dem Wechsel 10 s Rückfrage „Beibehalten?“ mit Rückfall.
  2. **Bearbeiten, dann Speichern oder Verwerfen:** die Seite bearbeitet einen Entwurf (die Vorschau zeigt ihn sofort), „Speichern“ (primär, nur aktiv, wenn der Entwurf vom Gespeicherten abweicht) und „Verwerfen“; solange etwas ungespeichert ist, steht „Ungespeicherte Änderungen“ sichtbar da. Wer die Seite mit Änderungen verlässt (Esc/B, anderer Eintrag der Seitenleiste, „Zurück“, „Schließen“, Weltstart), wird gefragt: Speichern / Verwerfen / Weiter bearbeiten (Baustein `UnsavedChangesDialog`, Fokus und sichere Wahl „Weiter bearbeiten“). Beispiel: Charakter (Seite im Hauptmenü und Bildschirm im Spiel).

  **Nie** ein einzelnes „Fertig“/„OK“, das Änderungen ohnehin übernimmt. Das Schild (`SignEditor`, 76.2-d) folgt Muster 1: jede Zeile gilt beim Tippen (`GameController.sign.edit`), „Schließen“, Esc, Enter in der letzten Zeile und jedes andere Schließen (anderer Bildschirm) senden den Text; beim Tod geht der getippte Text verloren, der Server lehnt Änderungen toter Spieler ab. Die Bildschirmtastatur des Pads behält „Fertig“: es beendet die Eingabe in ein Feld, kein Bildschirm.
- Wirkt eine Einstellung erst später („ab dem nächsten Weltstart“, „ab dem nächsten Start“), steht das als Hilfetext **an der Einstellung**, nicht als Fußnote am Seitenende.
- Leere Listen zeigen einen Satz, was hineinkommt, und die Aktion, die es ändert („Noch keine Welten. [Neue Welt]“).
- Pfade (Log-Ordner, Texturpakete, Mods) nicht als langer Fließtext; Schaltfläche „Ordner öffnen“ und daneben oder darunter der Pfad als auswählbarer Text, der an jeder Stelle umbricht (Tooltips sind Zusatz, nie die einzige Stelle).

## 6 Bausteine

QML-Komponenten in `src/app/qml/controls/`. Jede setzt `Accessible.name`/`role`, nimmt Farben aus `Theme`, Maße aus `Ui`, und hat feste Wertemengen als QML-`enum` (E-688). Dazu die Hilfsbausteine `Bevel` (Fase), `FocusOutline` (Tastaturfokus, gold, 2 px außen), `GameSwitch` (der Schalter allein, für Listenzeilen), `ScreenFrame` (Fläche jedes Bildschirms und Dialogs), `GameTextField` (Textfeld, eingelassen wie die Aufklappliste, goldener Rahmen mit Fokus; jedes Textfeld der Menüs), `Segments` (die Segmente von 6.6 ohne Einstellungszeile, z. B. Seite und Bewegung der Charakter-Vorschau), `NavButton` (Eintrag einer Seitenleiste, 6.11) und `GameLogo` (Abschnitt 4). Die Einstellungsseiten bauen auf `settings/SettingsPage.qml` (Titel, Gruppen, scrollt als Ganzes).

### 6.0 `ModalDialog`, `UnsavedChangesDialog`
**Dialoge, die eine Antwort verlangen, sind modal für das ganze Fenster** (Mensch 2026-10-22, E-852): mittig über dem Fenster (nicht über der Seite, die sie öffnet), `scrimStrong` über allem darunter, die Seitenleiste eingeschlossen; nichts darunter nimmt Klick, Taste oder den Pad-Fokus, bis der Dialog schließt. Baustein `ModalDialog` (`Dialog` im `Overlay.overlay` des Fensters, `Overlay.modal` = `scrimStrong`, Breite `Ui.screen.s`, Fläche `ScreenFrame`); jede Rückfrage im Menü und im Spiel baut darauf: `UnsavedChangesDialog`, `DeleteWorldDialog`. Test: `test_modal_dialog` (Mitte des Fensters, kein Klick und kein Pad-Schritt erreicht die Seitenleiste, Tab bleibt im Dialog).

`UnsavedChangesDialog`: Rückfrage beim Verlassen mit ungespeicherten Änderungen (Muster 2, Abschnitt 5): „Verwerfen“ (zerstörend), „Weiter bearbeiten“ (Fokus, auch Esc/B), „Speichern“ (primär); `ask(proceed)` führt `proceed` nach Speichern oder Verwerfen aus.

### 6.1 `GameButton`
- Gesperrt (`enabled: false`) ist jede Art flach und grau (`buttonDisabled`, `textDisabled`, ohne Fase), damit eine gesperrte primäre Schaltfläche nicht wie die Hervorhebung aussieht.
- `kind: GameButton.Primary | Secondary | Destructive | Quiet` (QML-`enum`, kein String).
- Primary: `accent`, fett; höchstens **eine** pro Bildschirm (die wahrscheinlichste nächste Aktion: „Weiterspielen“, „Beitreten“, „Speichern“, „Zurück zum Spiel“).
- Secondary: `button`. Destructive: `danger`, nur wenn danach eine Rückfrage kommt oder es unumkehrbar ist. Quiet: nur Text (z. B. „‹ Zurück“, „Mehr …“).
- Fase: 1 px `bevelLight` oben/links, 2 px `bevelDark` unten/rechts; gedrückt umgekehrt (blockiger Look). Fokus: 2 px `focusRing` außen.
- Text immer ein Verb oder Ziel („Welt erstellen“, „Ordner öffnen“, „Speichern und zum Menü“); Tastenkürzel als kleines Schild rechts im Knopf (`KeyBadge`), nicht in Klammern im Text. Der Text bleibt dabei in der Mitte des Knopfes: das Schild sitzt am rechten Rand, der Text hält seinen Platz auf beiden Seiten frei (76.2-d, PA-4).

### 6.2 `SettingGroup`
Überschrift (Stufe „Gruppe“, `heading`), darunter die Zeilen auf `panelRaised` mit Innenabstand `l`. Eine Hilfe, die die ganze Gruppe betrifft (Bedienung der Tastentabelle), steht als `help` (Stufe „Hilfe“, `textMuted`) zwischen Überschrift und Karte, nicht als Fließtext in der Karte; eine Tabelle in der Karte hat Spaltenköpfe (Stufe „Hilfe“, fett, `textSecondary`). Einklappen (`collapsible`, „▸ Erweitert“) ist vorgesehen, aber noch nicht gebaut: keine Seite braucht es bisher.

### 6.3 `SettingRow`
Zeile mit `label`, optional `help` (eine Zeile, `textMuted`, unter dem Label, darf umbrechen), optional `badge` (z. B. „nach einem Neustart“), optional `key` (Taste als `KeyBadge` neben dem Label), rechts das Steuerelement. Zeilenhöhe ≥ 40. Fokusziel ist das Steuerelement der Zeile; Links/Rechts verstellen einen Regler (Gamepad und Pfeiltasten), A/Enter öffnet Listen und schaltet Schalter. `enabled: false` dimmt die Zeile und nennt im Hilfetext den Grund („Nur mit VSync aus“); Knöpfe und Schalter einer gedimmten Zeile dimmen sich nicht noch einmal. `dimmed` dimmt die Zeile, ohne sie zu sperren (die Lautstärken, solange stumm geschaltet ist).

### 6.4 `SettingSlider`
Regler mit Wert rechts in fester Breite, Einheit im Wert („12 Chunks“, „70°“, „80 %“). Feste Stufen mit `snapMode`. Hat der Wert einen Standard (`defaultValue`), steht an seiner Stelle eine kleine Marke auf der Spur (Weltoptionen, E-930). Gefüllter Teil `accent`, Spur `track`, Griff eckig (Block). Für Zahlen ohne Bedeutung Prozent (Mausempfindlichkeit: Anteil am höchsten Wert) oder ein benannter Wert („linear“ bei der Kennlinie des Pads).

### 6.5 `SettingSwitch`
Ein-/Aus-Einstellungen sind **Schalter** (`Switch`), nicht Kästchen; die Beschriftung steht links wie bei allen Zeilen. Kästchen (`SettingCheck`) nur in Mehrfachauswahlen innerhalb einer Gruppe (z. B. Debug-Visualisierungen, die Bauwerks-Arten der Kartenlegende); auch in Formularen ist Ein/Aus ein Schalter („Inventar beim Tod behalten“, E-930). Leertaste und Enter schalten Schalter und Kästchen. (Entscheidung 6)

### 6.6 `SettingChoice` (Segmente)
2–4 kurze Möglichkeiten nebeneinander als Segmente (Anzeigemodus, Texturfilterung, Ansicht), mit Platz alle gleich breit (so breit, wie der längste Text fett braucht), mit wenig Platz zur eigenen Breite geschrumpft (E-931). Ab 5 Möglichkeiten oder langen Texten `SettingDropdown`. Segmente zeigen alle Möglichkeiten auf einen Blick; die Pfeiltasten Links/Rechts wählen das Nachbarsegment, das Pad bewegt den Fokus und bestätigt mit A.

### 6.7 `SettingDropdown`
`ThemedComboBox` (`field`, Fase eingelassen; hervorgehobene Zeile der Liste in `accent` mit `accentText`) in einer Einstellungszeile; Aufklapplisten außerhalb der Einstellungen sind ebenfalls `ThemedComboBox`. Für Sprache, Monitor, Bildraten-Limit, Oberflächengröße, Controller-Profil.

### 6.8 `Hint`
`kind: Info | Warning | Error`; Kasten mit 3 px Balken links in `link`/`warning`/`error`, Text `text`. Für Hinweise, die eine Gruppe betreffen (z. B. „Mods sind in dieser Sitzung abgeschaltet“). Nie nur Farbe: Warnung und Fehler haben ein Symbol (!, ×).

### 6.9 `ListRow`
Für Welten, Server, Mods, Texturpakete: Bild/Symbol links (optional), Titel (fett) und eine Zeile Untertitel (`textSecondary`), Aktionen rechts (höchstens eine primäre). `interactive: true` lässt die Zeile unter der Maus aufhellen und meldet `clicked()`/`doubleClicked()` (Server: Klick übernimmt die Adresse, Doppelklick tritt bei; Welten: Doppelklick spielt). `focusable: true` macht die Zeile selbst zum Fokusziel (Enter, Leertaste, A klicken sie; Server-Zeilen). Den Fokus nehmen die Aktionen der Zeile (Schalter, Knöpfe); eine ganz fokussierbare Zeile mit Doppelklick/A als Hauptaktion und ein „…“-Menü für weitere Aktionen sind für 76.2 vorgesehen. Lange Listen (die rund 40 Befehle in „Server verwalten“) nehmen `compact: true`: Titel und Untertitel in einer Zeile, der Untertitel abgeschnitten und ganz, solange die Zeile den Fokus hat (E-931). Zeilen untereinander stehen mit Abstand `Ui.space.s` (Welten, Texturpakete, Spieler in „Server verwalten“), kompakte mit `Ui.space.xs`; sie berühren sich nie und liegen auf der Seite oder einem eingelassenen Feld, nicht auf der Karte einer `SettingGroup` (gleiche Fläche, die Zeilen liefen ineinander).

### 6.10 `KeyBadge`
Taste oder Pad-Symbol (`PadGlyph`) als kleines Schild, z. B. neben „Handbuch“ das „H“. Ersetzt „Handbuch (H)“, „Beschriftungen in der Welt anzeigen (L)“.

### 6.11 `NavList` (Seitenleiste)
Kategorien als Liste links (Einträge `NavButton`, Text, optional Tasten-Schild oder eine leise Zahl rechts, `notes`: die Einträge je Kapitel im Handbuch), gewählte Kategorie mit `accent`-Balken links und `panelRaised`; LB/RB (Gamepad, `gamepadCommand` des Bildschirms) und Strg+Tab / Strg+Umschalt+Tab wechseln die Kategorie (auf dem Mac die Control-Taste, die Qt als Meta meldet; Shortcuts der `NavList`, die vor dem fokussierten Steuerelement greifen, und `NavList.takeCommand`). Hoch/Runter wandern durch die Einträge wie in der Seitenleiste des Hauptmenüs.

### 6.12 `LineChart` (Diagramm, E-863)
Kleines Liniendiagramm eines Werts über die Zeit (Übersicht von „Server verwalten“): oben Titel (Stufe „Hilfe“, `textSecondary`) und der aktuelle Wert mit Einheit (fett), darunter die Fläche `panelRaised` mit leisem Rahmen `separator`, Höhe `Ui.size.chart`; bis zur ersten Messung (`measured: false`) zeigt es „–“ als Wert und „Noch keine Messung“ in der Fläche statt einer Linie aus Nullen; die Linie in `accent`, die neueste Messung rechts, `capacity` Werte füllen die Breite (`AdminController.chartCapacity`: 600 Sekunden, 1 440 Minuten). Zahlen der Grenzen und Längen kommen aus C++ (`ticksPerSecond`, `tickBudgetMs`, `nameLength` …), nie als Literal in QML. Eine Grenze (`limit`, z. B. 20 Ticks je Sekunde) ist eine eigene Linie in `graphLimit`. Die Skala rundet den höchsten Wert auf 1, 2 oder 5 mal eine Zehnerpotenz und steht klein oben links. Gezeichnet von `DebugGraph` im Szenengraph (keine Canvas). `Accessible.role` ist `Chart`, der Name Titel und Wert. Mehrere Diagramme stehen in einem Raster mit zwei Spalten; der Zeitraum ist ein `Segments` darüber.

## 7 Beschriftung

- **Satzanfang groß, sonst wie im Satz** („Größe der Oberfläche“, nicht „Größe Der Oberfläche“); englisch ebenso (sentence case).
- **Schaltflächen sagen, was passiert:** Verb + Objekt („Welt löschen“, „Server beitreten“); Ausnahmen „Schließen“, „Abbrechen“, „Zurück“, „Speichern“, „Verwerfen“. Nicht „OK“/„Ja“ in Dialogen, sondern die Aktion („Löschen“).
- **Ein Wort, eine Sache:** „Schließen“ für Bildschirme, deren Änderungen sofort gelten (LAN; die Einstellungen schließen mit dem Rückweg-Eintrag, 8b), „Speichern“/„Verwerfen“ für Entwürfe (Charakter); kein „Fertig“ (Abschnitt 5).
- **Keine Erklärungen in Klammern im Label:** „Passwort (nur, wenn der Server eines hat)“ → Label „Passwort“, Hilfetext „Nur nötig, wenn der Server eines hat“. Ebenso „Port (0: ein beliebiger freier Port)“.
- **Einheiten am Wert**, nicht am Label. Zahlen im deutschen Format (Komma).
- **Du-Form**, kurz, ohne Fachwörter auf der ersten Ebene („Chunks“ ist im Spiel eingeführt und bleibt; „Mip-Stufen“, „Atlas“ nur unter „Erweitert“).
- **Länge:** Deutsche Texte sind 30–40 % länger. Jede Zeile wird auf Deutsch bei 1024 × 640 und 200 % geprüft (Layout-Test). Labels ≤ 28 Zeichen, Hilfetexte ≤ 2 Zeilen; Segmente ≤ 14 Zeichen je Möglichkeit.
- **Inhaltstexte aus Paketen** (Texturpaket- und Mod-Beschreibungen) kommen übersetzt aus den Sprachdateien des Pakets; fehlt die Übersetzung, die englische ohne Hinweis.

## 8 Reihenfolge und Gliederung der Einstellungen

Umgesetzt in `src/app/qml/settings/` (`SettingsScreen.qml` mit `enum Page`, eine Ebene tiefer in der Seitenleiste, Abschnitt 8b; je Kategorie eine Seite: `GraphicsPage`, `AudioPage`, `ControlsPage`, `GamepadPage`, `InterfacePage`, `TexturePacksPage`, `AdvancedPage`). Die Overlays des Test-Hooks `--overlay` öffnen die Seiten: `settings` und `display` Grafik, `audio`, `controls`, `gamepad`, `interface`, `texturepacks`, `debug` Erweitert (`SettingsScreen.pageOfOverlay`).

Kategorien (Seitenleiste), in dieser Reihenfolge; in jeder Kategorie die Gruppen von häufig nach selten:

1. **Grafik:** *Anzeige* (Anzeigemodus, Monitor, VSync, Bildraten-Limit, Limit im Menü und im Hintergrund – 74.3) · *Welt* (Sichtweite, Sichtfeld) · *Bewegung und Komfort* (Sichtfeld beim Sprinten, Kamerawackeln) · *Qualität* (Texturfilterung; 45 Render-Modus) · später *Erweitert* eingeklappt.
2. **Audio:** Gesamtlautstärke, Stumm (als Schalter direkt darunter; dimmt die Regler), Musik, Effekte, Umgebung.
3. **Steuerung:** *Maus* (Empfindlichkeit, Umkehren) · *Tastenbelegung* (die Tabelle).
4. **Controller:** wie heute, aber gegliedert in *Profil* · *Sticks und Trigger* · *Tasten*.
5. **Oberfläche:** *Sprache und Größe* (Sprache, Größe der Oberfläche mit der wirksamen Größe als Hilfe, wenn das Fenster sie begrenzt: „Wirksam: 113 % (begrenzt durch die Fenstergröße)“) · *Ansicht* (Kamera, Beschriftungen in der Welt, Hand beim Ausblenden der Anzeige, Pause bei Fokusverlust).
6. **Texturpakete.**
7. **Erweitert:** Debug-Anzeige und Visualisierungen, Protokoll und Absturzberichte.

Jede heutige Einstellung hat damit genau einen Platz; neue Einstellungen (41, 45, 55, 67) werden einer Kategorie und Gruppe zugeordnet, bevor sie gebaut werden. Welt- und Serveroptionen (55) gehören **nicht** in die Einstellungen, sondern zur Welt („Neue Welt“ → „Weitere Optionen“, im Spiel Pause → „Server verwalten“ → „Spielregeln“, 8c).

Ein „Schnell“-Bereich ist nicht nötig, wenn die erste Kategorie Grafik → Anzeige ist und die Seitenleiste alles in einem Schritt erreicht.

## 8a Hauptmenü (Variante C, E-852)

Umgesetzt in `src/app/qml/menus/` (`MainMenu`, `MenuSidebar`, `HomePage`, `ContinueCard`, `WorldRow`, `WorldTile`, `NewWorldPage`, `MultiplayerPage`, `CharacterPage`, `ModsPage`, `DeleteWorldDialog`, `ProfileCard`) und `character/CharacterEditor.qml`.

- **Seitenleiste** links (`Ui.size.menuNav`, auf `scrim`): Logo, dann Spielen (die Startseite, gewählt) · Mehrspieler · Charakter · Handbuch (Tasten-Schild) · Mods & Pakete · Einstellungen; unten das Profil (kleine Figur, Name, „Charakter anpassen“, öffnet den Charakter) und Beenden. Hoch/Runter wandern durch die Einträge; der Eintrag der gezeigten Seite ist markiert.
- **Startseite** rechts davon im Rand `Ui.screen.margin`, höchstens `Ui.screen.l` breit: nach einem Absturz der Hinweis mit „Log-Ordner öffnen“; die Karte „Zuletzt gespielt“ (Kachel mit Anfangsbuchstaben, Name, „Heute, 21:14 · Normal · Überleben“, primär **Weiterspielen**, dort beginnt der Pad-Fokus); „Deine Welten“ mit „Neue Welt …“ und allen Welten, die zuletzt gespielte zuerst (Spielen, Löschen; Doppelklick spielt; eine Welt, die sich nicht öffnen lässt, nennt den Grund in `error`).
- **Erststart:** statt der Karte „Willkommen! Erstelle deine erste Welt.“ mit primärem **Neue Welt erstellen**; keine leere Liste.
- **Neue Welt** ist eine eigene Seite (scrollt als Ganzes) mit „‹ Zurück“ und primär **Erstellen und spielen** im Fuß; Esc/B führt zurück (E-930, Entwurf D.3 A der Menü-Prüfung): links die Gruppen „Welt“ (Name, Welttyp, Seed mit Würfel „Zufällig“; das leere Feld zeigt den Zufalls-Seed als Platzhalter, die Karte immer den Seed des Felds), „Regeln“ (Spielmodus, Schalter „Inventar beim Tod behalten“) und bei „Normal“ „Weltgenerierung“ (Zeile „Weltoptionen“, Hilfetext die Zusammenfassung, „Anpassen …“ am Anfang der Steuer-Spalte); rechts eine kleine Kartenvorschau (`Ui.size.map`) mit den sechs häufigsten Biomen und „+ n weitere“, ohne Zoom und Dörfer. Ist die Seite schmaler als `Ui.screen.s` plus Kartenspalte (1024 × 640), einspaltig mit der Karte am Ende, unter der Weltgenerierung. Der Spielmodus steht bei 1280 × 720 ohne Scrollen im Bild (`test_ui_scale.sh`, `test_menu_layout.sh`). **Weltoptionen** (`WorldOptionsPage`) zeigt die volle Karte (Höhe der Seitenansicht, wächst nach rechts, Legende, Zoom, Dörfer und Bauwerke je Art) und die Regler mit Standard-Marke.
- **Mehrspieler, Charakter, Mods & Pakete sind Seiten** im selben Bereich (kein Bildschirm über der Startseite, kein „Schließen“); die Seitenleiste markiert die gezeigte. Mehrspieler: zuletzt genutzte Server links, das Formular rechts, darunter „Im lokalen Netzwerk“. „Mods & Pakete“ zeigt die Mods; deren Fuß führt mit „Texturpakete …“ in die Einstellungen (Entscheidung 8). Esc/B führt von jeder Seite zur Startseite.
- **Handbuch und Einstellungen** führen die Seitenleiste eine Ebene tiefer (Abschnitt 8b), kein Bildschirm über dem Menü.

## 8b Einstellungen und Handbuch: eine Ebene tiefer in der Seitenleiste (E-852)

Mensch 2026-10-22, wie die Systemeinstellungen von macOS: **eine** Seitenleiste, die ihre Einträge wechselt (Drill-down), kein Bildschirm über dem Hauptmenü. Baustein `SidebarScreen` (`controls/`, auf `SideBar`, das auch `MenuSidebar` trägt), im Hauptmenü und im Spiel dieselben Komponenten (`SettingsScreen`, `HandbookScreen`).

- **Seitenleiste** an derselben Stelle und so breit wie im Hauptmenü (`Ui.size.menuNav`): oben das Logo (bleibt, damit die Leiste nicht springt), darunter der **Rückweg-Eintrag**, der sein Ziel nennt, dann der Titel („Einstellungen“, „Handbuch“, Stufe „Abschnitt“), dann die Kategorien. Profil und Beenden des Hauptmenüs entfallen eine Ebene tiefer (sie gehören zum Hauptmenü, nicht zu den Kategorien).
- **Rückweg-Eintrag:** „‹ Hauptmenü“ im Hauptmenü, „‹ Pausenmenü“ im Spiel aus dem Pausenmenü, „‹ Zurück zum Spiel“ direkt aus der Welt (Taste H, Taste O für die Debug-Optionen), „‹ Zurück“ über einem anderen Bildschirm (Handbuch aus dem Inventar). Esc und B des Pads tun dasselbe. Zurück im Hauptmenü hat der Eintrag „Einstellungen“ bzw. „Handbuch“ wieder den Fokus.
- **Einstellungen:** die sieben Kategorien aus Abschnitt 8, rechts die Seite im Rand `Ui.screen.margin`, höchstens `Ui.screen.l` breit; kein Fuß mehr mit „Schließen“ (der Rückweg-Eintrag ersetzt ihn), Änderungen gelten sofort (Muster 1). LB/RB und Strg+Tab wechseln die Kategorie.
- **Handbuch:** in der Seitenleiste unter dem Titel die Suche (`GameTextField`, Enter öffnet den ersten Treffer, Runter springt in die Liste) und die Kapitel mit ihrer Zahl an Einträgen (`NavList.notes`; die Liste scrollt, wenn das Fenster zu niedrig ist, und folgt dem Fokus); während einer Suche ist kein Kapitel markiert, ein gewähltes Kapitel beendet die Suche. Rechts oben „‹ Vorherige Seite“ (leise, Rücktaste, Alt+Links) mit dem Bedienhinweis, darunter die Einträge (`Ui.size.handbookEntries`) und die Seite (`HandbookPage`). LB/RB wechseln das Kapitel.
- **Hintergrund:** im Hauptmenü dessen Verlauf (nichts vom Menü scheint durch), im Spiel das fast deckende `panel` über der Welt (weder Welt noch HUD scheinen durch den Text), die Seitenleiste darauf auf `scrim`.
- Test-Hooks bleiben: `open:menuSettingsScreen` (Einstellungen im Hauptmenü), die Overlays `settings`, `display`, `audio`, … (im Spiel), `--handbook <ziel>`.

## 8c Server verwalten (E-863)

Umgesetzt in `src/app/qml/admin/` (`AdminScreen` und je Abschnitt eine Seite). Pausenmenü → „Server verwalten“ (nur für Operatoren) führt eine Ebene tiefer wie die Einstellungen (8b), Rückweg „‹ Pausenmenü“. Abschnitte: Übersicht (Diagramme 6.12), Spieler, Chat, Befehle, Listen, Spielregeln, Log, Server. Muster: Listen und Spieleraktionen gelten sofort (Muster 1), Spielregeln und Grenzen sind ein Entwurf mit „Speichern“/„Verwerfen“ (Muster 2), Rauswerfen, Sperren und Stoppen fragen modal (`AdminActionDialog`, Fokus auf „Abbrechen“, die Aktion destruktiv und nach sich selbst benannt). Antworten und Ablehnungen des Servers stehen als `Hint` über dem Abschnitt. Die Spielregeln zeigen den übersetzten Namen (`QcAdminSettingNames`) als Bezeichnung, den Schlüssel als Schild (`badge`) darunter und Beschreibung, Bereich und Standard als Hilfe; der Entwurf liegt im Controller und überdauert das Schließen (Leiste „ungespeichert“). Die eigenen Operator-Rechte zu entziehen fragt modal. Die Zeile des Gastgebers hat keine Aktionen, nur den Hinweis „Du stellst die Welt bereit“ (für andere Operatoren „Stellt die Welt bereit“); die eigene Zeile eines Operators bietet kein Rauswerfen und Sperren (E-931). Hat eine Seite nichts, was den Fokus nimmt, steht der Pad-Fokus auf ihrem Eintrag in der Seitenleiste. Listen von Zeilen (Chat, Log, Ausgabe) liegen eingelassen auf `field` und scrollen für sich, die Eingabezeile steht darunter; Warnungen und Fehler mit Farbe und Zeichen („!“, „×“). Layout-Test: `tests/ui/test_admin_layout.sh`.

## 8d HUD hinter Bildschirmen (E-927)

Was vom HUD hinter einem Bildschirm bleibt, entscheidet nur `GameController.hudShown` (`QcScreenState::hudShown`); kein Bildschirm blendet HUD-Teile selbst aus.

- **Jeder Bildschirm über der Welt** (Inventare, Maschinen, Pausenmenü, Charakter, LAN, Schild) **und der Todesbildschirm**: Chat-Zeilen, Welt-Hinweis, Blockname, Fadenkreuz, Karte in der Hand, Name eines neu gewählten Items (der eigene Name des Stapels an der Schnellleiste bleibt), Beschriftungen in der Welt (Showcase-Namen, Lichtwerte) und die Fortschritts-Mitteilung aus; Schnellleiste, Erfahrung, Herzen, Hunger, Rüstung, Tageszeit und Effekte bleiben (wie im Vorbild die Schnellleiste hinter dem Inventar).
- **Vollbild-Bildschirme** (Einstellungen, Server verwalten, Fortschritte, Handbuch – auch über einem anderen Bildschirm): das ganze HUD aus. Ein neuer Bildschirm mit fast deckendem Paneel über die ganze Fläche kommt in `QcScreenState::isFullScreen`.
- **Chatzeile:** alles bleibt außer dem Welt-Hinweis unter der Eingabe.
- Geprüft von `test_hud_layout.sh` (Zeile „HUD parts shown“ des Layout-Checks, 100 und 200 %).

## 8e Pausenmenü, Tod, Trennung, Meldung (76.2-d, Entwurf D.1 A)

Umgesetzt in `src/app/qml/menus/` (`PauseMenu`, `DeathScreen`, `DisconnectScreen`, `MessageScreen`, gemeinsam `LeaveGameButton`).

- **Eine Karte** (`WorldCard`, Fläche `ScreenFrame`), Breite `Ui.screen.s`, Innenrand `xl`, über der abgedunkelten Welt (`scrim`, beim Tod `deathScrim`), mittig im Raum **über den Statuszeilen des HUD** (Schnellleiste, Herzen; sie bleiben hinter Pause und Tod sichtbar, 8d) bzw. über den Pad-Hinweisen. Ist die Karte höher als dieser Raum (200 % im Fenster 1280 × 720, 225 %), scrollt ihr Inhalt in der Karte; das Element mit dem Fokus (Tab, Pad) wird hineingescrollt.
- **Kopf ohne „Schließen“:** Titel links (Stufe „Titel“), darunter bei Bedarf eine Zeile in `textMuted`/„Hilfe“ (warum die Welt weiterläuft; „Dein Inventar bleibt erhalten.“).
- **Pausenmenü in Gruppen, Abstand `xl`:** „Weiterspielen“ (primär, Schild „Esc“ bzw. B, Pad-Fokus startet dort) · die Bildschirme des Spiels in zwei Spalten (Einstellungen, Handbuch, Charakter, Fortschritte) · die Welt (Welt speichern, Im LAN öffnen, Server verwalten; nur die zutreffenden, ein einzelner letzter Knopf füllt die Zeile) · unter einer Trennlinie (`separator`) abgesetzt der Weg hinaus. Knöpfe `buttonLarge`, in einer Gruppe Abstand `s`, zwei nebeneinander gleich breit.
- **Weg hinaus überall gleich** (`LeaveGameButton`, sekundär, volle Breite): „Speichern und zum Menü“ (gespeicherte Welt), „Zum Menü“ (nicht gespeichert), „Verbindung trennen“ (Netzwerkspiel).
- **Tod:** dieselbe Karte, „Wiederbeleben“ primär und der Weg hinaus sekundär, beide so breit wie die Knöpfe des Pausenmenüs; Esc/B tun nichts.
- **Trennung:** „Zurück zum Menü“ primär, „Erneut beitreten“ (Netzwerkspiel) sekundär darunter. **Meldung** (Laden, Fehler): der Text in Stufe „Abschnitt“, darunter „Zurück zum Menü“ bzw. „Abbrechen“. Aufnahmen mit `--overlay message:<text>`.
- Tests: `test_ui_scale.sh` (Pause, Tod, Schild, Meldung bei 100 % und 200 %, Pad-Fokus im Pausenmenü), `test_pause_screens.sh` (Knöpfe in gespeicherter Welt und als Gast ohne Operator-Rechte, Karten über den Statuszeilen bei 200 % und 225 %, Schild über die Bildschirmtastatur), `test_ui_palette` (`menusUseOnlyTheSizeSteps`).

## 9 Mindestgröße, Skalierung, Scrollen

- Jede Seite muss bei **1024 × 640** und **200 %** (begrenzt auf das, was passt, E-725) ohne abgeschnittene Elemente auskommen; der Inhalt scrollt, Kopf und Fuß nicht.
- Die Seitenleiste ist überall `Ui.size.menuNav` (224) breit; Kategorien, die nicht in die Höhe passen, scrollen (Handbuch).
- Layout-Tests: jede neue Seite kommt in `tests/ui/test_ui_scale.sh` und `test_menu_layout.sh` (Overlay oder `open:`), Deutsch eingeschlossen (`--language de`).

## 10 Gamepad und Tastatur

- Fokusreihenfolge = Lesereihenfolge: Kopf → Navigation → Inhalt von oben nach unten → Fuß. Der Fokus startet auf der primären Aktion (Dialoge) bzw. der ersten Einstellung (Einstellungen).
- Seitenleisten-Bildschirme (`SidebarScreen`: Einstellungen, Handbuch, Server verwalten): beim Öffnen und nach jedem LB/RB steht der Fokus auf dem ersten Element der Seite (`focusPage()`). Rückweg-Eintrag (`NavButton.padLeftOnly`): Pad: B, oder Hoch von der ersten Kategorie (auch Links); nie Startpunkt, nie mit Runter (E-925). Die Pad-Hinweise zeigen „LB RB Kategorie“ nur dort, „Y zurücksetzen“ nur auf Seiten mit Standardwerten (`SettingsPage.resettable`, heute Steuerung und Controller); Y fragt einmal nach („Y erneut: zurücksetzen“, 3 s).
- Listen wie die Weltliste: die Zeile ist das Fokusziel, Hoch/Runter wandern Zeile für Zeile, Rechts erreicht ihre Knöpfe, A spielt.
- A/Enter = auswählen, B/Esc = zurück/schließen, LB/RB = Kategorie wechseln, Links/Rechts = Wert ändern, Y = auf Standard zurücksetzen (mit Hinweis in den Pad-Hinweisen).
- Fokus immer mit `FocusRing` (`focusRing`, 2 px, außerhalb des Elements), nie nur über eine Farbänderung.
- Kein Element, das nur mit der Maus geht (Ziehen, Hover-Menüs); Tooltips sind Zusatz, nie die einzige Erklärung.

## 11 Zugänglichkeit und Technik (Pflicht)

- Farben nur aus `Theme`, Größen nur aus `Ui.px`/`Ui.hudPx` bzw. den benannten Stufen; neue Rollen mit Kontrastpaar.
- Jedes Bedienelement mit `Accessible.name` (Prüfung `QTCRAFT_ACCESSIBILITY_CHECK`).
- Feste Wertemengen als `Q_ENUM`/QML-`enum`, Anzeige-Texte aus `qsTr()`; abgelehnte Werte mit `qCWarning` (E-688).
- Alle Texte über `qsTr()` und `translations/de.json`; Inhaltstexte über die Sprachdateien.
- Steuerelement-Stil: ein eigener, plattformunabhängiger Stil für alle Bausteine (Entscheidung 1), damit Fenster, Offscreen-Screenshots, macOS und Windows gleich aussehen.

## 12 Entscheidungen (Mensch, 2026-10-22)

Alle neun Empfehlungen angenommen; Abweichung bei 4 (Qt-Grün).

1. **Eigener Steuerelement-Stil** auf allen Systemen: `QQuickStyle` „Basic“ (`QcAppWindow::applyControlStyle`, auch offscreen; `QT_QUICK_CONTROLS_STYLE` wählt zum Vergleich einen anderen) mit der Palette aus Abschnitt 2 und den eigenen Bausteinen. Screenshots zeigen damit, was Spieler sehen.
2. **Einstellungen mit Seitenleiste** (Variante B): Grafik, Audio, Steuerung, Controller, Oberfläche, Texturpakete, Erweitert.
3. **Hauptmenü Variante C** (Startseite mit „Weiterspielen“ und Seitenleiste) – umgesetzt in 76.0c (Abschnitt 8a, E-852).
4. **Hervorhebungsfarbe Qt-Grün** (#41CD52) statt Grasgrün, mit dunklem Text (Abschnitt 2.2); Gold bleibt Fokus und Überschrift.
5. **Pixelschrift nur für das Logo** (Abschnitt 4), Pixelify Sans, umgesetzt in 76.0c; Gewicht Regular (Mensch 2026-10-22, vorher fett).
6. **Schalter statt Kästchen** für Ein/Aus-Einstellungen.
7. **Debug und Diagnose unter „Erweitert“**; die Taste O öffnet die Seite in der Welt weiter direkt (Overlay `debug`).
8. **Texturpakete in den Einstellungen, Mods im Hauptmenü.**
9. **Reihenfolge:** 76.0b Bausteine, zentrale Werte, Stil und Einstellungen (mit 74.3) samt Pausenmenü; 76.0c (vorgezogen) Hauptmenü, Weltliste, Neue Welt, Mehrspieler, LAN, Mods und Charakter; die übrigen Bildschirme in 76.2.
