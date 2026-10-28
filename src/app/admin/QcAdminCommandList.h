#pragma once

#include "common/core/QcCommandCategory.h"
#include "common/protocol/QcAdminMessages.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

// The commands of the server for the command list of "Manage server" (decision E-911): every
// command of the catalogue with its translated usage and description, ordered by group
// (QcCommandCategory, the list's sections) and name; `filter` keeps those whose name, alias,
// usage, description or group contains it (case-insensitive). Clicking one puts it into the
// command line.
class QcAdminCommandList : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AdminCommandList)
    QML_UNCREATABLE("Provided by GameController.admin")
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    // Commands in the catalogue, filtered or not.
    Q_PROPERTY(int total READ total NOTIFY countChanged FINAL)

public:
    enum Role { NameRole = Qt::UserRole + 1, UsageRole, DescriptionRole, GroupRole, OperatorRole };

    // A command as the list shows it.
    struct Entry
    {
        QString name;
        QStringList aliases;
        QString usage;
        QString description;
        // The group's index (QcCommandCategory) and translated title.
        int group = 0;
        QString groupTitle;
        bool operatorOnly = false;
    };

    explicit QcAdminCommandList(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int count() const { return static_cast<int>(shown().size()); }
    int total() const { return static_cast<int>(entries().size()); }
    // Every command of the table, by group and name.
    const QList<Entry> &entries() const { return m_entries; }
    const QList<Entry> &shown() const { return m_shown; }

    // The translated title of a group.
    static QString groupTitle(QcCommandCategory category);
    // Takes the server's command table (texts translated now).
    void setCatalogue(const QcMsg::AdminCatalogue &catalogue);

signals:
    void filterChanged();
    void countChanged();

private:
    // Shows the entries the filter keeps.
    void refilter();
    bool matches(const Entry &entry) const;

    QList<Entry> m_entries;
    QList<Entry> m_shown;
    QString m_filter;
};
