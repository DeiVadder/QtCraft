// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcJsonFieldReader.h"
#include "common/content/villages/QcVillageDefinitions.h"

#include <QList>
#include <QString>

#include <optional>

// The street plans of a village style (decision E-408): `layouts` in villages/*.json, a list of
// weighted kinds with their parameters, and the defaults of each kind and of a style without the
// field.
namespace QcVillageLayoutParser {

// A layout of the kind with the kind's own parameters.
QcVillageLayout defaults(QcVillageLayout::Kind kind);
// The mix a style without `layouts` is laid out with.
QList<QcVillageLayout> defaultLayouts();
// The kind's name in data ("cross", "street", "t", "l", "plaza", "tree").
QString kindName(QcVillageLayout::Kind kind);
[[nodiscard]] std::optional<QcVillageLayout::Kind> kindNamed(const QString &name);
// The style's `layouts` (1 to maxLayouts entries); the defaults when it has none. Fails `reader`
// on a broken entry.
QList<QcVillageLayout> read(const QJsonObject &style, QcJsonFieldReader &reader);

} // namespace QcVillageLayoutParser
