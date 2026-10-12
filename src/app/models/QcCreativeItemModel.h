#pragma once

#include "QcCreativeCatalog.h"

#include <QAbstractListModel>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QcContentRegistry;

// The items of the creative inventory for QML (decision E-286): the catalogue of the session's
// content, narrowed to a tab and a search. Roles: itemName, label, icon, maxStack.
class QcCreativeItemModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    // Index into categoryNames (QcCreativeCatalog::Category).
    Q_PROPERTY(int category READ category WRITE setCategory NOTIFY categoryChanged FINAL)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(QStringList categoryNames READ categoryNames NOTIFY categoryNamesChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { ItemNameRole = Qt::UserRole + 1, LabelRole, IconRole, MaxStackRole };

    explicit QcCreativeItemModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The catalogue of new content or of another language.
    void setContent(std::shared_ptr<const QcContentRegistry> content, const QString &language);
    int category() const { return int(m_category); }
    void setCategory(int category);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    // The tab names in the UI language.
    static QStringList categoryNames();
    int count() const { return int(m_shown.size()); }

signals:
    void categoryChanged();
    void queryChanged();
    void categoryNamesChanged();
    void countChanged();

private:
    void refilter();

    std::shared_ptr<const QcContentRegistry> m_content;
    QcCreativeCatalog m_catalog;
    QcCreativeCatalog::Category m_category = QcCreativeCatalog::Category::All;
    QString m_query;
    QList<const QcCreativeCatalog::Entry *> m_shown;
};
