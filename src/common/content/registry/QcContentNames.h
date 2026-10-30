// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief Content names have the form "namespace:path" with lowercase letters, digits, '_', '-',
    '.' and '/'. */
namespace QcContentNames {

/*! Returns whether \a part is a non-empty namespace or path of only the allowed characters. */
bool isValidPart(const QString &part);
/*! Returns whether \a name is "namespace:path" with two valid parts. */
bool isValidQualified(const QString &name);
/*! Returns \a name with \a defaultNamespace added when it has none, or an empty string when the
    result is not a valid qualified name. */
QString qualify(const QString &name, const QString &defaultNamespace);

} // namespace QcContentNames
