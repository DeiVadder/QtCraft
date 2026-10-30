// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

/*! \brief Texture source as delivered by content packs.

    The server never decodes it; the client turns it into pixels. */
struct QcTextureDefinition
{
    /*! Texture name that blocks and items refer to. */
    QString name;
    /*! Procedural description (pattern, colours, seed), used when png is empty. */
    QJsonObject procedural;
    /*! Encoded PNG image supplied by a content pack. */
    QByteArray png;
};
