// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/debug/QcStartCheck.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcStartCheck {

QStringList lines(const QList<QQmlError> &warnings)
{
    QStringList list;
    for (const QQmlError &warning : warnings) {
        list.append(warning.toString());
    }
    return list;
}

bool missingQtPart(const QStringList &errors)
{
    // QQmlImportDatabase: 'module "QtQuick.Templates" plugin "qtquicktemplates2plugin" not found',
    // or 'Cannot load library …' when the file is there but does not load.
    static const QRegularExpression missing(u"plugin \"[^\"]+\" not found|Cannot load library"_s);
    return std::ranges::any_of(errors, [](const QString &error) { return missing.match(error).hasMatch(); });
}

bool plainStart(bool offscreen, const QStringList &arguments, const QProcessEnvironment &environment)
{
    return !offscreen && arguments.size() <= 1 && environment.value(u"QTCRAFT_SCREENSHOT"_s).isEmpty();
}

bool mayRestart(bool offscreen, const QStringList &arguments, const QProcessEnvironment &environment)
{
    return plainStart(offscreen, arguments, environment) && environment.value(QString::fromLatin1(retryVariable)).isEmpty();
}

} // namespace QcStartCheck
