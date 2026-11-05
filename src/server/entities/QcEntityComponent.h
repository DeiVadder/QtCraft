// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcKeptComponent.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>

/*! A component of a saved entity this version does not read (decision E-420), kept as raw bytes
    (QcKeptComponent, shared with item stacks, E-823). QcEntityCodec reads and writes them. */
using QcEntityComponent = QcKeptComponent;
/*! The components of a saved entity this version does not read. */
using QcEntityComponents = QcKeptComponents;

/*! \brief What a saved entry holds that this version does not use (decision E-420), carried by
    the runtime object so that loading, unloading and saving lose nothing: the components it does
    not read and the header fields its kind leaves unused in this version (the persistent id of
    every kind but creatures, the type name of armour stands, minecarts and experience orbs),
    written back as read. */
struct QcEntityKept
{
    /*! The components it does not read. */
    QcEntityComponents components;
    /*! The persistent id of a kind that does not use it (0 when none was saved). */
    quint64 uid = 0;
    /*! The type name of a kind that does not use it (empty when none was saved). */
    QString type;

    /*! Returns whether nothing is kept. */
    bool isEmpty() const { return components.isEmpty() && uid == 0 && type.isEmpty(); }
    bool operator==(const QcEntityKept &) const = default;
};
