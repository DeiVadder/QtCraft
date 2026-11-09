// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcBossBarMessages.h"

#include <QAbstractListModel>
#include <QColor>
#include <QtQmlIntegration/qqmlintegration.h>

class QcContentRegistry;

// The boss bars of the HUD for QML (GameController.hud.bossBars, decision E-1127): the creature's
// name (a name tag's, else its kind's translated), how full the bar is (0 to 1) and its colour.
// Rows are rebuilt only when bars come or go; a changing health only changes the rows' data.
class QcBossBarModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role { NameRole = Qt::UserRole + 1, ProgressRole, ColourRole };

    explicit QcBossBarModel(QObject *parent = nullptr);

    // The bars the session shows (QcClientBossBars::bars), names translated with `content` into
    // `language`; once per frame.
    void update(const QList<QcMsg::BossBar> &bars, const QcContentRegistry *content, const QString &language);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The colour of the bar colour `colour` (QcBossBarColour) as the HUD draws it.
    [[nodiscard]] static QColor colourOf(quint8 colour);

private:
    struct Row
    {
        quint32 entity = 0;
        QString name;
        double progress = 1.0;
        quint8 colour = 0;

        bool operator==(const Row &) const = default;
    };

    QList<Row> m_rows;
};
