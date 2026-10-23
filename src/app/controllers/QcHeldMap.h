#pragma once

#include "common/maps/QcMapMarker.h"

#include <QList>
#include <QObject>
#include <QRgb>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcContentRegistry;
class QcMapImageProvider;
class QcTextureAtlas;
struct QcSessionParts;

// The filled map in the player's hand (GameController.hud.heldMap, decision E-805): which map it is,
// its image (image://maps, drawn here from the cached map and the map colours of the atlas, decision
// E-801) and the players on it (decision E-803). Updated once per frame; it draws the image only
// when the map, its revision or the colours changed.
class QcHeldMap : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(HeldMap)
    QML_UNCREATABLE("Provided by GameController.hud.heldMap")
    // A filled map is held (its picture may still be on the way).
    Q_PROPERTY(bool held READ held NOTIFY changed FINAL)
    // The client has the map's picture.
    Q_PROPERTY(bool known READ known NOTIFY changed FINAL)
    Q_PROPERTY(int mapId READ mapId NOTIFY changed FINAL)
    Q_PROPERTY(int scale READ scale NOTIFY changed FINAL)
    // image://maps/<id>/<picture revision>-<colour generation>: a new URL whenever the picture
    // changes (moving markers leave it).
    Q_PROPERTY(QString source READ source NOTIFY changed FINAL)
    // One entry per player shown: x and z from -1 (west, north edge) to 1 (east, south edge),
    // rotation in degrees clockwise from north, onMap (false: a dot at the edge) and own.
    Q_PROPERTY(QVariantList markers READ markers NOTIFY changed FINAL)

public:
    explicit QcHeldMap(const QcSessionParts &parts, QObject *parent = nullptr);

    bool held() const { return m_held; }
    bool known() const { return m_revision > 0; }
    int mapId() const { return static_cast<int>(m_mapId); }
    int scale() const { return m_scale; }
    QString source() const;
    QVariantList markers() const { return m_markers; }

    // Where the images go; set once at the start (the QML engine owns the provider).
    void setProvider(QcMapImageProvider *provider) { m_provider = provider; }
    // The map colours of a new atlas (decision E-801): maps are drawn again.
    void useAtlas(const QcContentRegistry &content, const QcTextureAtlas &atlas);
    // Follows the held item and the cached map, once per frame.
    void update();

    // The markers as QML reads them (see the property `markers`).
    [[nodiscard]] static QVariantList markerList(const QList<QcMapMarker> &markers);

signals:
    void changed();

private:
    const QcSessionParts &m_parts;
    QcMapImageProvider *m_provider = nullptr;
    QList<QRgb> m_colours;
    // Grows with every new set of colours; part of the image URL.
    quint64 m_colourGeneration = 0;
    bool m_held = false;
    quint32 m_mapId = 0;
    int m_scale = 0;
    // The map's revision and the one that last changed its pixels (QcClientMaps::Map).
    quint64 m_revision = 0;
    quint64 m_pictureRevision = 0;
    quint64 m_drawnGeneration = 0;
    QVariantList m_markers;
};
