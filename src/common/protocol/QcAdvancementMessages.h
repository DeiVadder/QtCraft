// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/* QcMsg is documented in QcSessionMessages.h. */
namespace QcMsg {

/*! \brief Server -> client: the receiving player's progress in the adventure guide (decision
    E-923).

    After the welcome the server sends all of it in messages of at most maxEntries entries, the
    first with \c {reset} (the client forgets what it knew); afterwards each change as a delta
    with the advancements whose criteria changed. An entry carries all met criteria of its
    advancement (none: revoked) and whether it is reached; \c {announce} asks for the toast
    (reached just now). */
struct AdvancementProgress
{
    /*! The most entries of a message; the decoder refuses more. */
    static constexpr qsizetype maxEntries = 256;
    /*! The most criteria of an entry; the decoder refuses more. */
    static constexpr qsizetype maxCriteria = 32;
    /*! The longest advancement or criterion name (UTF-16 units); the decoder refuses longer
        ones. */
    static constexpr qsizetype maxNameLength = 128;

    /*! \brief One advancement's progress. */
    struct Entry
    {
        /*! The advancement's name. */
        QString advancement;
        /*! All its met criteria; none: revoked. */
        QStringList criteria;
        /*! Whether it is reached. */
        bool done = false;
        /*! Whether it was reached just now (the client shows the toast). */
        bool announce = false;

        bool operator==(const Entry &other) const = default;
    };

    /*! Whether the client forgets what it knew first. */
    bool reset = false;
    /*! The entries. */
    QList<Entry> entries;
};

} // namespace QcMsg
