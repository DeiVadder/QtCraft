#include "server/storage/QcSaveFile.h"

#include "common/core/QcCrc32.h"
#include "server/storage/QcItemDataCodec.h"

#include <QFile>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 maxFileBytes = qint64{256} * 1024 * 1024;

} // namespace

namespace QcSaveFile {

QByteArray bytes(QByteArrayView magic, const QByteArray &payload, quint16 version)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(streamVersion);
    stream.writeRawData(magic.data(), static_cast<int>(magic.size()));
    stream << version << static_cast<quint32>(payload.size()) << qcCrc32(payload);
    stream.writeRawData(payload.constData(), static_cast<int>(payload.size()));
    return data;
}

[[nodiscard]] QcStatus write(const QString &path, QByteArrayView magic, const QByteArray &payload, quint16 version)
{
    QSaveFile file(path);
    const QByteArray data = bytes(magic, payload, version);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    return {};
}

[[nodiscard]] QcResult<Payload> read(const QString &path, QByteArrayView magic, quint16 newestVersion)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(path, file.errorString()));
    }
    if (file.size() > maxFileBytes) {
        return qcError(u"%1 is too large"_s.arg(path));
    }

    return parse(file.readAll(), path, magic, newestVersion);
}

[[nodiscard]] QcResult<Payload> parse(const QByteArray &data, const QString &path, QByteArrayView magic, quint16 newestVersion)
{
    constexpr qsizetype headerSize = 4 + 2 + 4 + 4;
    if (data.size() < headerSize || !data.startsWith(magic)) {
        return qcError(u"%1 is not a QtCraft save file or is damaged"_s.arg(path));
    }
    QDataStream stream(data);
    stream.setVersion(streamVersion);
    stream.skipRawData(4);
    quint16 fileVersion = 0;
    quint32 size = 0;
    quint32 crc = 0;
    stream >> fileVersion >> size >> crc;
    if (fileVersion < baseVersion || fileVersion > newestVersion) {
        return qcError(u"%1 has unsupported format version %2"_s.arg(path).arg(fileVersion));
    }
    if (static_cast<qsizetype>(size) != data.size() - headerSize) {
        return qcError(u"%1 is truncated or damaged"_s.arg(path));
    }
    QByteArray payload = data.mid(headerSize);
    if (qcCrc32(payload) != crc) {
        return qcError(u"%1 is damaged (checksum mismatch)"_s.arg(path));
    }
    return Payload{std::move(payload), fileVersion};
}

void writeStack(QDataStream &stream, const QcItemStack &stack, const QcContentRegistry &content)
{
    const QString name = !stack.isEmpty() && content.isValidItem(stack.item) ? content.item(stack.item).name : QString();
    const bool withData = !name.isEmpty() && stack.data;
    const qint32 count = name.isEmpty() ? 0 : stack.count;
    stream << name << (withData ? count | QcItemDataCodec::dataFlag : count) << static_cast<qint32>(stack.damage);
    if (withData) {
        QcItemDataCodec::write(stream, stack.itemData(), content);
    }
}

QString keepAside(const QString &path)
{
    // An older copy is never overwritten.
    for (int attempt = 1; attempt <= maxDamagedCopies; ++attempt) {
        const QString backup = attempt == 1 ? path + u".damaged"_s : u"%1.damaged.%2"_s.arg(path).arg(attempt);
        if (!QFile::exists(backup)) {
            return QFile::copy(path, backup) ? backup : QString();
        }
    }
    return {};
}

} // namespace QcSaveFile
