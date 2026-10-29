// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcPhotoMode.h"

#include "app/controllers/QcSettingsController.h"
#include "app/debug/QcAppLogging.h"
#include "common/core/QcDataRoot.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

using namespace Qt::StringLiterals;

QcPhotoMode::QcPhotoMode(const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    connect(&settings, &QcSettingsController::settingsChanged, this, &QcPhotoMode::handHiddenChanged);
}

bool QcPhotoMode::handHidden() const
{
    return m_hudHidden && m_settings.photoHidesHand();
}

void QcPhotoMode::toggleHud()
{
    m_hudHidden = !m_hudHidden;
    qCInfo(lcGame) << (m_hudHidden ? "Photo mode: HUD hidden" : "Photo mode: HUD shown");
    emit hudHiddenChanged();
    emit handHiddenChanged();
}

void QcPhotoMode::showHud()
{
    if (m_hudHidden) {
        toggleHud();
    }
}

QcResult<QString> QcPhotoMode::takeScreenshot()
{
    const QString folder = directory();
    if (!m_grabber || !QDir().mkpath(folder)) {
        return fail(u"Cannot save a screenshot in %1"_s.arg(folder), tr("Screenshot not saved: cannot write to %1").arg(folder));
    }

    const QImage image = grab();
    QString path = QDir(folder).filePath(fileName(QDateTime::currentDateTime(), folder));
    if (image.isNull() || !image.save(path, "PNG")) {
        return fail(u"Cannot save the screenshot %1"_s.arg(path), tr("Screenshot not saved: cannot write %1").arg(path));
    }

    qCInfo(lcGame).noquote() << u"Screenshot saved to"_s << path;
    m_lastScreenshot = QFileInfo(path).fileName();
    emit screenshotSaved();
    return path;
}

QImage QcPhotoMode::grab()
{
    // The grab renders a fresh frame, so the notices that bind to capturing are gone from it.
    m_capturing = true;
    emit capturingChanged();
    QImage image = m_grabber();
    m_capturing = false;
    emit capturingChanged();
    return image;
}

QcResult<QString> QcPhotoMode::fail(const QString &log, const QString &reason)
{
    qCWarning(lcGame).noquote() << log;
    emit screenshotFailed(reason);
    return std::unexpected(reason);
}

QString QcPhotoMode::directory()
{
    return QDir(QcDataRoot::path()).filePath(u"screenshots"_s);
}

QString QcPhotoMode::fileName(const QDateTime &time, const QString &directory)
{
    const QString stem = time.toString(u"yyyy-MM-dd_HH.mm.ss"_s);
    const QDir folder(directory);
    QString name = stem + u".png"_s;
    for (int number = 2; folder.exists(name); ++number) {
        name = u"%1_%2.png"_s.arg(stem).arg(number);
    }
    return name;
}
