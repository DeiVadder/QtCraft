#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Serves the map preview of a new world to QML as image://worldmap/<revision> (decision E-921): the
// revision only makes a new map a new URL, the provider always serves the latest image. The GUI
// thread hands the images over (QcWorldMapPreview); Qt Quick may request them from loader threads,
// hence the mutex. Before the first map it serves a transparent pixel.
class QcWorldMapImageProvider : public QQuickImageProvider
{
    Q_OBJECT

public:
    static constexpr auto providerId = "worldmap";

    QcWorldMapImageProvider();

    void setImage(const QImage &image);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QMutex m_mutex;
    QImage m_image;
};
