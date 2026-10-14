# Fortschritt: Veröffentlichung auf GitHub (Nebenstrang)

Wiederaufnahme-Dokument für diesen Nebenstrang. `docs/FORTSCHRITT.md` gehört dem Hauptstrang und wird hier **nicht** angefasst.

## Rahmen

- **Branch:** `feature/github-ci`, Worktree `.claude/worktrees/github-ci`, abgezweigt von `dev` d6568ba (Etappe 31.1) am 2026-10-14. Der Haupt-Checkout bleibt auf `dev`, weil dort andere Agenten mergen.
- **Merge:** Nicht selbst nach `dev` mergen; der Mensch entscheidet.
- **Entscheidungsnummern:** E-950 ff. (Hauptstrang zuletzt E-369).
- **Nicht vor dem Ende:** Die Commit-E-Mail (`<alte Firmenadresse>`) wird erst ganz am Ende umgeschrieben, nachdem die Etappen aus `docs/FORTSCHRITT.md` durch sind, und vor dem ersten Push. Bis dahin nichts zu GitHub pushen.

## Stand

- **Erledigt:** GitHub Actions: CI (macOS mit allen Tests, Linux/Windows ohne Fehlschlag), Release (Tag `v<version>`, Entwurf mit `.zip` und `.dmg`), Pages (`web/`); Presets `ci-debug`, `ci-deploy` (Qt aus `QT_ROOT_DIR`, Open-Source-Qt 6.8.3); `release.sh` mit `QTCRAFT_RELEASE_PRESET` und `QTCRAFT_RELEASE_GUI=0` (lokal grün); `LICENSE` (GPL-3.0); Web-Handbuch (E-950) mit Export, `test_web_handbook`, `scripts/export-handbook.sh`.
- **Unabhängige Prüfung (2026-10-14):** 0 Blocker, 8 Soll-Befunde, 11 Hinweise. Behoben: Back-Taste über `history.state` (auch nach Zurück/Vor des Browsers), Null-Links, kaputte Adressen, überholte Routen, Release-Workflow führt die Tests aus und ersetzt einen alten Entwurf, `QT_DIR` aus der Shell, `QTCRAFT_RELEASE_GUI` nur 0/1, Hinweis „ohne Spielstarts“ in Ausgabe und Release-Text, Pixeltoleranz, Paritätstest der Suche in `QJSEngine`. Befund „ß wie ss falten“ war falsch (`QString::toCaseFolded` faltet 1:1); der neue Paritätstest hat das gezeigt.
- **Bewusst offen (Hinweise der Prüfung):** JSON eingerückt statt kompakt (lesbare Diffs; 2,7 MB je Sprache, Pages komprimiert); Actions an Tags statt SHAs gebunden; UI-Texte in `handbook.js` sind Kopien aus `translations/*.ts`; Icons werden wie im Spiel ohne Glättung verkleinert; `QcHandbookExport` im Headless bindet wie zuvor `QcHeadlessGame` Server-Header ein (Headless ist Kompositionsziel).
- **Voller `verify.sh` grün (2026-10-14, Stand `72d8bf8`, 4448 s):** Debug 149/149, ASan/UBSan 146/146, TSan 146/146, Release-Build, statische Analyse, Smoke-Läufe. Danach nur Doku-Commits (Vorschlag Enderperlen).
- **Daten erzeugt mit:** einem lokalen Qt 6.8; CI baut mit Open-Source-Qt 6.8.3.
- **Offen:**
  - Erster CI-Lauf auf GitHub: Bau gegen Qt 6.8.3 (`Qt::GuiPrivate` zwischen 6.8.3 und neueren 6.8-Versionen), Offscreen-Tests mit Metal auf der Runner-VM, Linux/Windows-Stand.
  - Einmal Settings → Pages → Source „GitHub Actions“ einstellen.
  - Vor dem Merge nach `dev`: `docs/Entscheidungen.md` endet hier mit E-950, im Hauptstrang mit den neuesten Nummern; der Konflikt am Dateiende ist nur Anhängen beider Teile.
  - Ganz am Ende: Commit-E-Mail umschreiben (siehe oben).
- **Pakete für andere:** öffentliche Pakete baut nur die CI mit Open-Source-Qt 6.8.3; lokal gebaute Pakete sind nur für eigene Tests.

## Vorschläge außerhalb dieses Strangs

- `docs/Vorschlag-Enderperlen.md`: mit dem Kopf im Block per Enderperlen Lage für Lage hindurch, auch durch eine Grundgestein-Decke (vom Menschen gewünscht, 2026-10-14); Grundgestein selbst bleibt unzerstörbar und unbeweglich, ein Brechen über Kolben oder Explosionen ist ausdrücklich nicht gewollt.
