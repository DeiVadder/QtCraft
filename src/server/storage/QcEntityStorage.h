#pragma once

#include "QcContentRegistry.h"
#include "QcFallingBlockManager.h"
#include "QcItemEntityManager.h"
#include "QcMobManager.h"
#include "QcPlantManager.h"
#include "QcResult.h"

#include <QDataStream>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>

// Dropped items, growing saplings and falling blocks of a world (entities.qce).
struct QcWorldEntities
{
    QList<QcItemEntityManager::Drop> items;
    QList<QcPlantManager::Sapling> saplings;
    // Since format 2 of entities.qce.
    QList<QcFallingBlockManager::Falling> fallingBlocks;
};

// Persistent creatures (peaceful animals) and the chunks that already got their animals (mobs.qce).
struct QcWorldMobs
{
    QList<QcMobManager::Stored> mobs;
    QSet<QcChunkPos> populatedChunks;
};

// Entities of a world directory: dropped items, saplings and falling blocks (entities.qce) and
// persistent creatures (mobs.qce). Items, blocks and creatures are stored by name.
class QcEntityStorage
{
public:
    // Far more than the item manager keeps, so a valid file is never cut short.
    static constexpr quint32 maxStoredEntities = 65536;
    static constexpr quint32 maxStoredPopulatedChunks = 16 * 1024 * 1024;

    QcEntityStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    // Empty without error when nothing was saved yet. Unknown items are dropped with a warning.
    QcResult<QcWorldEntities> readEntities(QStringList &warnings) const;
    QcStatus writeEntities(const QcWorldEntities &entities) const;

    // Empty without error when nothing was saved yet. Unknown kinds are dropped with a warning.
    QcResult<QcWorldMobs> readMobs(QStringList &warnings) const;
    QcStatus writeMobs(const QcWorldMobs &mobs) const;

private:
    QcStatus readItems(QDataStream &stream, const QString &path, QList<QcItemEntityManager::Drop> &items, QStringList &warnings) const;
    // Adds a read drop of `count` items, split into stacks the item allows, up to maxStoredEntities
    // drops; returns the number of items left over.
    int addItem(QcItemEntityManager::Drop drop, QcItemId item, int count, int damage, QList<QcItemEntityManager::Drop> &items) const;
    static QcStatus readSaplings(QDataStream &stream, const QString &path, QList<QcPlantManager::Sapling> &saplings);
    QcStatus readFallingBlocks(QDataStream &stream, const QString &path, quint16 version, QList<QcFallingBlockManager::Falling> &blocks,
                               QStringList &warnings) const;

    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
};
