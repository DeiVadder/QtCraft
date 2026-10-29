// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payload of the map messages (QcMapMessages.h, decision E-802): the pixels as little-endian 16-bit
// values, compressed with qCompress when that saves bytes; every field is checked against the map's
// limits before the client keeps it.

#include "common/maps/QcMapData.h"
#include "common/protocol/QcProtocolCodec.h"

#include <QtEndian>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

namespace {

// Smaller pixel blocks are sent as they are.
constexpr qsizetype compressFrom = 256;

QByteArray pixelBytes(const QList<quint16> &pixels)
{
    QByteArray bytes(pixels.size() * 2, Qt::Uninitialized);
    for (qsizetype i = 0; i < pixels.size(); ++i) {
        qToLittleEndian(pixels[i], bytes.data() + i * 2);
    }
    return bytes;
}

// The pixels of an area of `count` pixels; std::nullopt for a block of the wrong length or one
// that unpacks to more than it may.
[[nodiscard]] std::optional<QList<quint16>> readPixels(QDataStream &stream, qsizetype count)
{
    bool packed = false;
    QByteArray bytes;
    stream >> packed >> bytes;
    if (packed) {
        // qUncompress allocates what the first four bytes claim: checked before.
        if (bytes.size() < 4 || qFromBigEndian<quint32>(bytes.constData()) != static_cast<quint32>(count * 2)) {
            return std::nullopt;
        }
        bytes = qUncompress(bytes);
    }
    if (bytes.size() != count * 2) {
        return std::nullopt;
    }
    QList<quint16> pixels(count);
    for (qsizetype i = 0; i < count; ++i) {
        pixels[i] = qFromLittleEndian<quint16>(bytes.constData() + i * 2);
    }
    return pixels;
}

[[nodiscard]] std::optional<QList<QcMapMarker>> readMarkers(QDataStream &stream)
{
    quint8 count = 0;
    stream >> count;
    if (count > QcMapMarker::maxCount) {
        return std::nullopt;
    }
    QList<QcMapMarker> markers(count);
    for (QcMapMarker &marker : markers) {
        quint8 flags = 0;
        stream >> marker.x >> marker.z >> marker.rotation >> flags;
        if (marker.rotation >= 16 || flags > 7) {
            return std::nullopt;
        }
        marker.onMap = (flags & 1) != 0;
        marker.own = (flags & 2) != 0;
        marker.frame = (flags & 4) != 0;
    }
    return markers;
}

} // namespace

void encode(QDataStream &stream, const QcMsg::MapUpdate &m)
{
    stream << m.id << m.scale << static_cast<qint32>(m.centre.x()) << static_cast<qint32>(m.centre.y()) << static_cast<quint8>(m.area.x())
        << static_cast<quint8>(m.area.y())
           << static_cast<quint8>(m.area.width()) << static_cast<quint8>(m.area.height());
    if (!m.area.isEmpty()) {
        const QByteArray raw = pixelBytes(m.pixels);
        const QByteArray packed = raw.size() >= compressFrom ? qCompress(raw) : QByteArray();
        const bool usePacked = !packed.isEmpty() && packed.size() < raw.size();
        stream << usePacked << (usePacked ? packed : raw);
    }
    stream << static_cast<quint8>(m.markers.size());
    for (const QcMapMarker &marker : m.markers) {
        stream << marker.x << marker.z << marker.rotation << static_cast<quint8>((marker.onMap ? 1 : 0) | (marker.own ? 2 : 0) | (marker.frame ? 4 : 0));
    }
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MapUpdate>, QDataStream &stream)
{
    QcMsg::MapUpdate m;
    qint32 x = 0;
    qint32 z = 0;
    quint8 left = 0;
    quint8 top = 0;
    quint8 width = 0;
    quint8 height = 0;
    stream >> m.id >> m.scale >> x >> z >> left >> top >> width >> height;
    m.centre = {x, z};
    m.area = QRect(left, top, width, height);
    if (m.id == 0 || m.scale > QcMapData::maxScale || std::abs(x) > QcMapData::maxCentre || std::abs(z) > QcMapData::maxCentre
        || (!m.area.isEmpty() && !QcMapData::bounds().contains(m.area))) {
        return qcError(u"Invalid map update"_s);
    }
    if (!m.area.isEmpty()) {
        std::optional<QList<quint16>> pixels = readPixels(stream, static_cast<qsizetype>(width) * height);
        if (!pixels) {
            return qcError(u"Invalid map pixels"_s);
        }
        m.pixels = std::move(*pixels);
    }
    std::optional<QList<QcMapMarker>> markers = readMarkers(stream);
    if (!markers) {
        return qcError(u"Invalid map markers"_s);
    }
    m.markers = std::move(*markers);
    return m;
}

} // namespace QcProtocolCodec
