#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

// A component of a saved entity this version does not read (decision E-420), kept as raw bytes so
// that saving the entity again loses nothing: a component of an unknown id whole, or, for a known
// component of a newer version, the fields after the ones this version knows (`tail`), written
// back after them under the newer version. QcEntityCodec reads and writes them.
struct QcEntityComponent
{
    quint16 id = 0;
    quint16 version = 0;
    // Only the fields after the known ones of a known component (see above).
    bool tail = false;
    QByteArray payload;

    bool operator==(const QcEntityComponent &) const = default;
};

using QcEntityComponents = QList<QcEntityComponent>;

// What a saved entry holds that this version does not use (decision E-420), carried by the runtime
// object so that loading, unloading and saving lose nothing: the components it does not read and
// the header fields its kind leaves unused in this version (the persistent id of every kind but
// creatures, the type name of armour stands, minecarts and experience orbs), written back as read.
struct QcEntityKept
{
    QcEntityComponents components;
    quint64 uid = 0;
    QString type;

    bool isEmpty() const { return components.isEmpty() && uid == 0 && type.isEmpty(); }
    bool operator==(const QcEntityKept &) const = default;
};
