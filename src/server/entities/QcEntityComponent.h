// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcKeptComponent.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

// A component of a saved entity this version does not read (decision E-420), kept as raw bytes
// (QcKeptComponent, shared with item stacks, E-823). QcEntityCodec reads and writes them.
using QcEntityComponent = QcKeptComponent;
using QcEntityComponents = QcKeptComponents;

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
