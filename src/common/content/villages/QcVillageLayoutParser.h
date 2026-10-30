// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcJsonFieldReader.h"
#include "common/content/villages/QcVillageDefinitions.h"

#include <QList>
#include <QString>

#include <optional>

/*! \brief The street plans of a village style (decision E-408): \c {layouts} in
    villages/\unicode {0x2A}.json, a list of weighted kinds with their parameters, and the
    defaults of each kind and of a style without the field. */
namespace QcVillageLayoutParser {

/*! Returns a layout of kind \a kind with the kind's own parameters. */
QcVillageLayout defaults(QcVillageLayout::Kind kind);
/*! Returns the mix a style without \c {layouts} is laid out with. */
QList<QcVillageLayout> defaultLayouts();
/*! Returns the name of \a kind in data ("cross", "street", "t", "l", "plaza", "tree"). */
QString kindName(QcVillageLayout::Kind kind);
/*! Returns the kind called \a name in data; none for an unknown name. */
[[nodiscard]] std::optional<QcVillageLayout::Kind> kindNamed(const QString &name);
/*! Returns the \c {layouts} of the style object \a style (1 to maxLayouts entries); the
    defaults when it has none. Fails \a reader on a broken entry. */
QList<QcVillageLayout> read(const QJsonObject &style, QcJsonFieldReader &reader);

} // namespace QcVillageLayoutParser
