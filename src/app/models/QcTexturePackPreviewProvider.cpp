#include "app/models/QcTexturePackPreviewProvider.h"

#include <QMutexLocker>
#include <QUrl>

QcTexturePackPreviewProvider::QcTexturePackPreviewProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

void QcTexturePackPreviewProvider::setPreviews(QHash<QString, QImage> previews)
{
    const QMutexLocker locker(&m_mutex);
    m_previews = std::move(previews);
}

QImage QcTexturePackPreviewProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage preview;
    {
        const QMutexLocker locker(&m_mutex);
        // The id may carry "?<generation>" so QML reloads a changed preview.
        preview = m_previews.value(QUrl::fromPercentEncoding(id.section(u'?', 0, 0).toUtf8()));
    }

    if (preview.isNull()) {
        preview = QImage(2, 1, QImage::Format_RGBA8888);
        preview.fill(Qt::transparent);
    }
    if (size) {
        *size = preview.size();
    }

    // Pixel art: scale without smoothing.
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        return preview.scaled(requestedSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    return preview;
}
