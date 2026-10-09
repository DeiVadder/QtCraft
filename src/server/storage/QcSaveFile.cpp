#include "QcSaveFile.h"

#include "QcCrc32.h"

#include <QFile>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 maxFileBytes = qint64(256) * 1024 * 1024;

} // namespace

namespace QcSaveFile {

QcStatus write(const QString &path, QByteArrayView magic, const QByteArray &payload, quint16 version)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }

    QDataStream stream(&file);
    stream.setVersion(streamVersion);
    stream.writeRawData(magic.data(), int(magic.size()));
    stream << version << quint32(payload.size()) << qcCrc32(payload);
    stream.writeRawData(payload.constData(), int(payload.size()));
    if (stream.status() != QDataStream::Ok || !file.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    return {};
}

QcResult<QByteArray> read(const QString &path, QByteArrayView magic, quint16 newestVersion, quint16 *version)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(path, file.errorString()));
    }
    if (file.size() > maxFileBytes) {
        return qcError(u"%1 is too large"_s.arg(path));
    }

    const QByteArray data = file.readAll();
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
    if (version) {
        *version = fileVersion;
    }

    if (qsizetype(size) != data.size() - headerSize) {
        return qcError(u"%1 is truncated or damaged"_s.arg(path));
    }
    QByteArray payload = data.mid(headerSize);
    if (qcCrc32(payload) != crc) {
        return qcError(u"%1 is damaged (checksum mismatch)"_s.arg(path));
    }
    return payload;
}

void writeStack(QDataStream &stream, QcItemStack stack, const QcContentRegistry &content)
{
    const QString name = !stack.isEmpty() && content.isValidItem(stack.item) ? content.item(stack.item).name : QString();
    stream << name << qint32(name.isEmpty() ? 0 : stack.count) << qint32(stack.damage);
}

} // namespace QcSaveFile
