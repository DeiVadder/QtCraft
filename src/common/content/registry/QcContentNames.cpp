// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcContentNames.h"

#include <algorithm>

namespace QcContentNames {

bool isValidPart(const QString &part)
{
    return !part.isEmpty() && std::ranges::all_of(part, [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'_' || c == u'-' || c == u'.' || c == u'/';
    });
}

bool isValidQualified(const QString &name)
{
    const qsizetype colon = name.indexOf(u':');
    return colon > 0 && isValidPart(name.left(colon)) && isValidPart(name.mid(colon + 1));
}

QString qualify(const QString &name, const QString &defaultNamespace)
{
    const QString qualified = name.contains(u':') ? name : defaultNamespace + u':' + name;
    return isValidQualified(qualified) ? qualified : QString();
}

} // namespace QcContentNames
