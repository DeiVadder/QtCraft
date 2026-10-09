#pragma once

#include <QString>

// A player's chosen look: outfit and hair style by content name, and four colours as 0xRRGGBB.
// The client keeps it in its settings and sends it to the server, which checks it.
struct QcAppearance
{
    static constexpr qsizetype maxNameLength = 64;

    QString outfit = QStringLiteral("qtcraft:explorer");
    // Empty for no hair.
    QString hair = QStringLiteral("qtcraft:short_hair");
    quint32 skinColor = 0xd8a07a;
    quint32 hairColor = 0x4a3020;
    quint32 shirtColor = 0x3a7bc8;
    quint32 pantsColor = 0x34406a;

    bool operator==(const QcAppearance &) const = default;
};
