#pragma once

#include "QcArmourStandManager.h"
#include "QcContentRegistry.h"
#include "QcResult.h"

#include <QDataStream>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// Armour stands of a world directory (stands.qce, decision E-238): position, yaw, pose, name,
// what they wear and hold, their cape and the item they drop, all items by name. Worlds from before
// stage 23 have no such file and load without stands; files of version 1 load without capes.
class QcStandStorage
{
public:
    // Damaged files kept aside per world at most (stands.qce.damaged, .damaged.2, …).
    static constexpr int maxDamagedCopies = 16;

    QcStandStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    // Empty when nothing was saved yet. Unknown items, armour that no longer fits its slot and capes
    // the content no longer offers are dropped with a warning. A damaged file is copied aside
    // (keepAside) and gives no stands and a warning instead of an error (decision E-238). Stand ids
    // are left 0 (the server assigns them).
    QList<QcArmourStandManager::Stand> read(QStringList &warnings) const;
    QcStatus write(const QList<QcArmourStandManager::Stand> &stands) const;
    // Copies a damaged file to the first free name <path>.damaged, .damaged.2, … (also used for
    // access.json); that name, or empty when copying failed.
    static QString keepAside(const QString &path);

private:
    QcResult<QList<QcArmourStandManager::Stand>> readFile(const QString &path, QStringList &warnings) const;
    // One stored stand; false when it is damaged.
    bool readStand(QDataStream &stream, quint16 version, QcArmourStandManager::Stand &stand, QStringList &warnings) const;
    // Drops a cape the content no longer offers, with a warning.
    void checkCape(QcArmourStandManager::Stand &stand, const QString &path, QStringList &warnings) const;
    // A stored stack by name; unknown items become empty with a warning.
    QcItemStack readStack(QDataStream &stream, const QString &path, QStringList &warnings) const;

    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
};
