#pragma once

#include "QcInventory.h"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QcContentRegistry;

// The player's inventory slots for QML (hotbar = rows 0..8).
class QcInventoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int selectedSlot READ selectedSlot NOTIFY selectedSlotChanged FINAL)

public:
    enum Role { ItemNameRole = Qt::UserRole + 1, LabelRole, CountRole, IconRole, DurabilityRole, EmptyRole };

    explicit QcInventoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setContent(std::shared_ptr<const QcContentRegistry> content, QString language);
    void setInventory(const QcInventory &inventory);
    int selectedSlot() const { return m_inventory.selectedHotbarSlot(); }

signals:
    void selectedSlotChanged();

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QString m_language;
    QcInventory m_inventory;
};
