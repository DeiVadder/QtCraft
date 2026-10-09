#include "QcItemIconProvider.h"

#include <QMutexLocker>

QcItemIconProvider::QcItemIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void QcItemIconProvider::setIcons(QHash<QString, QImage> icons)
{
    const QMutexLocker locker(&m_mutex);
    m_icons = std::move(icons);
}

QImage QcItemIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage icon;
    {
        const QMutexLocker locker(&m_mutex);
        icon = m_icons.value(id);
    }

    if (icon.isNull()) {
        icon = QImage(iconSize, iconSize, QImage::Format_RGBA8888);
        icon.fill(Qt::transparent);
    }
    if (size) {
        *size = icon.size();
    }

    // Pixel art: scale without smoothing.
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        return icon.scaled(requestedSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return icon;
}
