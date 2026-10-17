#include "app/models/QcHandbookModel.h"

#include "common/handbook/QcHandbookJson.h"

using namespace Qt::StringLiterals;

QcHandbookModel::QcHandbookModel(QObject *parent)
    : QObject(parent)
{
}

void QcHandbookModel::setHandbook(std::shared_ptr<const QcHandbook> handbook)
{
    m_handbook = std::move(handbook);
    m_categories = m_handbook ? QcHandbookJson::categories(*m_handbook).toVariantList() : QVariantList();
    emit handbookChanged();
}

QVariantMap QcHandbookModel::summary(const QcHandbookEntry &entry) const
{
    QVariantMap map = QcHandbookJson::summary(entry).toVariantMap();
    map.insert(u"packName"_s, m_handbook->packName(entry.pack));
    const QcHandbook::Category *category = m_handbook->category(entry.category);
    map.insert(u"categoryTitle"_s, category ? category->title : entry.category);
    return map;
}

QVariantList QcHandbookModel::entries(const QString &category) const
{
    QVariantList list;
    const QcHandbook::Category *found = m_handbook ? m_handbook->category(category) : nullptr;
    if (!found) {
        return list;
    }

    list.reserve(found->entries.size() + 2);
    // Pack headings only when more than the base game is loaded.
    const bool headings = m_handbook->packs().size() > 1;
    QString pack;
    for (const qsizetype index : found->entries) {
        const QcHandbookEntry &entry = m_handbook->entries()[index];
        if (headings && (list.isEmpty() || entry.pack != pack)) {
            pack = entry.pack;
            list.append(QVariantMap{{u"header"_s, m_handbook->packName(pack)}});
        }
        list.append(summary(entry));
    }
    return list;
}

QVariantList QcHandbookModel::search(const QString &text) const
{
    QVariantList list;
    if (!m_handbook) {
        return list;
    }

    const QList<qsizetype> found = m_handbook->search(text);
    for (const qsizetype index : found) {
        list.append(summary(m_handbook->entries()[index]));
    }
    return list;
}

QVariantMap QcHandbookModel::page(const QString &id) const
{
    const QcHandbookEntry *entry = m_handbook ? m_handbook->entry(id) : nullptr;
    return entry ? QcHandbookJson::entry(*m_handbook, *entry).toVariantMap() : QVariantMap();
}

QString QcHandbookModel::resolve(const QString &target) const
{
    if (!m_handbook || target.isEmpty()) {
        return {};
    }
    if (m_handbook->entry(target)) {
        return target;
    }

    // Unqualified names ("zombie") are looked up in every pack, base game first.
    QStringList names{target};
    if (!target.contains(u':')) {
        names.clear();
        for (const QcHandbook::Pack &pack : m_handbook->packs()) {
            names.append(pack.id + u':' + target);
        }
    }

    for (const QString &name : std::as_const(names)) {
        for (const QString &prefix : {u"block:"_s, u"item:"_s, u"mob:"_s, u"biome:"_s, u"chapter:"_s}) {
            if (m_handbook->entry(prefix + name)) {
                return prefix + name;
            }
        }
    }
    return {};
}
