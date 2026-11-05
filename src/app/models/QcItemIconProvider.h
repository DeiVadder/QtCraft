// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QImage>
#include <QQuickImageProvider>
#include <QSize>

#include <mutex>
#include <utility>

// Serves item icons to QML as image://items/<item name>?g=<generation> (QcItemIconUrls builds the
// URLs, decision E-650). Icons are set from the GUI thread when content arrives; Qt Quick may request
// them from loader threads, hence the mutex. Scaled icons are kept per item and size until new icons
// arrive.
class QcItemIconProvider : public QQuickImageProvider
{
    Q_OBJECT

public:
    static constexpr auto providerId = "items";
    static constexpr int iconSize = 16;

    QcItemIconProvider();

    void setIcons(QHash<QString, QImage> icons);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    // The GUI thread hands the icons to Qt Quick's image loader threads under this mutex.
    std::mutex m_mutex;
    QHash<QString, QImage> m_icons;
    QHash<std::pair<QString, QSize>, QImage> m_scaled;
};
