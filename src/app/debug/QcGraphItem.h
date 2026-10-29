// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QList>
#include <QQuickItem>
#include <QtQmlIntegration/qqmlintegration.h>

// A line graph of the latest frame or tick times for the debug display (decision E-225), drawn as
// two line strips in the scene graph (values and a budget line): no canvas, no text layout. The
// newest value is at the right; `capacity` values fill the width. Values above `maxValue` are cut.
class QcGraphItem : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DebugGraph)
    Q_PROPERTY(QList<qreal> values READ values WRITE setValues NOTIFY changed FINAL)
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY changed FINAL)
    Q_PROPERTY(qreal maxValue READ maxValue WRITE setMaxValue NOTIFY changed FINAL)
    // Drawn as a horizontal line; 0 hides it, and so does a scale that ends below it.
    Q_PROPERTY(qreal limit READ limit WRITE setLimit NOTIFY changed FINAL)
    Q_PROPERTY(bool limitShown READ limitShown NOTIFY changed FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed FINAL)
    Q_PROPERTY(QColor limitColor READ limitColor WRITE setLimitColor NOTIFY changed FINAL)

public:
    explicit QcGraphItem(QQuickItem *parent = nullptr);

    QList<qreal> values() const { return m_values; }
    void setValues(const QList<qreal> &values);
    int capacity() const { return m_capacity; }
    void setCapacity(int capacity);
    qreal maxValue() const { return m_maxValue; }
    void setMaxValue(qreal value);
    qreal limit() const { return m_limit; }
    void setLimit(qreal value);
    bool limitShown() const { return m_limit > 0.0 && m_limit <= m_maxValue; }
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    QColor limitColor() const { return m_limitColor; }
    void setLimitColor(const QColor &color);

signals:
    void changed();

protected:
    // Called on the render thread while the GUI thread is blocked. Qt Quick owns the returned node
    // tree: `old` is the tree returned last time (nullptr at first or after the scene graph was
    // lost), and Qt deletes the tree when the item goes away. The two line nodes keep the default
    // flag OwnedByParent (they go with the root) and own their geometry and material.
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;

private:
    void changeAndUpdate();

    QList<qreal> m_values;
    int m_capacity = 240;
    qreal m_maxValue = 33.3;
    qreal m_limit = 0.0;
    QColor m_color;
    QColor m_limitColor;
};
