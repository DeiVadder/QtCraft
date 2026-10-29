// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Content names have the form "namespace:path" with lowercase letters, digits, '_', '-', '.' and '/'.
namespace QcContentNames {

bool isValidPart(const QString &part);
bool isValidQualified(const QString &name);
// Adds `defaultNamespace` when `name` has none. Returns an empty string for invalid names.
QString qualify(const QString &name, const QString &defaultNamespace);

} // namespace QcContentNames
