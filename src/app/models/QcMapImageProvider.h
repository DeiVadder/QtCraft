// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Serves the drawn maps to QML as image://maps/<map id>/<revision> (decision E-802): the revision
// only makes a changed map a new URL, the provider always serves the latest image of the id. The
// GUI thread draws the images (QcHeldMap) and hands them over; Qt Quick may request them from
// loader threads, hence the mutex. A map not drawn yet is transparent (the paper shows).
class QcMapImageProvider : public QQuickImageProvider
{
    Q_OBJECT

public:
    static constexpr auto providerId = "maps";
    // Images kept; the hand holds one map, a few more spare redrawing after a short switch.
    static constexpr int maxImages = 8;

    QcMapImageProvider();

    void setImage(quint32 id, const QImage &image);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QMutex m_mutex;
    QHash<quint32, QImage> m_images;
    // Ids in the order they were last set, the oldest first.
    QList<quint32> m_order;
};
