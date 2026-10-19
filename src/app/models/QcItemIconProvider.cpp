#include "app/models/QcItemIconProvider.h"

#include "app/models/QcItemIconUrls.h"

#include <QMutexLocker>

QcItemIconProvider::QcItemIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void QcItemIconProvider::setIcons(QHash<QString, QImage> icons)
{
    {
        const QMutexLocker locker(&m_mutex);
        m_icons = std::move(icons);
        m_scaled.clear();
    }

    // New URLs for every icon: QML's pixmap cache must not answer with the old ones.
    QcItemIconUrls::instance()->advance();
}

QImage QcItemIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // The id carries "?g=<generation>" only for QML's cache.
    const QString name = id.section(u'?', 0, 0);
    const bool scale = requestedSize.isValid() && !requestedSize.isEmpty();
    const QMutexLocker locker(&m_mutex);

    QImage icon = m_icons.value(name);
    if (icon.isNull()) {
        icon = QImage(iconSize, iconSize, QImage::Format_RGBA8888);
        icon.fill(Qt::transparent);
    }
    if (size) {
        *size = icon.size();
    }
    if (!scale) {
        return icon;
    }

    // Pixel art: scale without smoothing.
    const std::pair key{name, requestedSize};
    if (const auto scaled = m_scaled.constFind(key); scaled != m_scaled.constEnd()) {
        return *scaled;
    }
    return *m_scaled.insert(key, icon.scaled(requestedSize, Qt::IgnoreAspectRatio, Qt::FastTransformation));
}
