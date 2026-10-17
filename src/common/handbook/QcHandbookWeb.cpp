#include "common/handbook/QcHandbookWeb.h"

#include "common/handbook/QcHandbookJson.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

void addIcons(const QList<QcHandbookEntry::Link> &links, QSet<QString> &names)
{
    for (const QcHandbookEntry::Link &link : links) {
        if (!link.icon.isEmpty()) {
            names.insert(link.icon);
        }
    }
}

} // namespace

namespace QcHandbookWeb {

File data(const QcHandbook &handbook)
{
    QJsonArray packs;
    for (const QcHandbook::Pack &pack : handbook.packs()) {
        packs.append(QJsonObject{{u"id"_s, pack.id}, {u"name"_s, pack.name}, {u"version"_s, pack.version}});
    }

    QJsonArray categories;
    for (const QcHandbook::Category &category : handbook.categories()) {
        if (category.entries.isEmpty()) {
            continue;
        }
        QJsonArray ids;
        for (const qsizetype index : category.entries) {
            ids.append(handbook.entries()[index].id);
        }
        categories.append(QJsonObject{{u"id"_s, category.id}, {u"title"_s, category.title}, {u"entries"_s, ids}});
    }

    QJsonObject entries;
    for (const QcHandbookEntry &entry : handbook.entries()) {
        QJsonObject page = QcHandbookJson::entry(handbook, entry);
        page.insert(u"englishTitle"_s, entry.englishTitle);
        entries.insert(entry.id, page);
    }

    const QJsonObject root{{u"language"_s, handbook.language()}, {u"packs"_s, packs}, {u"categories"_s, categories}, {u"entries"_s, entries}};
    return {handbook.language() + u".json"_s, QJsonDocument(root).toJson(QJsonDocument::Indented)};
}

File languages(const QStringList &ids)
{
    QJsonArray list;
    for (const QString &id : ids) {
        // Without a territory: "English", not "American English".
        const QLocale locale(QLocale(id).language(), QLocale::World);
        QString name = locale.nativeLanguageName();
        if (!name.isEmpty()) {
            name[0] = name[0].toUpper();
        }
        list.append(QJsonObject{{u"id"_s, id}, {u"name"_s, name}});
    }
    return {u"languages.json"_s, QJsonDocument(QJsonObject{{u"languages"_s, list}}).toJson(QJsonDocument::Indented)};
}

QStringList icons(const QcHandbook &handbook)
{
    QSet<QString> names;
    for (const QcHandbookEntry &entry : handbook.entries()) {
        if (!entry.icon.isEmpty()) {
            names.insert(entry.icon);
        }
        for (const QcHandbookEntry::Fact &fact : entry.facts) {
            addIcons(fact.links, names);
        }
        for (const QcHandbookEntry::Section &section : entry.sections) {
            addIcons(section.links, names);
            for (const QcHandbookEntry::Recipe &recipe : section.recipes) {
                addIcons(recipe.cells, names);
                addIcons(recipe.machines, names);
                addIcons({recipe.result}, names);
            }
        }
    }
    QStringList sorted(names.begin(), names.end());
    std::ranges::sort(sorted);
    return sorted;
}

[[nodiscard]] QcStatus write(const QList<File> &files, const QString &directory)
{
    if (!QDir().mkpath(directory)) {
        return qcError(u"Cannot create %1"_s.arg(directory));
    }

    for (const File &file : files) {
        QSaveFile out(QDir(directory).filePath(file.name));
        if (!out.open(QIODevice::WriteOnly) || out.write(file.content) != file.content.size() || !out.commit()) {
            return qcError(u"Cannot write %1: %2"_s.arg(out.fileName(), out.errorString()));
        }
    }
    return {};
}

} // namespace QcHandbookWeb
