// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief Identity of a content pack that went into a registry (base game or mod); shown in the
    UI and recorded with saved worlds. */
struct QcPackReference
{
    /*! The pack id from its manifest. */
    QString id;
    /*! The display name from its manifest. */
    QString name;
    /*! The version text from its manifest. */
    QString version;

    friend bool operator==(const QcPackReference &, const QcPackReference &) = default;
};
