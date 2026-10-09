#include "QcSlotRange.h"

#include <algorithm>

QcSlotRange::QcSlotRange(QObject *parent)
    : QAbstractListModel(parent)
{
}

void QcSlotRange::setSource(QAbstractItemModel *source)
{
    if (source == m_source) {
        return;
    }

    beginResetModel();
    for (const QMetaObject::Connection &connection : std::as_const(m_connections)) {
        disconnect(connection);
    }
    m_connections.clear();
    m_source = source;
    m_slotRole = Qt::UserRole + 1;

    if (m_source) {
        const QList<int> roles = m_source->roleNames().keys();
        for (const int role : roles) {
            m_slotRole = std::max(m_slotRole, role + 1);
        }

        m_connections = {
            connect(m_source, &QAbstractItemModel::dataChanged, this, &QcSlotRange::onSourceDataChanged),
            connect(m_source, &QAbstractItemModel::modelAboutToBeReset, this, &QcSlotRange::beginResetModel),
            connect(m_source, &QAbstractItemModel::modelReset, this, &QcSlotRange::endResetModel),
            // Fixed-size sources are the use case; any structural change simply resets the range.
            connect(m_source, &QAbstractItemModel::layoutAboutToBeChanged, this, &QcSlotRange::beginResetModel),
            connect(m_source, &QAbstractItemModel::layoutChanged, this, &QcSlotRange::endResetModel),
            connect(m_source, &QAbstractItemModel::rowsAboutToBeInserted, this, &QcSlotRange::beginResetModel),
            connect(m_source, &QAbstractItemModel::rowsInserted, this, &QcSlotRange::endResetModel),
            connect(m_source, &QAbstractItemModel::rowsAboutToBeRemoved, this, &QcSlotRange::beginResetModel),
            connect(m_source, &QAbstractItemModel::rowsRemoved, this, &QcSlotRange::endResetModel),
        };
    }

    endResetModel();
    emit sourceChanged();
}

void QcSlotRange::setFirst(int first)
{
    first = std::max(0, first);
    if (first == m_first) {
        return;
    }

    beginResetModel();
    m_first = first;
    endResetModel();
    emit firstChanged();
}

void QcSlotRange::setCount(int count)
{
    count = std::max(0, count);
    if (count == m_count) {
        return;
    }

    beginResetModel();
    m_count = count;
    endResetModel();
    emit countChanged();
}

int QcSlotRange::visibleCount() const
{
    return m_source ? std::clamp(m_source->rowCount() - m_first, 0, m_count) : 0;
}

int QcSlotRange::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : visibleCount();
}

QVariant QcSlotRange::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= visibleCount()) {
        return {};
    }

    const int row = m_first + index.row();
    if (role == m_slotRole) {
        return row;
    }
    return m_source->data(m_source->index(row, 0), role);
}

QHash<int, QByteArray> QcSlotRange::roleNames() const
{
    QHash<int, QByteArray> roles = m_source ? m_source->roleNames() : QHash<int, QByteArray>();
    roles.insert(m_slotRole, "slot");
    return roles;
}

void QcSlotRange::onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
{
    const int top = std::max(topLeft.row(), m_first) - m_first;
    const int bottom = std::min(bottomRight.row(), m_first + visibleCount() - 1) - m_first;
    if (top <= bottom) {
        emit dataChanged(index(top), index(bottom), roles);
    }
}
