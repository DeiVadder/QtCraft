// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

// Lines of the server's chat or log or of the command output for the "Manage server" screen
// (decision E-863), newest last. New lines are appended (the oldest go over the limit), so a list
// view keeps its place while lines arrive; a new history or tail replaces them all.
class QcAdminLineModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AdminLineModel)
    QML_UNCREATABLE("Provided by GameController.admin")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { TimeRole = Qt::UserRole + 1, LevelRole, SourceRole, TextRole };

    struct Line
    {
        qint64 timeMs = 0;
        // QcMsg::AdminLogLine::Level for the log; Info for chat lines.
        int level = 1;
        // The log's category or the chat line's sender (empty for the server).
        QString source;
        QString text;

        bool operator==(const Line &) const = default;
    };

    explicit QcAdminLineModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_lines.size()); }
    const QList<Line> &lines() const { return m_lines; }

    void replace(const QList<Line> &lines);
    // Appends `lines`, keeping the newest `limit`.
    void append(const QList<Line> &lines, qsizetype limit);
    void clear();

signals:
    void countChanged();

private:
    QList<Line> m_lines;
};
