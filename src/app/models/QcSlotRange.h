// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>

// A contiguous run of rows of another list model for QML views that show only some slots (the
// hotbar, a crafting grid, a chest): delegates exist only for these rows. Adds the role `slot`, the
// row in the source model.
class QcSlotRange : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SlotRange)
    Q_PROPERTY(QAbstractItemModel *source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(int first READ first WRITE setFirst NOTIFY firstChanged FINAL)
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY countChanged FINAL)

public:
    explicit QcSlotRange(QObject *parent = nullptr);

    QAbstractItemModel *source() const { return m_source; }
    void setSource(QAbstractItemModel *source);
    int first() const { return m_first; }
    void setFirst(int first);
    int count() const { return m_count; }
    void setCount(int count);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void sourceChanged();
    void firstChanged();
    void countChanged();

private:
    // Rows of the source that exist and lie in the range.
    int visibleCount() const;
    void onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles);

    QPointer<QAbstractItemModel> m_source;
    QList<QMetaObject::Connection> m_connections;
    int m_first = 0;
    int m_count = 0;
    // Role id of `slot`, above every role of the source.
    int m_slotRole = Qt::UserRole + 1;
};
