// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcHangingCodec.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

using namespace Qt::StringLiterals;

namespace {

using Entry = QcEntityRecord::Ordered<QcHangingEntity>;
using QcEntityCodec::Component;

// The fields of the component, as written; checked against the content after reading.
struct Fields
{
    quint8 type = 0;
    quint8 face = 0;
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    QString motif;
    quint8 rotation = 0;
    QcItemStack held;
};

// Reads the entries of one chunk.
class Reader
{
public:
    Reader(QDataStream &stream, QcChunkPos chunk, const QcContentRegistry &content, QcLoadWarnings &warnings)
        : m_stream(stream)
        , m_chunk(chunk)
        , m_content(content)
        , m_warnings(warnings)
        , m_path(u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z))
        , m_stacks(content, m_path, warnings)
    {
    }

    [[nodiscard]] std::optional<QList<Entry>> read()
    {
        QList<Entry> entries;
        if (m_stream.atEnd()) {
            return entries;
        }
        const std::optional<quint32> count = QcStreamLimits::readCount(m_stream, QcEntityCodec::maxPerChunk);
        if (!count) {
            return std::nullopt;
        }
        for (quint32 i = 0; i < *count; ++i) {
            const std::optional<std::optional<Entry>> entry = readEntry();
            if (!entry) {
                return std::nullopt;
            }
            if (*entry) {
                entries.append(**entry);
            }
        }
        return entries;
    }

private:
    // An engaged optional holding the entry, or nothing for an entry dropped with a warning;
    // nothing for damaged data.
    [[nodiscard]] std::optional<std::optional<Entry>> readEntry()
    {
        const std::optional<QString> item = QcComponentFrames::readText(m_stream, QcEntityCodec::maxTypeName);
        quint64 order = 0;
        m_stream >> order;
        Fields fields;
        const auto readFields = [this, &fields](QDataStream &in, quint16) {
            in >> fields.type >> fields.face >> fields.x >> fields.y >> fields.z >> fields.motif >> fields.rotation;
            fields.held = m_stacks.read(in);
            return in.status() == QDataStream::Ok;
        };
        std::optional<QcComponentFrames::ReadComponents> components =
            item ? QcComponentFrames::read(m_stream, {{static_cast<quint16>(Component::Hanging), QcEntityCodec::kindVersion, readFields}}) : std::nullopt;
        if (!components || fields.type >= qcHangingTypeCount || fields.face >= qcBlockFaceCount || fields.rotation >= QcHanging::rotations) {
            return std::nullopt;
        }

        std::optional<QcHangingEntity> hanging = resolve(fields, *item);
        if (!hanging) {
            return std::optional<Entry>();
        }
        // The middle follows the motif's size in the current content, so a mod that resized it can
        // move it into another chunk: only that entry goes, never the chunk.
        if (QcPhysics::blockContaining(hanging->motion.position).chunk() != m_chunk) {
            m_warnings.add(u"%1: frame or painting '%2' at %3, %4, %5 no longer fits in its chunk, dropped"_s.arg(m_path, *item)
                               .arg(fields.x)
                               .arg(fields.y)
                               .arg(fields.z));
            return std::optional<Entry>();
        }
        hanging->kept.components = std::move(components->kept);
        return std::optional<Entry>(Entry{*hanging, order});
    }

    // The frame or painting described by `fields` for its item's definition; nothing (with a
    // warning) for an unknown item or motif.
    [[nodiscard]] std::optional<QcHangingEntity> resolve(const Fields &fields, const QString &itemName)
    {
        const std::optional<QcItemId> item = m_content.itemId(itemName);
        const QcItemDefinition *definition = item ? &m_content.item(*item) : nullptr;
        if (!definition || !definition->hangs || definition->hangs->type != static_cast<QcHangingType>(fields.type)) {
            m_warnings.addOnce(u"hanging:"_s + itemName, u"%1: unknown frame or painting '%2' dropped (in this world reported only here)"_s.arg(m_path, itemName));
            return std::nullopt;
        }

        QcHangingEntity hanging;
        hanging.item = itemName;
        hanging.hanging.type = static_cast<QcHangingType>(fields.type);
        hanging.hanging.face = static_cast<QcBlockFace>(fields.face);
        hanging.hanging.cell = {fields.x, fields.y, fields.z};
        hanging.hanging.rotation = fields.rotation;
        hanging.held = hanging.hanging.type == QcHangingType::ItemFrame ? fields.held : QcItemStack();
        if (hanging.hanging.type == QcHangingType::Painting) {
            const QList<QcPaintingMotif> &motifs = definition->hangs->motifs;
            const auto it = std::ranges::find(motifs, fields.motif, &QcPaintingMotif::name);
            if (it == motifs.end()) {
                m_warnings.addOnce(u"motif:"_s + fields.motif, u"%1: unknown painting motif '%2' dropped (in this world reported only here)"_s.arg(m_path, fields.motif));
                return std::nullopt;
            }
            hanging.hanging.motif = static_cast<quint8>(it - motifs.begin());
            hanging.hanging.width = it->width;
            hanging.hanging.height = it->height;
        }
        hanging.motion.position = hanging.hanging.centre();
        return hanging;
    }

    QDataStream &m_stream;
    QcChunkPos m_chunk;
    const QcContentRegistry &m_content;
    QcLoadWarnings &m_warnings;
    QString m_path;
    QcStackReader m_stacks;
};

} // namespace

namespace QcHangingCodec {

void write(QDataStream &stream, const QList<Entry> &hangings, const QcContentRegistry &content)
{
    if (hangings.isEmpty()) {
        return;
    }
    stream << static_cast<quint32>(hangings.size());
    for (const auto &[hanging, order] : hangings) {
        stream << hanging.item << order;
        QString motif;
        if (const std::optional<QcItemId> item = content.itemId(hanging.item)) {
            if (const std::optional<QcHangingDefinition> &hangs = content.item(*item).hangs) {
                motif = hangs->motifs.value(hanging.hanging.motif).name;
            }
        }
        QcComponentFrames::Writer components(stream, hanging.kept.components);
        components.add(static_cast<quint16>(Component::Hanging), QcEntityCodec::kindVersion, [&](QDataStream &out) {
            const QcBlockPos cell = hanging.hanging.cell;
            out << static_cast<quint8>(hanging.hanging.type) << static_cast<quint8>(hanging.hanging.face) << static_cast<qint32>(cell.x)
                << static_cast<qint32>(cell.y) << static_cast<qint32>(cell.z) << motif
                << hanging.hanging.rotation;
            QcSaveFile::writeStack(out, hanging.held, content);
        });
        components.finish();
    }
}

[[nodiscard]] std::optional<QList<Entry>> read(QDataStream &stream, QcChunkPos chunk, const QcContentRegistry &content, QcLoadWarnings &warnings)
{
    return Reader(stream, chunk, content, warnings).read();
}

} // namespace QcHangingCodec
