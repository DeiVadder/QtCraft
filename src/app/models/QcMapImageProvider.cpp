#include "app/models/QcMapImageProvider.h"

#include "common/maps/QcMapData.h"

#include <QMutexLocker>

QcMapImageProvider::QcMapImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void QcMapImageProvider::setImage(quint32 id, const QImage &image)
{
    const QMutexLocker locker(&m_mutex);
    m_images.insert(id, image);
    m_order.removeOne(id);
    m_order.append(id);
    while (m_order.size() > maxImages) {
        m_images.remove(m_order.takeFirst());
    }
}

QImage QcMapImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    bool ok = false;
    const quint32 mapId = id.section(u'/', 0, 0).toUInt(&ok);
    QImage image;
    if (ok) {
        const QMutexLocker locker(&m_mutex);
        image = m_images.value(mapId);
    }

    if (image.isNull()) {
        image = QImage(QcMapData::size, QcMapData::size, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
    }
    if (size) {
        *size = image.size();
    }

    // Pixel art: scale without smoothing.
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    return image;
}
