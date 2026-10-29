// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/commands/QcCommandRegistry.h"

#include "server/core/QcServerLogging.h"

#include <algorithm>

using namespace Qt::StringLiterals;

bool QcCommandRegistry::isValidName(QStringView name)
{
    if (name.isEmpty() || name.size() > maxNameLength) {
        return false;
    }
    return std::ranges::all_of(name, [](QChar c) { return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'_'; });
}

QcStatus QcCommandRegistry::add(QcCommandSpec spec)
{
    if (!spec.run || !spec.usage || !spec.description) {
        return qcError(u"Command '%1' needs a handler, a usage and a description"_s.arg(spec.name));
    }

    QStringList names = spec.aliases;
    names.prepend(spec.name);
    for (const QString &name : std::as_const(names)) {
        if (!isValidName(name)) {
            return qcError(u"Invalid command name '%1'"_s.arg(name));
        }
        if (m_names.contains(name) || names.count(name) > 1) {
            return qcError(u"Command name '%1' is taken"_s.arg(name));
        }
    }

    for (const QString &name : std::as_const(names)) {
        m_names.insert(name, m_specs.size());
    }
    m_specs.append(std::move(spec));
    return {};
}

void QcCommandRegistry::addBuiltIn(QcCommandSpec spec)
{
    if (const QcStatus added = add(std::move(spec)); !added) {
        qCCritical(lcCommands, "%s", qPrintable(added.error()));
        Q_ASSERT(false);
    }
}

const QcCommandSpec *QcCommandRegistry::find(QStringView name) const
{
    const auto found = m_names.constFind(name.toString());
    return found != m_names.constEnd() ? &m_specs[*found] : nullptr;
}

QList<const QcCommandSpec *> QcCommandRegistry::commands() const
{
    QList<const QcCommandSpec *> sorted;
    for (const QcCommandSpec &spec : m_specs) {
        sorted.append(&spec);
    }
    std::ranges::sort(sorted, {}, &QcCommandSpec::name);
    return sorted;
}
