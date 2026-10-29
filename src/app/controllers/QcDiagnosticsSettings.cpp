// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcDiagnosticsSettings.h"

#include "app/controllers/QcSettingsController.h"
#include "app/debug/QcAppLogging.h"
#include "common/core/QcDataRoot.h"
#include "common/diagnostics/QcDiagnosticsBundle.h"
#include "common/diagnostics/QcDiagnosticsSession.h"

#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QSettings>
#include <QtConcurrentRun>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace {

constexpr QLatin1StringView logToFileKey("diagnostics/logToFile");

} // namespace

QcDiagnosticsSettings::QcDiagnosticsSettings(QObject *parent)
    : QObject(parent)
    , m_logToFile(storedLogToFile())
{
}

bool QcDiagnosticsSettings::storedLogToFile()
{
    const QSettings file(QcSettingsController::settingsPath(), QSettings::IniFormat);
    return file.value(logToFileKey, true).toBool();
}

void QcDiagnosticsSettings::setLogToFile(bool on)
{
    if (on == m_logToFile) {
        return;
    }
    m_logToFile = on;
    // Its own key, written at once: main() reads it before the other settings load.
    QSettings file(QcSettingsController::settingsPath(), QSettings::IniFormat);
    file.setValue(logToFileKey, on);
    emit logToFileChanged();
}

void QcDiagnosticsSettings::setSession(const QcDiagnosticsSession &session)
{
    m_logFile = QDir::toNativeSeparators(session.logPath());
    m_logFolder = QDir::toNativeSeparators(session.logDirectory());
    m_crashReport = QDir::toNativeSeparators(session.previousCrashReport());
    emit sessionChanged();
}

void QcDiagnosticsSettings::openLogFolder() const
{
    QDir().mkpath(QDir::fromNativeSeparators(m_logFolder));
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::fromNativeSeparators(m_logFolder)))) {
        qCWarning(lcStartup).noquote() << u"Cannot open the folder %1"_s.arg(m_logFolder);
    }
}

void QcDiagnosticsSettings::collectDiagnostics()
{
    if (m_collecting) {
        // One bundle at a time; the button is disabled meanwhile.
        return;
    }
    m_collecting = true;
    emit collectingChanged();

    auto *watcher = new QFutureWatcher<QcResult<QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const QcResult<QString> written = watcher->result();
        watcher->deleteLater();
        m_collecting = false;
        emit collectingChanged();
        if (!written) {
            qCWarning(lcStartup).noquote() << u"Cannot write the diagnostics:"_s << written.error();
            emit diagnosticsCollected({});
            return;
        }
        qCInfo(lcStartup).noquote() << u"Diagnostics written to"_s << QDir::toNativeSeparators(*written);
        QDesktopServices::openUrl(QUrl::fromLocalFile(*written));
        emit diagnosticsCollected(QDir::toNativeSeparators(*written));
    });
    watcher->setFuture(QtConcurrent::run([data = QcDataRoot::path()] { return QcDiagnosticsBundle::write({u"qtcraft"_s, data, {}}); }));
}

void QcDiagnosticsSettings::dismissCrashReport()
{
    if (m_crashReport.isEmpty()) {
        return;
    }
    m_crashReport.clear();
    emit sessionChanged();
}
