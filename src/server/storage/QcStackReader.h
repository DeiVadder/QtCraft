#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"
#include "server/storage/QcLoadWarnings.h"

#include <QDataStream>
#include <QString>

#include <optional>

// Reads item stacks of one save file by name (written by QcSaveFile::writeStack) with their data
// (QcItemDataCodec); an unknown item is dropped with one warning per name
// (QcLoadWarnings::addOnce), counts and damage are clamped to what the item allows (a stack with
// contents holds one item). Damaged data turns the stream corrupt (QDataStream::ReadCorruptData).
// A filled map saved before E-823 with its map id in `damage` gets it as its map id.
class QcStackReader
{
public:
    // A stack as saved, before it is fitted to what its item allows now.
    struct Saved
    {
        QcItemId item = qcNoItem;
        qint32 count = 0;
        qint32 damage = 0;
        QcItemData data;
    };

    QcStackReader(const QcContentRegistry &content, QString path, QcLoadWarnings &warnings);

    // The stack of one slot; empty for an empty slot, an unknown item or damaged data.
    QcItemStack read(QDataStream &stream);
    // The data of a stack whose contents may nest `depth` levels (QcItemDataCodec); std::nullopt
    // for damaged data. Unknown components are kept, those beyond QcComponentFrames::maxKept
    // dropped with a warning.
    [[nodiscard]] std::optional<QcItemData> readData(QDataStream &stream, int depth);
    // The saved stack of a known item within the item's limits (count, damage, a name's length; a
    // filled map's id moved from its damage, see above).
    [[nodiscard]] QcItemStack fitted(const Saved &saved) const;

private:
    QcItemStack readAt(QDataStream &stream, int depth);

    const QcContentRegistry &m_content;
    QString m_path;
    QcLoadWarnings &m_warnings;
};
