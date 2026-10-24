# Handoff Etappe 48 (TLS im Mehrspieler)

Branch `feature/48-tls`, abgezweigt von `dev` 486e88a. Entscheidung **E-872** (`docs/Entscheidungen.md`)
beschreibt den Stand vollständig; hier nur, was für die Fortsetzung zählt.

## 48.1 (macOS) – fertig

- **OpenSSL 3.5.9 mitgeliefert:** `3rdparty/openssl-version.txt` (Version + SHA-256 des
  Release-Tarballs), `scripts/build-openssl.sh` (Cache `~/.cache/qtcraft/openssl/<v>-<arch>-<config>`,
  macOS universal, 13.3, nur `libssl.3.dylib`/`libcrypto.3.dylib` mit `@rpath`), die Sperre jetzt
  gemeinsam mit SDL in `scripts/cache-entry-lock.sh`. CMake `cmake/QcTls.cmake`: `QC_TLS`,
  `QC_OPENSSL_ROOT`, `QC_OPENSSL_AUTO_BUILD`, Ziele `QcOpenSSL::SSL`/`QcOpenSSL::Crypto`
  (öffentlich an `qc_network`), Definition `QC_TLS_ENABLED`.
- **Laufzeit:** `QcTlsBackend::ensure()` (`src/network/tls`) wählt einmal das Backend `openssl`; es
  läuft vor dem ersten Zertifikat/Socket und beim Start (headless `main()`, `QcMultiplayerController`). Qts Plugin findet die durch
  den rpath schon geladenen Bibliotheken (dyld gibt für den Blattnamen das geladene Bild zurück).
  `test_tls_credentials::bundledOpenSsl` prüft Backend, Version und Herkunft der geladenen Bibliotheken.
- **Schlüssel:** `QcTlsKey` über libcrypto, **EC P-256** (das OpenSSL-Backend nimmt EC als Server,
  Handshake TLS 1.3). Security.framework-Code, `QcTlsKey_mac.mm` und `useTemporaryKeychain()` sind
  entfernt. RSA-OIDs und `QcDer::algorithm`/`null` sind weg.
- **Die Fehler** (zweite Verbindung verliert Daten) waren Secure Transport; mit OpenSSL ohne
  Code-Änderung grün. `test_network_game::anAddressMayOpenOnlySoManyConnections` brauchte eine
  Test-Anpassung: die abgewiesene dritte Verbindung endet vor dem Handshake (Connector `failed`).
- **`QC_TLS=OFF`:** Klartext mit Warnungen; `QcTlsKey_other.cpp`, Tests mit TLS-Bezug per `QSKIP`,
  `test_tls_credentials` nur mit TLS gebaut. Ein OFF-Build wurde **nicht** gebaut (nur durchdacht).
- **Release:** `scripts/release.sh` baut OpenSSL, kopiert die Lizenz, prüft Frameworks
  (universal), das Plugin `PlugIns/tls/libqopensslbackend.dylib`, den Fingerabdruck des dedizierten
  Servers und verweigert ein Release ohne TLS. `release.sh` selbst ist **nicht** gelaufen.
- **Doku:** README (Bau, Release-Checkliste „OpenSSL-Sicherheitsmeldungen prüfen“), THIRD_PARTY.md,
  CREDITS.md, `docs/Architektur.md` (Build-Optionen), `docs/Multiplayer.md`; Übersetzungen und
  `scripts/export-handbook.sh` gelaufen.

## Offen

- `scripts/release.sh` einmal laufen lassen (prüft, dass macdeployqt die beiden Bibliotheken nach
  `Contents/Frameworks` bringt und die Kopie nur Bibliotheken aus dem Bundle lädt).
- Ein Probe-Lauf aus dem RSA-Zwischenstand hat ein Test-Zertifikat „QtCraft“ samt Schlüssel in den
  Anmelde-Schlüsselbund des Menschen gelegt (`security find-certificate -c QtCraft`); der Mensch
  entscheidet, ob er es entfernt.

## 48.2

- **Windows/Linux-Pakete und CI:** Linux baut das Skript schon nativ (`./Configure`, ungeprüft);
  Windows braucht nmake + Perl (Strawberry) oder Qts OpenSSL-Toolkit, `cmake/QcTls.cmake` bricht auf
  WIN32 ohne `QC_OPENSSL_ROOT` ab. Die CI-Workflows (`.github/workflows/ci.yml`, `release.yml`)
  brauchen einen Cache-Schlüssel über `3rdparty/openssl-version.txt` und `scripts/build-openssl.sh`
  wie bei SDL. **Wichtig für den Playtest (74):** die Tester spielen auf Windows; ohne 48.2 können
  Windows-Builds nicht beitreten (oder nur als `QC_TLS=OFF`, das mit normalen Builds nicht spricht).
- **Kosten headless ohne Echtzeit:** Handshake-Zeit (Unter-Test mit N Verbindungen über Port 0,
  `QElapsedTimer` je `encrypted`) und CPU je Spieler (`bench-headless.sh` mit Bots, vorher/nachher);
  Ergebnis in E-872 („Kosten“).
- UI-Feinschliff (Fingerabdruck-Zeile, Forget-Knopf, IPv6-Schalter).
