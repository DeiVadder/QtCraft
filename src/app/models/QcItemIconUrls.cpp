// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcItemIconUrls.h"

#include <QQmlEngine>

using namespace Qt::StringLiterals;

QcItemIconUrls *QcItemIconUrls::instance()
{
    static QcItemIconUrls urls;
    return &urls;
}

QcItemIconUrls *QcItemIconUrls::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    // The process owns the instance; the engine must not delete it.
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

QString QcItemIconUrls::url(const QString &itemName)
{
    return instance()->source(itemName, instance()->generation());
}

QString QcItemIconUrls::source(const QString &itemName, int generation) const
{
    if (itemName.isEmpty()) {
        return {};
    }
    return u"image://items/%1?g=%2"_s.arg(itemName).arg(generation);
}

void QcItemIconUrls::advance()
{
    ++m_generation;
    emit generationChanged();
}
