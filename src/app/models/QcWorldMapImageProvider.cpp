// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcWorldMapImageProvider.h"

#include <QMutexLocker>

QcWorldMapImageProvider::QcWorldMapImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void QcWorldMapImageProvider::setImage(const QImage &image)
{
    const QMutexLocker locker(&m_mutex);
    m_image = image;
}

QImage QcWorldMapImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id)
    QImage image;
    {
        const QMutexLocker locker(&m_mutex);
        image = m_image;
    }
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
    }
    if (size) {
        *size = image.size();
    }

    // One pixel per cell: scaled without smoothing, the cells stay sharp.
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    return image;
}
