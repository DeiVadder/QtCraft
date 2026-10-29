#pragma once

#include "common/gameplay/QcCreativeCatalog.h"

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
    int category() const { return static_cast<int>(m_category); }
    void setCategory(int category);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    // The tab names: the handbook's category names where the handbook has the category (blocks,
    // building parts, tools and armour, food), else the UI's own names.
    QStringList categoryNames() const;
    int count() const { return static_cast<int>(m_shown.size()); }

signals:
    void categoryChanged();
    void queryChanged();
    void categoryNamesChanged();
    void countChanged();

private:
    void refilter();
    const QcContentRegistry *content() const { return m_content.get(); }
    void setContent(std::shared_ptr<const QcContentRegistry> content) { m_content = std::move(content); }
    const QcCreativeCatalog &catalog() const { return m_catalog; }
    void setCatalog(QcCreativeCatalog catalog) { m_catalog = std::move(catalog); }
    const QList<const QcCreativeCatalog::Entry *> &shown() const { return m_shown; }
    void setShown(QList<const QcCreativeCatalog::Entry *> shown) { m_shown = std::move(shown); }

    std::shared_ptr<const QcContentRegistry> m_content;
    QcCreativeCatalog m_catalog;
    QString m_language;
    QcCreativeCatalog::Category m_category = QcCreativeCatalog::Category::All;
    QString m_query;
    QList<const QcCreativeCatalog::Entry *> m_shown;
};
