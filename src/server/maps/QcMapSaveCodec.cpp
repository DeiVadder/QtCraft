#include "server/maps/QcMapSaveCodec.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/maps/QcMapPixel.h"
#include "server/maps/QcMapStore.h"
#include "server/storage/QcSaveFile.h"

#include <QDataStream>
#include <QFileInfo>
#include <QHash>
#include <QtEndian>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Pixels refer to their block by its place in the file's own list of names.
struct Palette
{
    QStringList names;
    QList<quint16> pixels;
};

Palette paletteOf(const QcMapData &map, const QcContentRegistry &content)
{
    Palette palette;
    QHash<quint32, quint16> indices;
    palette.pixels.reserve(QcMapData::pixelCount);
    for (const quint16 pixel : map.pixels()) {
        const std::optional<quint32> block = QcMapPixel::blockOf(pixel);
        if (!block || std::cmp_greater_equal(*block, content.blocks().size())) {
            palette.pixels.append(QcMapPixel::blank);
            continue;
        }
        if (!indices.contains(*block)) {
            indices.insert(*block, quint16(palette.names.size()));
            palette.names.append(content.blocks()[*block].name);
        }
        palette.pixels.append(QcMapPixel::make(indices.value(*block), QcMapPixel::shadeOf(pixel)));
    }
    return palette;
}

QByteArray packPixels(const QList<quint16> &pixels)
{
    QByteArray raw(pixels.size() * 2, Qt::Uninitialized);
    for (qsizetype i = 0; i < pixels.size(); ++i) {
        qToLittleEndian(pixels[i], raw.data() + i * 2);
    }
    return qCompress(raw);
}

// The runtime pixels for the file's pixels; std::nullopt for a damaged block.
[[nodiscard]] std::optional<QList<quint16>> unpackPixels(const QByteArray &packed, const QList<std::optional<quint32>> &blocks)
{
    constexpr qsizetype rawSize = qsizetype(QcMapData::pixelCount) * 2;
    if (packed.size() < 4 || qFromBigEndian<quint32>(packed.constData()) != quint32(rawSize)) {
        return std::nullopt;
    }
    const QByteArray raw = qUncompress(packed);
    if (raw.size() != rawSize) {
        return std::nullopt;
    }
    QList<quint16> pixels(QcMapData::pixelCount);
    for (qsizetype i = 0; i < QcMapData::pixelCount; ++i) {
        const quint16 stored = qFromLittleEndian<quint16>(raw.constData() + i * 2);
        const std::optional<quint32> index = QcMapPixel::blockOf(stored);
        const std::optional<quint32> block = index && std::cmp_less(*index, blocks.size()) ? blocks[*index] : std::nullopt;
        pixels[i] = block ? QcMapPixel::make(*block, QcMapPixel::shadeOf(stored)) : QcMapPixel::blank;
    }
    return pixels;
}

} // namespace

namespace QcMapSaveCodec {

QString fileName(quint32 id)
{
    return u"maps/%1.qcmap"_s.arg(id);
}

std::optional<quint32> idOf(const QString &fileName)
{
    const QString name = QFileInfo(fileName).fileName();
    if (!name.endsWith(u".qcmap"_s)) {
        return std::nullopt;
    }
    bool ok = false;
    const quint32 id = name.chopped(6).toUInt(&ok);
    return ok && id > 0 && id <= QcMapStore::maxId ? std::optional<quint32>(id) : std::nullopt;
}

QByteArray write(const QcMapData &map, const QcContentRegistry &content)
{
    const Palette palette = paletteOf(map, content);
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QcSaveFile::streamVersion);
    out << overworld.toString() << quint8(map.scale()) << qint32(map.centre().x()) << qint32(map.centre().y()) << quint32(palette.names.size());
    for (const QString &name : palette.names) {
        out << name;
    }
    out << packPixels(palette.pixels);
    return QcSaveFile::bytes(magic, payload);
}

QcResult<QcMapData> read(const QByteArray &file, const QString &path, const QcContentRegistry &content)
{
    if (file.size() > maxBytes) {
        return qcError(u"%1 is too large for a map"_s.arg(path));
    }
    const QcResult<QcSaveFile::Payload> payload = QcSaveFile::parse(file, path, magic);
    if (!payload) {
        return qcError(payload.error());
    }
    QDataStream in(payload->bytes);
    in.setVersion(QcSaveFile::streamVersion);
    QString world;
    quint8 scale = 0;
    qint32 x = 0;
    qint32 z = 0;
    quint32 count = 0;
    in >> world >> scale >> x >> z >> count;
    if (in.status() != QDataStream::Ok || world != overworld || count > QcMapPixel::maxBlock + 1) {
        return qcError(u"%1 is not a valid map"_s.arg(path));
    }
    QStringList names;
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        names.append(QString());
        in >> names.back();
    }
    QByteArray packed;
    in >> packed;
    if (in.status() != QDataStream::Ok || !in.atEnd()) {
        return qcError(u"%1 is not a valid map"_s.arg(path));
    }

    QList<std::optional<quint32>> blocks;
    for (const QString &name : names) {
        const std::optional<QcBlockId> state = content.blockId(name);
        blocks.append(state ? std::optional<quint32>(content.state(*state).type) : std::nullopt);
    }
    std::optional<QList<quint16>> pixels = unpackPixels(packed, blocks);
    std::optional<QcMapData> map = pixels ? QcMapData::fromParts(scale, {x, z}, std::move(*pixels)) : std::nullopt;
    if (!map) {
        return qcError(u"%1 is not a valid map"_s.arg(path));
    }
    return std::move(*map);
}

} // namespace QcMapSaveCodec
