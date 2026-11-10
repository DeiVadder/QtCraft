// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"
#include "server/storage/QcLoadWarnings.h"

#include <QDataStream>
#include <QList>
#include <QString>

#include <optional>

/*! \brief Reads item stacks of one save file by name (written by QcSaveFile::writeStack) with their
    data (QcItemDataCodec); an unknown item is dropped with one warning per name
    (QcLoadWarnings::addOnce), damage is clamped to what the item allows, and a stack holds at most
    what its item stacks to now (a stack with contents holds one item). What lay beyond is no
    longer lost when the caller collects it (setCollectsOverflow(), decision E-1066): a stack of
    an item that stacked higher when it was saved. Otherwise it is warned about.

    Damaged data turns the stream corrupt (QDataStream::ReadCorruptData). A filled map saved
    before E-823 with its map id in \c {damage} gets it as its map id. */
class QcStackReader
{
public:
    /*! \brief A stack as saved, before it is fitted to what its item allows now: item, count,
        damage and data. \qcfields */
    struct Saved
    {
        QcItemId item = qcNoItem;
        qint32 count = 0;
        qint32 damage = 0;
        QcItemData data;
    };

    /*! \brief Slots with overflow put into them. \qcfields
        \c {stacks} are the slots, \c {rest} what found no empty slot. */
    struct Filled
    {
        QList<QcItemStack> stacks;
        QList<QcItemStack> rest;
    };

    QcStackReader(const QcContentRegistry &content, QString path, QcLoadWarnings &warnings);

    /*! Sets whether what a stack holds beyond its limit waits for takeOverflow() (\a collects)
        instead of being warned about as lost. */
    void setCollectsOverflow(bool collects) { m_collectsOverflow = collects; }
    /*! Returns the stacks read beyond their limit since the last call, split to fit, also those
        from inside other stacks. */
    [[nodiscard]] QList<QcItemStack> takeOverflow();
    /*! Returns \a stacks with takeOverflow() in their empty slots, in order, and what is left. */
    [[nodiscard]] Filled fillWithOverflow(QList<QcItemStack> stacks);

    /*! Returns the stack of one slot read from \a stream; empty for an empty slot, an unknown
        item or damaged data. */
    QcItemStack read(QDataStream &stream);
    /*! Returns the data of a stack read from \a stream whose contents may nest \a depth levels
        (QcItemDataCodec); std::nullopt for damaged data. Unknown components are kept, those
        beyond QcComponentFrames::maxKept dropped with a warning. */
    [[nodiscard]] std::optional<QcItemData> readData(QDataStream &stream, int depth);
    /*! Returns the stack \a saved of a known item within the item's limits (count, damage, a name's
        length; a filled map's id moved from its damage, see above). */
    [[nodiscard]] QcItemStack fitted(const Saved &saved) const;

private:
    QcItemStack readAt(QDataStream &stream, int depth);
    // The first stack of `saved` within its limit; the others are overflow.
    QcItemStack keptOf(const Saved &saved);

    const QcContentRegistry &m_content;
    QString m_path;
    QcLoadWarnings &m_warnings;
    QList<QcItemStack> m_overflow;
    bool m_collectsOverflow = false;
};
