# UI-Leitfaden

Stand 2026-10-22 (Etappe 76.0, entschieden vom Menschen am 2026-10-22, Entscheidung E-845). Gilt für jeden Bildschirm außerhalb der Welt (Hauptmenü, Pause, Einstellungen, Mehrspieler, Mods, Charakter, Handbuch, Dialoge) und für die Paneele über der Welt (Inventar, Behälter, Handel). Das HUD hat eigene Regeln (`Ui.hudPx`, E-725) und ist hier nur am Rand betroffen. Neue Menüs (41 Zaubertisch/Amboss, 45 Render-Modus, 55 Welt- und Server-Optionen, 67 Fortschritte, 77 Server-Oberfläche) bauen auf diesem Leitfaden auf; 76.2 bringt Hauptmenü und übrige Bildschirme auf diesen Stand. Umgesetzt sind seit 76.0b die Bausteine (Abschnitt 6), die zentralen Werte (Abschnitt 0), der eigene Steuerelement-Stil und die Einstellungen mit Seitenleiste (Abschnitt 8) samt Pausenmenü.

## 0 Wo man was zentral ändert

Alle Werte stehen an genau **einer** Stelle im Code; Seiten und Bausteine schreiben keine eigenen Farben, Abstände, Größen oder Schriftgrößen.

| Was | Wo | Name in QML |
|---|---|---|
| Farben (Rollen mit Kontrastprüfung) | `src/clientcore/settings/QcUiPalette.cpp`, Tabelle `palette` (Rolle, QML-Name, ARGB) | `Theme.accent`, `Theme.panel`, … |
| Farben der Qt-Steuerelemente (Basic-Stil: Listen, Felder, Kästchen) | dieselbe Datei, `controlPalette()` (aus den Rollen abgeleitet) | – |
| Abstände, auch zum Fensterrand | `src/app/qml/controls/Ui.qml`, `space`, `screen.margin`, `screen.marginVertical` | `Ui.space.xs` … `Ui.space.xxl`, `Ui.screen.margin` |
| Größen der Steuerelemente | `Ui.qml`, `size` | `Ui.size.row`, `Ui.size.button`, `Ui.size.nav`, … |
| Bildschirmbreiten | `Ui.qml`, `screen` | `Ui.screen.s`, `.m`, `.l` |
| Radius | `Ui.qml` | `Ui.radius` |
| Schriftgrößen | `Ui.qml`, `type` | `Ui.type.title`, `.section`, `.group`, `.body`, `.help` |

Werte in `Ui.qml` sind für 100 % in 1024 × 640 geschrieben und laufen durch `Ui.px()` (E-725), stehen also schon in der Größe des Spielers bereit. Eine neue Farbe ist eine neue Rolle in `QcUiPalette` (Enum `Role`, Tabelle, Q_PROPERTY in `QcUiTheme.h`) und bekommt ihr Text-Hintergrund-Paar in `textPairs()`; ein neuer Abstand oder eine neue Größe kommt als benannter Wert nach `Ui.qml`. `test_ui_palette` prüft: keine Farbe außerhalb der Palette in irgendeiner QML-Datei (`screensUseOnlyThePalette`), keine eigene Größe in den Einstellungen, den Bausteinen (`src/app/qml/controls`) und dem Pausenmenü (`menusUseOnlyTheSizeSteps`: kein Zahlwert in `px()`/`hudPx()` oder `Qt.size()` und keine Zahl über 1 in einer Eigenschaft oder Deklaration, außer Faktoren wie `2 * Ui.space.l` und Eigenschaften ohne Größe wie `from`, `to`, `value`, `stepSize`, `opacity`, `z`, `model`, Zeiten in ms; `ItemSlot` nimmt den Faktor des HUD oder des Bildschirms und ist ausgenommen; die übrigen Bildschirme kommen mit 76.2 dazu).

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

Systemschrift. **Pixelschrift nur für das Logo (Entscheidung 5):** vorgesehen ist „Pixelify Sans“ (SIL Open Font License 1.1, darf eingebettet und mit dem Spiel weitergegeben werden; Lizenztext gehört neben die Datei). Das Logo steht im Hauptmenü und kommt mit 76.2; die Schriftdatei braucht dafür einen Download, den der Mensch freigibt, eingebettet über die Ressourcen (qrc). Bis dahin Systemschrift fett. Größen bei 100 %:

| Stufe | Name | Größe | Gewicht | Farbe | Wofür |
|---|---|---|---|---|---|
| Logo | `Ui.type.logo` / `logoSmall` | 64 / 48 | fett | `text` | nur Hauptmenü |
| Titel | `Ui.type.title` | 24 | fett | `text` | Titel eines Bildschirms (Einstellungen, Pause) |
| Abschnitt | `Ui.type.section` | 18 | fett | `text` | Seitentitel in den Einstellungen, Spaltenüberschrift |
| Gruppe | `Ui.type.group` | 14 | fett | `heading` | Gruppenüberschrift in Einstellungen |
| Text | `Ui.type.body` | 14 | normal | `text` | Beschriftungen, Werte, Schaltflächen |
| Hilfe | `Ui.type.help` | 12 | normal | `textMuted` | Hilfetext, Fußnoten (nie kleiner) |
| Schild | `Ui.type.badge` | 11 | fett | `textSecondary`/`heading` | Tasten-Schild, „ab Weltstart“ |

## 5 Seitenvorlage

Jeder Bildschirm hat dieselben vier Teile:

1. **Kopf:** Titel links (Stufe „Titel“), rechts „Schließen (Esc)“ bzw. das B-Symbol des Pads. Bei Unterseiten links „‹ Zurück“ (wie im Handbuch).
2. **Navigation** (nur bei mehreren Seiten): Seitenleiste links (Entscheidung 2).
3. **Inhalt:** scrollt als Ganzes (`ThemedScrollBar`), Kopf und Fuß bleiben stehen. Gruppen untereinander, Abstand `xl`.
4. **Fuß:** Aktionen rechtsbündig, die primäre ganz rechts; seitenbezogene Aktionen („Standard wiederherstellen“, „Ordner öffnen“) links. Darunter, wenn ein Pad benutzt wird, die Pad-Hinweise (`PadHints`).

Regeln:

- Einstellungen wirken **sofort** und werden beim Schließen gespeichert (wie heute). Deshalb kein „Übernehmen/Abbrechen“, sondern nur „Fertig“. Ausnahme: Anzeigemodus und Monitor (74.3) – nach dem Wechsel 10 s Rückfrage „Beibehalten?“ mit Rückfall.
- Wirkt eine Einstellung erst später („ab dem nächsten Weltstart“, „ab dem nächsten Start“), steht das als Hilfetext **an der Einstellung**, nicht als Fußnote am Seitenende.
- Leere Listen zeigen einen Satz, was hineinkommt, und die Aktion, die es ändert („Noch keine Welten. [Neue Welt]“).
- Pfade (Log-Ordner, Texturpakete, Mods) nicht als langer Fließtext; Schaltfläche „Ordner öffnen“ und daneben oder darunter der Pfad als auswählbarer Text, der an jeder Stelle umbricht (Tooltips sind Zusatz, nie die einzige Stelle).

## 6 Bausteine

QML-Komponenten in `src/app/qml/controls/`. Jede setzt `Accessible.name`/`role`, nimmt Farben aus `Theme`, Maße aus `Ui`, und hat feste Wertemengen als QML-`enum` (E-688). Dazu die Hilfsbausteine `Bevel` (Fase), `FocusOutline` (Tastaturfokus, gold, 2 px außen), `GameSwitch` (der Schalter allein, für Listenzeilen) und `ScreenFrame` (Fläche jedes Bildschirms und Dialogs). Die Einstellungsseiten bauen auf `settings/SettingsPage.qml` (Titel, Gruppen, scrollt als Ganzes).

### 6.1 `GameButton`
- `kind: GameButton.Primary | Secondary | Destructive | Quiet` (QML-`enum`, kein String).
- Primary: `accent`, fett; höchstens **eine** pro Bildschirm (die wahrscheinlichste nächste Aktion: „Spielen“, „Beitreten“, „Fertig“, „Zurück zum Spiel“).
- Secondary: `button`. Destructive: `danger`, nur wenn danach eine Rückfrage kommt oder es unumkehrbar ist. Quiet: nur Text (z. B. „‹ Zurück“, „Mehr …“).
- Fase: 1 px `bevelLight` oben/links, 2 px `bevelDark` unten/rechts; gedrückt umgekehrt (blockiger Look). Fokus: 2 px `focusRing` außen.
- Text immer ein Verb oder Ziel („Welt erstellen“, „Ordner öffnen“, „Zum Menü“); Tastenkürzel als kleines Schild rechts im Knopf (`KeyBadge`), nicht in Klammern im Text.

### 6.2 `SettingGroup`
Überschrift (Stufe „Gruppe“, `heading`), darunter die Zeilen auf `panelRaised` mit Innenabstand `l`. Einklappen (`collapsible`, „▸ Erweitert“) ist vorgesehen, aber noch nicht gebaut: keine Seite braucht es bisher.

### 6.3 `SettingRow`
Zeile mit `label`, optional `help` (eine Zeile, `textMuted`, unter dem Label, darf umbrechen), optional `badge` (z. B. „nach einem Neustart“), optional `key` (Taste als `KeyBadge` neben dem Label), rechts das Steuerelement. Zeilenhöhe ≥ 40. Fokusziel ist das Steuerelement der Zeile; Links/Rechts verstellen einen Regler (Gamepad und Pfeiltasten), A/Enter öffnet Listen und schaltet Schalter. `enabled: false` dimmt die Zeile und nennt im Hilfetext den Grund („Nur mit VSync aus“); Knöpfe und Schalter einer gedimmten Zeile dimmen sich nicht noch einmal. `dimmed` dimmt die Zeile, ohne sie zu sperren (die Lautstärken, solange stumm geschaltet ist).

### 6.4 `SettingSlider`
Regler mit Wert rechts in fester Breite, Einheit im Wert („12 Chunks“, „70°“, „80 %“). Feste Stufen mit `snapMode`. Gefüllter Teil `accent`, Spur `track`, Griff eckig (Block). Für Zahlen ohne Bedeutung (Empfindlichkeit 0,15) besser Prozent oder benannte Endpunkte („langsam … schnell“).

### 6.5 `SettingSwitch`
Ein-/Aus-Einstellungen sind **Schalter** (`Switch`), nicht Kästchen; die Beschriftung steht links wie bei allen Zeilen. Kästchen (`SettingCheck`) nur in Mehrfachauswahlen innerhalb einer Gruppe (z. B. Debug-Visualisierungen) und in Formularen („Inventar beim Tod behalten“). Leertaste und Enter schalten Schalter und Kästchen. (Entscheidung 6)

### 6.6 `SettingChoice` (Segmente)
2–4 kurze Möglichkeiten nebeneinander als Segmente (Anzeigemodus, Texturfilterung, Ansicht). Ab 5 Möglichkeiten oder langen Texten `SettingDropdown`. Segmente zeigen alle Möglichkeiten auf einen Blick; die Pfeiltasten Links/Rechts wählen das Nachbarsegment, das Pad bewegt den Fokus und bestätigt mit A.

### 6.7 `SettingDropdown`
`ThemedComboBox` (`field`, Fase eingelassen; hervorgehobene Zeile der Liste in `accent` mit `accentText`) in einer Einstellungszeile; Aufklapplisten außerhalb der Einstellungen sind ebenfalls `ThemedComboBox`. Für Sprache, Monitor, Bildraten-Limit, Oberflächengröße, Controller-Profil.

### 6.8 `Hint`
`kind: Info | Warning | Error`; Kasten mit 3 px Balken links in `link`/`warning`/`error`, Text `text`. Für Hinweise, die eine Gruppe betreffen (z. B. „Mods sind in dieser Sitzung abgeschaltet“). Nie nur Farbe: Warnung und Fehler haben ein Symbol (!, ×).

### 6.9 `ListRow`
Für Welten, Server, Mods, Texturpakete: Bild/Symbol links (optional), Titel (fett) und eine Zeile Untertitel (`textSecondary`), Aktionen rechts (höchstens eine primäre). Den Fokus nehmen die Aktionen der Zeile (Schalter, Knöpfe); eine ganz fokussierbare Zeile mit Doppelklick/A als Hauptaktion und ein „…“-Menü für weitere Aktionen sind für 76.2 vorgesehen.

### 6.10 `KeyBadge`
Taste oder Pad-Symbol (`PadGlyph`) als kleines Schild, z. B. neben „Handbuch“ das „H“. Ersetzt „Handbuch (H)“, „Beschriftungen in der Welt anzeigen (L)“.

### 6.11 `NavList` (Seitenleiste)
Kategorien als Liste links (Symbol + Text), gewählte Kategorie mit `accent`-Balken links und `panelRaised`; LB/RB (Gamepad, `gamepadCommand` des Bildschirms) und Strg+Tab / Strg+Umschalt+Tab wechseln die Kategorie (auf dem Mac die Control-Taste, die Qt als Meta meldet; Shortcuts der `NavList`, die vor dem fokussierten Steuerelement greifen, und `NavList.takeCommand`).

## 7 Beschriftung

- **Satzanfang groß, sonst wie im Satz** („Größe der Oberfläche“, nicht „Größe Der Oberfläche“); englisch ebenso (sentence case).
- **Schaltflächen sagen, was passiert:** Verb + Objekt („Welt löschen“, „Server beitreten“); Ausnahmen „Fertig“, „Schließen“, „Abbrechen“, „Zurück“. Nicht „OK“/„Ja“ in Dialogen, sondern die Aktion („Löschen“).
- **Ein Wort, eine Sache:** „Schließen“ für Bildschirme ohne Änderungen, „Fertig“ für Einstellungen, nie beides für dasselbe. Heute: Einstellungen „Fertig“, Mods/Mehrspieler „Schließen“, Charakter „Fertig“ – passt, wenn die Regel so gilt.
- **Keine Erklärungen in Klammern im Label:** „Passwort (nur, wenn der Server eines hat)“ → Label „Passwort“, Hilfetext „Nur nötig, wenn der Server eines hat“. Ebenso „Port (0: ein beliebiger freier Port)“.
- **Einheiten am Wert**, nicht am Label. Zahlen im deutschen Format (Komma).
- **Du-Form**, kurz, ohne Fachwörter auf der ersten Ebene („Chunks“ ist im Spiel eingeführt und bleibt; „Mip-Stufen“, „Atlas“ nur unter „Erweitert“).
- **Länge:** Deutsche Texte sind 30–40 % länger. Jede Zeile wird auf Deutsch bei 1024 × 640 und 200 % geprüft (Layout-Test). Labels ≤ 28 Zeichen, Hilfetexte ≤ 2 Zeilen; Segmente ≤ 14 Zeichen je Möglichkeit.
- **Inhaltstexte aus Paketen** (Texturpaket- und Mod-Beschreibungen) kommen übersetzt aus den Sprachdateien des Pakets; fehlt die Übersetzung, die englische ohne Hinweis.

## 8 Reihenfolge und Gliederung der Einstellungen

Umgesetzt in `src/app/qml/settings/` (`SettingsScreen.qml` mit `enum Page`; je Kategorie eine Seite: `GraphicsPage`, `AudioPage`, `ControlsPage`, `GamepadPage`, `InterfacePage`, `TexturePacksPage`, `AdvancedPage`). Die Overlays des Test-Hooks `--overlay` öffnen die Seiten: `settings` und `display` Grafik, `audio`, `controls`, `gamepad`, `interface`, `texturepacks`, `debug` Erweitert (`SettingsScreen.pageOfOverlay`).

Kategorien (Seitenleiste), in dieser Reihenfolge; in jeder Kategorie die Gruppen von häufig nach selten:

1. **Grafik:** *Anzeige* (Anzeigemodus, Monitor, VSync, Bildraten-Limit, Limit im Menü und im Hintergrund – 74.3) · *Welt* (Sichtweite, Sichtfeld, Sichtfeld beim Sprinten) · *Qualität* (Texturfilterung; 45 Render-Modus) · später *Erweitert* eingeklappt.
2. **Audio:** Gesamtlautstärke, Stumm (als Schalter direkt darunter; dimmt die Regler), Musik, Effekte, Umgebung.
3. **Steuerung:** *Maus* (Empfindlichkeit, Umkehren) · *Tastenbelegung* (die Tabelle).
4. **Controller:** wie heute, aber gegliedert in *Profil* · *Sticks und Trigger* · *Tasten*.
5. **Oberfläche:** Sprache, Größe der Oberfläche, Ansicht (Kamera), Beschriftungen in der Welt, Hand beim Ausblenden der Anzeige.
6. **Texturpakete.**
7. **Erweitert:** Debug-Anzeige und Visualisierungen, Protokoll und Absturzberichte.

Jede heutige Einstellung hat damit genau einen Platz; neue Einstellungen (41, 45, 55, 67) werden einer Kategorie und Gruppe zugeordnet, bevor sie gebaut werden. Welt- und Serveroptionen (55) gehören **nicht** in die Einstellungen, sondern zur Welt („Neue Welt“ → „Weitere Optionen“, Pause → „Welt-Optionen“).

Ein „Schnell“-Bereich ist nicht nötig, wenn die erste Kategorie Grafik → Anzeige ist und die Seitenleiste alles in einem Schritt erreicht.

## 9 Mindestgröße, Skalierung, Scrollen

- Jede Seite muss bei **1024 × 640** und **200 %** (begrenzt auf das, was passt, E-725) ohne abgeschnittene Elemente auskommen; der Inhalt scrollt, Kopf und Fuß nicht.
- Die Seitenleiste wird in Fenstern schmaler als `Ui.screen.narrow` (1100) auf `Ui.size.navNarrow` (160) statt `Ui.size.nav` (200).
- Layout-Tests: jede neue Seite kommt in `tests/ui/test_ui_scale.sh` und `test_menu_layout.sh` (Overlay oder `open:`), Deutsch eingeschlossen (`--language de`).

## 10 Gamepad und Tastatur

- Fokusreihenfolge = Lesereihenfolge: Kopf → Navigation → Inhalt von oben nach unten → Fuß. Der Fokus startet auf der primären Aktion (Dialoge) bzw. der ersten Einstellung (Einstellungen).
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
3. **Hauptmenü Variante C** (Startseite mit „Weiterspielen“ und Seitenleiste) – Etappe 76.2.
4. **Hervorhebungsfarbe Qt-Grün** (#41CD52) statt Grasgrün, mit dunklem Text (Abschnitt 2.2); Gold bleibt Fokus und Überschrift.
5. **Pixelschrift nur für das Logo** (Abschnitt 4), mit dem Hauptmenü in 76.2.
6. **Schalter statt Kästchen** für Ein/Aus-Einstellungen.
7. **Debug und Diagnose unter „Erweitert“**; die Taste O öffnet die Seite in der Welt weiter direkt (Overlay `debug`).
8. **Texturpakete in den Einstellungen, Mods im Hauptmenü.**
9. **Reihenfolge:** 76.0b Bausteine, zentrale Werte, Stil und Einstellungen (mit 74.3) samt Pausenmenü; Hauptmenü und übrige Bildschirme in 76.2.
