#pragma once

#include <QByteArray>
#include <QList>
#include <QtGlobal>

// A saved component this version does not read (decisions E-420, E-823), kept as raw bytes so that
// saving again loses nothing: a component of an unknown id whole, or, for a known component of a
// newer version, the fields after the ones this version knows (`tail`), written back after them
// under the newer version. Entities (QcEntityComponent), block entities and item stacks
// (QcItemData::kept) keep them.
struct QcKeptComponent
{
    quint16 id = 0;
    quint16 version = 0;
    // Only the fields after the known ones of a known component (see above).
    bool tail = false;
    QByteArray payload;

    bool operator==(const QcKeptComponent &) const = default;
};

using QcKeptComponents = QList<QcKeptComponent>;
