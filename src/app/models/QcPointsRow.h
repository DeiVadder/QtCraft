// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QtQmlIntegration/qqmlintegration.h>

// A row of HUD icons that stand for points (GameController.player.heartRow, foodRow, armourRow,
// airRow): each icon holds `pointsPerIcon` points (two for hearts, food and armour, one per air
// bubble) and shows the share of them the player has (role `fill`, 0..1), so a heart is half full
// for an odd remainder. The row is shown or hidden as a whole (`shown`). New points change only
// the fills; the icons are made again only when their number changes.
class QcPointsRow : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PointsRow)
    QML_UNCREATABLE("Provided by GameController.player")
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged FINAL)

public:
    enum Role { FillRole = Qt::UserRole + 1 };

    explicit QcPointsRow(int pointsPerIcon, QObject *parent = nullptr);

    // The fills of the icons for `points` out of `maxPoints`: as many icons as `maxPoints` needs
    // (the last one may hold fewer points), each 0..1.
    [[nodiscard]] static QList<double> fills(double points, double maxPoints, int pointsPerIcon);

    bool shown() const { return m_shown; }
    void setShown(bool shown);
    // The player has `points` out of `maxPoints`.
    void setPoints(double points, double maxPoints);
    QList<double> iconFills() const { return m_fills; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void shownChanged();

private:
    void setFills(const QList<double> &fills);
    // Without signals: setFills() tells the views.
    void storeFills(const QList<double> &fills) { m_fills = fills; }
    void storeFill(qsizetype icon, double fill) { m_fills[icon] = fill; }

    const int m_pointsPerIcon;
    QList<double> m_fills;
    bool m_shown = false;
};
