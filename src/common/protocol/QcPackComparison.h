// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcLoginMessages.h"

#include <QStringList>

/*! \brief Compares the content packs of server and client at the login (decision E-252): same
    ids, same versions and, where both sides computed one, the same hash over the files. */
namespace QcPackComparison {

/*! Differences are named at most this often; more are summed up. */
inline constexpr qsizetype maxNamed = 5;

/*! Returns one English line per difference between the packs of the \a server and the \a client
    ("missing: rubies 1.0", "extra: fun 2.0", "rubies: server 1.1, client 1.0", "rubies 1.0:
    different files"); empty when the packs match. */
QStringList differences(const QList<QcPackFingerprint> &server, const QList<QcPackFingerprint> &client);
/*! Returns \a differences as one line, the first maxNamed of them. */
QString summary(const QStringList &differences);

} // namespace QcPackComparison
