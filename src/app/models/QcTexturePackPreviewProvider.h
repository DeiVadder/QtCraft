// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Serves the preview images of texture packs to QML as image://texturepacks/<pack id>. Set from the
// GUI thread; Qt Quick may request them from loader threads, hence the mutex.
class QcTexturePackPreviewProvider : public QQuickImageProvider
{
    Q_OBJECT

public:
    static constexpr auto providerId = "texturepacks";

    QcTexturePackPreviewProvider();

    void setPreviews(QHash<QString, QImage> previews);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QMutex m_mutex;
    QHash<QString, QImage> m_previews;
};
