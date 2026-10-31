# Review-Liste

Stand 2026-10-31 (Paket 79.53, E-947). Fragen für Reviews von C++- und QML-Änderungen, die keine Prüfung des Projekts mechanisch findet. Review-Agenten bekommen diesen Pfad. Was clang-tidy, clazy, qmllint und unsere Skripte (`check-qml.py`, `check-member-access.py`, `check-duplicates.py`, Längen) schon melden, steht hier nicht; die Regeln in `CLAUDE.md` gelten daneben unverändert.

Quelle: abgeleitet in eigenen Worten aus den Prüflisten A1–A7 der Qt-Agent-Skills (`qt-cpp-review` und `qt-qml-review`, jeweils `references/agent-rules.md`, BSD-3-Clause), ausgewählt und an unsere Regeln angepasst. Kein Text ist wörtlich übernommen.

Ein Befund nennt Datei und Zeile, den verfolgten Weg (welche Symbole, Rollen, Ids geprüft wurden) und wie sicher er ist: sichtbarer fehlerhafter Pfad, abhängig von etwas Ungeprüftem (mit dem Schritt, der es klärt), oder bloßer Verdacht.

## C++

### Modelle (`QAbstractItemModel`)

- Jedes `begin…Rows`/`begin…Columns`/`beginResetModel` hat auf **jedem** Pfad (auch frühe Rückkehr) sein `end…`.
- Jede Rolle aus `roleNames()` liefert `data()` auch; `roleNames()` baut die Tabelle einmal, nicht bei jedem Aufruf.
- `setData()` meldet Erfolg nur mit `dataChanged` für genau die geänderten Rollen.
- Proxy-Modelle ändern die Quelle nur über deren API, nicht an ihr vorbei.

### Besitz und Lebensdauer

- Jedes `new` hat einen Besitzer (Elternobjekt, Smart Pointer, `deleteLater`); Fabrik-Overrides, deren Ergebnis Qt besitzt, sagen das (E-688).
- `connect` mit einem Sender oder Empfänger, der null sein kann; Lambdas ohne Kontextobjekt, die ein gelöschtes Objekt erreichen.
- Listen von Zeigern (zuletzt benutzt, Beobachter), die beim Löschen des Ziels nicht bereinigt werden.
- Klassen mit virtuellen Funktionen: virtueller Destruktor, Kopieren/Verschieben bewusst erlaubt oder gesperrt.

### Threads (E-379)

- Members eines QObject, die ein Worker oder `QtConcurrent` schreibt, während der Besitzer-Thread liest.
- Signale aus einem Worker mit `Qt::DirectConnection`: der Slot läuft im falschen Thread.
- Modelle oder Qt-Quick-Objekte, die außerhalb ihres Threads verändert werden.
- Gemeinsame Container, die an einer Stelle unter Sperre, an einer anderen ohne benutzt werden; Zähler mit `++` ohne `std::atomic`.
- Timer, Sockets und Kindobjekte, die aus einem fremden Thread gestartet, verschoben oder gelöscht werden.

### Fehler und Eingaben (E-688)

- Rückgaben von `QFile::open`, Parsern und `[[nodiscard]]`-Funktionen, die verloren gehen, auch über mehrere Zeilen verteilt.
- Gespeichertes oder vom Netz kommendes JSON ohne Prüfung von Version, Typ und Bereich; negative Werte, wo nur positive gelten.
- Erfolg gemeldet, obwohl Schreiben oder Umbenennen fehlschlug.
- Eine Klasse, die Fehler mal per Rückgabe, mal per Log, mal gar nicht meldet.

### Leistung und Aufbau

- Nicht-konstantes `operator[]` auf geteiltem `QHash`/`QMap`/`QList` zum Lesen: legt Einträge an und kopiert die Daten (Detach); `value()`/`constFind()` nehmen.
- Teure Arbeit vor einem billigen frühen Abbruch; Container per Wert aus heißen Pfaden.
- Zwischenspeicher in Members, die nicht ungültig werden, wenn sich ihre Quelle ändert.
- Methoden, die Signale senden, deren Empfänger wieder dieselbe Methode aufrufen (Re-Entrancy).
- Die Reihenfolge von `QHash`/`QSet` als „erster“ oder „bester“ Treffer (nicht deterministisch).
- Setter, die nebenbei fremden Zustand zurücksetzen.
- `const`-Methoden, die einen veränderbaren Zeiger auf inneren Zustand liefern.

## QML

### Bindungen und Properties

- Bindungsschleifen über mehrere Objekte (Qt meldet nur die direkte).
- Ketten von `alias` auf `alias`; Namen ohne Qualifizierung, die je nach Scope etwas anderes treffen; `pragma ComponentBehavior: Bound` in Dateien mit Delegates, die Ids außerhalb lesen.
- `Qt.binding()` in Schleifen, deren Variable mit `var` geteilt ist.
- Properties, die einmal gesetzt und nie geschrieben werden, ohne `readonly`.
- Property-Namen `on` + Großbuchstabe (`check-qml.py`, Regel `on-property`).

### Layout und Anker

- Anker an ein unsichtbares Item oder an eines aus einem anderen Zweig (nur `parent` und Geschwister).
- Kinder eines Layouts, deren Größe an die implizite Größe des Layouts gebunden ist (Rückkopplung); fehlendes `Layout.fill…`, wo das Item den Platz füllen soll; verschachtelte Layouts ohne Größenregel.
- `anchors` und `Layout.*` am selben Item (`check-qml.py`, `anchors-layout`).

### Laden und Lebensdauer

- `createObject()` ohne Besitzer oder Zerstörung; ein `Loader`, der zur Laufzeit zwischen `source` und `sourceComponent` wechselt.
- `Loader.item` ohne Prüfung (`check-qml.py`, `loader-item`), besonders bei `asynchronous: true`.
- `Image` mit wechselnder Quelle ohne Reaktion auf `status` (Fehler, Laden); `sourceSize` bei großen Bildern (`image-source-size`).
- `Connections`, deren `target` sich ändert und null werden kann.
- Objekte, die C++ an QML gibt: wem gehören sie (JavaScript-Besitz lässt die Garbage Collection löschen)?

### Listen und Delegates

- Delegates mit `required property` für Index und Rollen; Rollen, die das Modell wirklich liefert.
- `currentIndex` nach Modellwechsel (0 statt −1); mit `reuseItems` Zustand des alten Eintrags, der sichtbar bleibt.
- `parent` im Delegate ungeschützt benutzt (es ist der View-Inhalt und kann null sein).
- Schwere Delegates: geschachtelte Repeater, mehrere Loader, teure Bindungen je Eintrag.

### Zustände

- `PropertyChanges` stellt beim Verlassen den Wert wieder her (`restoreEntryValues`); `Binding` stellt seit Qt 6 den alten Wert oder die alte Bindung wieder her.
- Zustände in wiederverwendbaren Komponenten, die der Benutzer überschreibt; Übergänge mit `from: "*"`, die bei neuen Zuständen falsch feuern.

### Aufbau und Verdrahtung

- Teure Funktionsaufrufe in Bindungen, die oft neu rechnen; Regex oder Schleifen in JavaScript, das nach C++ gehört (E-942).
- `Text` mit Standard-`textFormat`, wo `Text.PlainText` reicht (fremde Texte, Chat, Namen).
- Signale gehen nach oben, Properties nach unten: ein Kind, das in den Zustand der Eltern schreibt.
- Eingabe mit `MouseArea`, wo ein Pointer-Handler (`TapHandler`, `HoverHandler`) passt; wiederverwendbare Komponenten mit fester `width`/`height` statt impliziter Größe.
- Deklariert, aber nicht verdrahtet: Einstellung, Property, Signal oder Funktion, die nichts liest oder aufruft; Ergebnisse von Prüfungen, die verworfen werden; Vergleiche, die immer gleich ausgehen; zwei Seiten (Speichern/Laden, Senden/Empfangen, C++/QML), die nicht zusammenpassen.
