// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcDimensionParser.h"
#include "fixtures/QcTestUtil.h"

#include <QJsonObject>

namespace QcTestUtil {

// The base content with an empty dimension `name` without weather and biomes: the void generator
// with `options` (a stone platform, "platform=qtcraft:stone,radius=1,y=1").
inline std::shared_ptr<QcContentRegistry> contentWithVoidDimension(const QString &name, const QString &options)
{
    const QJsonObject json{{QStringLiteral("name"), name},
                           {QStringLiteral("generator"), QJsonObject{{QStringLiteral("type"), QStringLiteral("void")}, {QStringLiteral("options"), options}}},
                           {QStringLiteral("weather"), false}};
    QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, QStringLiteral("test"));
    if (!dimension) {
        qFatal("test dimension does not parse: %s", qPrintable(dimension.error()));
    }
    std::shared_ptr<QcContentRegistry> content = loadContent();
    content->worldgen().registerDimension(std::move(*dimension));
    return content;
}

} // namespace QcTestUtil
