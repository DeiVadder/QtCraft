// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/startup/QcStartFailure.h"

#include "app/debug/QcAppLogging.h"
#include "app/debug/QcStartCheck.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QThread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif


using namespace Qt::StringLiterals;

namespace {

QString advice(bool missingPart)
{
    if (!missingPart) {
        return QCoreApplication::translate("QcStartFailure", "Please send the log file to the game's makers.");
    }
    return QCoreApplication::translate("QcStartFailure",
                                       "Parts of Qt in the game's folder could not be found. Unpack the ZIP file completely "
                                       "(right-click, \"Extract all\"), then start qtcraft.exe in the unpacked folder, not inside "
                                       "the ZIP file. An antivirus program that checks new programs at their first start can "
                                       "cause this too: start QtCraft once more.");
}

} // namespace

namespace QcStartFailure {

bool loadMainWindow(QQmlApplicationEngine &engine, bool offscreen)
{
    QStringList errors;
    const QMetaObject::Connection collecting =
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&errors](const QList<QQmlError> &warnings) { errors += QcStartCheck::lines(warnings); });
    engine.loadFromModule("QtCraft", "Main");
    QObject::disconnect(collecting);
    if (!engine.rootObjects().isEmpty()) {
        return true;
    }

    // A part of Qt missing at the first start of a fresh package: once more (decision E-964).
    const bool missingPart = QcStartCheck::missingQtPart(errors);
    const bool mayRestart = QcStartCheck::mayRestart(offscreen, QCoreApplication::arguments(), QProcessEnvironment::systemEnvironment());
    if (missingPart && mayRestart && restart(2'000)) {
        return false;
    }
    report(errors, missingPart, offscreen);
    return false;
}

bool restart(int pauseMs)
{
    qCWarning(lcStartup) << "Parts of Qt were missing at the start; starting once more in" << pauseMs << "ms";
    QThread::msleep(static_cast<unsigned long>(pauseMs));
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setWorkingDirectory(QDir::currentPath());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(QcStartCheck::retryVariable), u"1"_s);
    process.setProcessEnvironment(environment);
    const bool started = process.startDetached();
    if (!started) {
        qCWarning(lcStartup).noquote() << u"Cannot start %1 once more: %2"_s.arg(process.program(), process.errorString());
    }
    return started;
}

void report(const QStringList &errors, bool missingPart, bool offscreen)
{
    const QString text = advice(missingPart);
    qCCritical(lcStartup).noquote() << u"The main window could not be loaded (%1): %2"_s.arg(QCoreApplication::applicationDirPath(), errors.join(u"; "_s));
    qCCritical(lcStartup).noquote() << text;
#ifdef Q_OS_WIN
    // A program without a console shows nothing else.
    if (QcStartCheck::plainStart(offscreen, QCoreApplication::arguments(), QProcessEnvironment::systemEnvironment())) {
        const QString message = QCoreApplication::translate("QcStartFailure", "QtCraft could not start.") + u"\n\n"_s + text + u"\n\n"_s
                                + errors.mid(0, 3).join(u"\n"_s);
        MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(message.utf16()), L"QtCraft", MB_OK | MB_ICONERROR);
    }
#else
    // Elsewhere the log and standard error say it.
    Q_UNUSED(offscreen)
#endif
}

} // namespace QcStartFailure
