// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class QcSettingsController;

// Photo mode (GameController.photo, decision E-692): the hide-HUD key hides the HUD with every
// overlay and the crosshair, and the hand when the settings say so; the screenshot key saves the
// window as a PNG in <data>/screenshots, named after the time, and announces it briefly. The
// picture comes from the same grab as the screenshot test hook (QcScreenshotHook::grabberFor).
// Lives in the GUI thread beside QcGameController.
class QcPhotoMode : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool hudHidden READ hudHidden NOTIFY hudHiddenChanged FINAL)
    // The file name of the latest screenshot, for the notice ("" before the first one).
    Q_PROPERTY(QString lastScreenshot READ lastScreenshot NOTIFY screenshotSaved FINAL)
    // True while the picture is taken: the notices hide so the previous one is not in the next.
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged FINAL)

public:
    explicit QcPhotoMode(const QcSettingsController &settings, QObject *parent = nullptr);

    bool hudHidden() const { return m_hudHidden; }
    // The hand is hidden with the HUD unless the settings keep it.
    bool handHidden() const;
    QString lastScreenshot() const { return m_lastScreenshot; }
    bool capturing() const { return m_capturing; }

    Q_INVOKABLE void toggleHud();
    // Shows the HUD again (a world ends).
    void showHud();
    // Where the picture comes from; without one nothing can be saved.
    void setGrabber(std::function<QImage()> grabber) { m_grabber = std::move(grabber); }
    // Saves the window now; the file's path, or why it failed (logged and sent as screenshotFailed).
    QcResult<QString> takeScreenshot();

    // <data>/screenshots.
    static QString directory();
    // "2026-10-19_14.03.07.png", with "_2", "_3", ... when that name is taken in `directory`.
    static QString fileName(const QDateTime &time, const QString &directory);

signals:
    void hudHiddenChanged();
    void screenshotSaved();
    // The picture could not be taken or saved; `reason` is translated, for the notice.
    void screenshotFailed(const QString &reason);
    void capturingChanged();
    // The hand's visibility may have changed (HUD toggled or the setting changed).
    void handHiddenChanged();

private:
    const QcSettingsController &m_settings;
    std::function<QImage()> m_grabber;
    // Takes the picture with capturing() true.
    QImage grab();
    // Logs `log`, sends `reason` and returns it as the error.
    QcResult<QString> fail(const QString &log, const QString &reason);

    bool m_hudHidden = false;
    bool m_capturing = false;
    QString m_lastScreenshot;
};
