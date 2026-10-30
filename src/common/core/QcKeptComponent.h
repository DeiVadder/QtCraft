// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QtGlobal>

/*! \brief A saved component this version does not read (decisions E-420, E-823), kept as raw bytes
    so that saving again loses nothing: a component of an unknown id whole, or, for a known
    component of a newer version, the fields after the ones this version knows (\c {tail}), written
    back after them under the newer version.

    Entities (QcEntityComponent), block entities and item stacks (QcItemData::kept) keep them. */
struct QcKeptComponent
{
    /*! The component's id. */
    quint16 id = 0;
    /*! The version it was written in. */
    quint16 version = 0;
    /*! Only the fields after the known ones of a known component (see above). */
    bool tail = false;
    /*! The raw bytes. */
    QByteArray payload;

    bool operator==(const QcKeptComponent &) const = default;
};

/*! The kept components of one entity, block entity or item stack. */
using QcKeptComponents = QList<QcKeptComponent>;
