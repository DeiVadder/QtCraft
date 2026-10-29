// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class QJSEngine;
class QQmlEngine;

// The URLs of item icons (QcItemIconProvider) as image://items/<item name>?g=<generation>, for C++
// and for QML as the singleton `ItemIcons` (decision E-650). The generation grows whenever the
// provider gets new icons, so QML's pixmap cache never shows an icon of earlier content or one asked
// for before the content arrived. One instance per process, like the provider it describes.
class QcItemIconUrls : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ItemIcons)
    QML_SINGLETON
    Q_PROPERTY(int generation READ generation NOTIFY generationChanged FINAL)

public:
    static QcItemIconUrls *instance();
    static QcItemIconUrls *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // The URL of the item's icon of the current generation; empty for an empty name.
    static QString url(const QString &itemName);

    int generation() const { return m_generation; }
    // QML passes the generation so that bindings follow generationChanged.
    Q_INVOKABLE QString source(const QString &itemName, int generation) const;
    // Called from the GUI thread after the provider's icons were replaced.
    void advance();

signals:
    void generationChanged();

private:
    QcItemIconUrls() = default;

    int m_generation = 0;
};
