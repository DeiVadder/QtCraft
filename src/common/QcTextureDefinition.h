#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Texture source as delivered by content packs. The server never decodes it; the client turns it into pixels.
struct QcTextureDefinition
{
    QString name;
    // Procedural description (pattern, colours, seed), used when png is empty.
    QJsonObject procedural;
    // Encoded PNG image supplied by a content pack.
    QByteArray png;
};
