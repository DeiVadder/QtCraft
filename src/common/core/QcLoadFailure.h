// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! \brief A load that failed (a file, a definition, the base game): why, and the warnings
    collected up to there (decision E-975). */
struct QcLoadFailure
{
    /*! Why the load failed. */
    QString error;
    /*! The warnings collected before the failure. */
    QStringList warnings;
};
