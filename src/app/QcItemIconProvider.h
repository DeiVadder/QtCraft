#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Serves item icons to QML as image://items/<item name>. Icons are set from the GUI thread when
// content arrives; Qt Quick may request them from loader threads, hence the mutex.
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
    QMutex m_mutex;
    QHash<QString, QImage> m_icons;
};
