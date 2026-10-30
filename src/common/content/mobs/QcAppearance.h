// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief A player's chosen look: outfit, hair style and cape by content name, and four colours as
    0xRRGGBB.

    The client keeps it in its settings and sends it to the server, which checks it. */
struct QcAppearance
{
    /*! Longest outfit, hair or cape name in UTF-16 code units; the protocol decoder refuses a
        message with a longer one. */
    static constexpr qsizetype maxNameLength = 64;

    /*! Content name of the outfit. */
    QString outfit = QStringLiteral("qtcraft:explorer");
    /*! Empty for no hair. */
    QString hair = QStringLiteral("qtcraft:short_hair");
    /*! Skin, hair, shirt and pants colours as 0xRRGGBB, multiplied into the layers tinted with
        the matching QcTint. \qcshared */
    quint32 skinColor = 0xd8a07a;
    quint32 hairColor = 0x4a3020;
    quint32 shirtColor = 0x3a7bc8;
    quint32 pantsColor = 0x34406a;
    /*! Empty for no cape (also the look of profiles stored before capes existed). */
    QString cape = {};

    bool operator==(const QcAppearance &) const = default;
};
