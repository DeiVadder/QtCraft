#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class QcDiagnosticsSession;

// The log and crash reports for the player (GameController.settings.diagnostics, decision E-834):
// whether the game writes a log file (settings.ini, diagnostics/logToFile, from the next start on),
// the folder of logs and crash reports, the crash report the previous session left, opening that
// folder and collecting a diagnostics bundle to hand in.
class QcDiagnosticsSettings : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool logToFile READ logToFile WRITE setLogToFile NOTIFY logToFileChanged FINAL)
    // The log written now (native path); empty when none.
    Q_PROPERTY(QString logFile READ logFile NOTIFY sessionChanged FINAL)
    Q_PROPERTY(QString logFolder READ logFolder NOTIFY sessionChanged FINAL)
    // The previous session's crash report (native path) until dismissed; empty when it ended normally.
    Q_PROPERTY(QString crashReport READ crashReport NOTIFY sessionChanged FINAL)
    // A bundle is being written (collectDiagnostics()).
    Q_PROPERTY(bool collecting READ collecting NOTIFY collectingChanged FINAL)

public:
    explicit QcDiagnosticsSettings(QObject *parent = nullptr);

    // The setting as stored (true when missing), read before the window opens.
    static bool storedLogToFile();

    bool logToFile() const { return m_logToFile; }
    void setLogToFile(bool on);
    QString logFile() const { return m_logFile; }
    QString logFolder() const { return m_logFolder; }
    QString crashReport() const { return m_crashReport; }
    bool collecting() const { return m_collecting; }
    // The files of this run (main()).
    void setSession(const QcDiagnosticsSession &session);

    // Opens the folder of logs and crash reports in the system's file manager.
    Q_INVOKABLE void openLogFolder() const;
    // Writes a diagnostics bundle on a worker thread (copying logs and dumps may take a while), then
    // opens its folder and emits diagnosticsCollected(). Ignored while a bundle is being written.
    Q_INVOKABLE void collectDiagnostics();
    // Hides the notice about the previous crash.
    Q_INVOKABLE void dismissCrashReport();

signals:
    void logToFileChanged();
    void sessionChanged();
    void collectingChanged();
    // The native path of the bundle written; empty on failure (logged).
    void diagnosticsCollected(const QString &folder);

private:
    bool m_logToFile = true;
    QString m_logFile;
    QString m_logFolder;
    QString m_crashReport;
    bool m_collecting = false;
};
