# Entscheidungen

Format je Eintrag: Was, Warum, verworfene Alternative.

## E-001 Sprache der Prozess-Dokumente

- **Was:** `FORTSCHRITT.md`, `FRAGEN.md` und `Entscheidungen.md` sind deutsch (Prozessdokumente für den Auftraggeber). Doku zu Spiel und Schnittstellen (`Architektur.md`, `Steuerschnittstelle.md`, `Modding.md`, `Mod-Sicherheit.md`, `ASSETS.md`), Quelltext und Standard-UI-Texte sind englisch.
- **Warum:** Der Auftrag fordert Englisch für Spiel- und Schnittstellendoku; die Prozessdokumente richten sich an den deutschsprachigen Auftraggeber.
- **Verworfen:** Alles englisch – erschwert dem Auftraggeber das Beantworten von Fragen.

## E-002 Texturen ausschließlich prozedural

- **Was:** Alle Texturen werden aus Daten-Beschreibungen (Farben, Rauschen, Muster) zur Laufzeit erzeugt; Mods dürfen zusätzlich PNG-Dateien mitbringen.
- **Warum:** Vom Auftraggeber so festgelegt; keine Lizenzrisiken, keine Binärdateien im Repo.
- **Verworfen:** CC0-Downloads.
