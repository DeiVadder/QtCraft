#include "common/handbook/QcHandbookMarkdown.h"

#include <QDir>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace {

QString escaped(QString text)
{
    return text.replace(u'|', u"\\|"_s).replace(u'\n', u' ');
}

class Writer
{
public:
    Writer(const QcHandbook &handbook, QString category)
        : m_handbook(handbook)
        , m_category(std::move(category))
    {
    }

    QString link(const QcHandbookEntry::Link &link) const;
    QString links(const QList<QcHandbookEntry::Link> &list) const;
    QString recipe(const QcHandbookEntry::Recipe &recipe) const;
    QString entry(const QcHandbookEntry &entry) const;

private:
    const QcHandbook &m_handbook;
    QString m_category;
};

QString Writer::link(const QcHandbookEntry::Link &link) const
{
    QString text = escaped(link.text);
    if (!link.target.isEmpty()) {
        const QcHandbookEntry *target = m_handbook.entry(link.target);
        const QString file = target && target->category != m_category ? QcHandbookMarkdown::fileName(target->category) : QString();
        text = u"[%1](%2#%3)"_s.arg(text, file, QcHandbookMarkdown::anchor(link.target));
    }
    if (!link.note.isEmpty()) {
        text += u" (%1)"_s.arg(escaped(link.note));
    }
    return text;
}

QString Writer::links(const QList<QcHandbookEntry::Link> &list) const
{
    QStringList parts;
    for (const QcHandbookEntry::Link &item : list) {
        parts.append(link(item));
    }
    return parts.join(u", "_s);
}

QString Writer::recipe(const QcHandbookEntry::Recipe &recipe) const
{
    QString text = u"*%1*\n\n"_s.arg(escaped(recipe.note));
    const QString result = recipe.resultCount > 1 ? u"%1 × %2"_s.arg(recipe.resultCount).arg(link(recipe.result)) : link(recipe.result);

    QStringList header;
    for (int column = 0; column < recipe.width; ++column) {
        header.append(QString::number(column + 1));
    }
    header.append(u"→"_s);
    text += u"| "_s + header.join(u" | "_s) + u" |\n|"_s + QString(u"---|"_s).repeated(recipe.width + 1) + u'\n';

    for (int row = 0; row < recipe.height; ++row) {
        QStringList cells;
        for (int column = 0; column < recipe.width; ++column) {
            const qsizetype index = static_cast<qsizetype>(row) * recipe.width + column;
            const QcHandbookEntry::Link cell = index < recipe.cells.size() ? recipe.cells[index] : QcHandbookEntry::Link{};
            cells.append(cell.isEmpty() ? u"·"_s : link(cell));
        }
        cells.append(row == 0 ? result : QString());
        text += u"| "_s + cells.join(u" | "_s) + u" |\n"_s;
    }

    if (!recipe.machines.isEmpty()) {
        text += u"\n"_s + links(recipe.machines) + u'\n';
    }
    return text;
}

QString Writer::entry(const QcHandbookEntry &entry) const
{
    QString text = u"### <a id=\"%1\"></a>%2\n\n"_s.arg(QcHandbookMarkdown::anchor(entry.id), entry.title);
    if (!entry.text.isEmpty()) {
        text += entry.text + u"\n\n"_s;
    }

    if (!entry.facts.isEmpty()) {
        text += u"| | |\n|---|---|\n"_s;
        for (const QcHandbookEntry::Fact &fact : entry.facts) {
            QString value = escaped(fact.value);
            if (fact.label == m_handbook.label(u"fact.id"_s) && !value.isEmpty()) {
                value = u'`' + value + u'`';
            }
            const QString linked = links(fact.links);
            if (!linked.isEmpty()) {
                value = value.isEmpty() ? linked : value + u": "_s + linked;
            }
            text += u"| %1 | %2 |\n"_s.arg(escaped(fact.label), value);
        }
        text += u'\n';
    }

    for (const QcHandbookEntry::Section &section : entry.sections) {
        if (!section.title.isEmpty()) {
            text += u"**%1**\n\n"_s.arg(section.title);
        }
        if (!section.text.isEmpty()) {
            text += section.text + u"\n\n"_s;
        }
        for (const QcHandbookEntry::Recipe &item : section.recipes) {
            text += recipe(item) + u'\n';
        }
        if (!section.links.isEmpty()) {
            text += links(section.links) + u"\n\n"_s;
        }
    }
    return text;
}

} // namespace

namespace QcHandbookMarkdown {

QString fileName(const QString &category)
{
    return category + u".md"_s;
}

QString anchor(const QString &entryId)
{
    QString result = entryId.toLower();
    for (QChar &c : result) {
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != u'_') {
            c = u'-';
        }
    }
    return result;
}

QList<File> files(const QcHandbook &handbook)
{
    QList<File> result;
    QString index = u"# %1\n\n%2\n\n**%3:** "_s.arg(handbook.label(u"title"_s), handbook.label(u"intro"_s), handbook.label(u"packs"_s));
    QStringList packs;
    for (const QcHandbook::Pack &pack : handbook.packs()) {
        packs.append(pack.version.isEmpty() ? pack.name : u"%1 %2"_s.arg(pack.name, pack.version));
    }
    index += packs.join(u", "_s) + u"\n\n## "_s + handbook.label(u"contents"_s) + u"\n\n"_s;

    for (const QcHandbook::Category &category : handbook.categories()) {
        if (category.entries.isEmpty()) {
            continue;
        }

        index += u"- [%1](%2) – %3\n"_s.arg(category.title, fileName(category.id), handbook.label(u"entries"_s).arg(category.entries.size()));
        const Writer writer(handbook, category.id);
        QString text = u"# %1\n\n[%2](index.md)\n\n"_s.arg(category.title, handbook.label(u"index"_s));

        // Contents of the file first, grouped by pack like the entries below.
        QString contents;
        QString body;
        QString pack;
        bool first = true;
        for (const qsizetype entryIndex : category.entries) {
            const QcHandbookEntry &entry = handbook.entries()[entryIndex];
            if (first || entry.pack != pack) {
                pack = entry.pack;
                first = false;
                contents += u"\n**%1:** "_s.arg(handbook.packName(pack));
                body += u"## %1\n\n"_s.arg(handbook.packName(pack));
            } else {
                contents += u" · "_s;
            }
            contents += u"[%1](#%2)"_s.arg(escaped(entry.title), anchor(entry.id));
            body += writer.entry(entry);
        }

        text += contents.mid(1) + u"\n\n"_s + body;

        // One trailing newline, whatever the last entry ended with.
        while (text.endsWith(u"\n\n"_s)) {
            text.chop(1);
        }
        result.append({fileName(category.id), text.toUtf8()});
    }

    result.prepend({u"index.md"_s, index.toUtf8()});
    return result;
}

[[nodiscard]] QcStatus write(const QcHandbook &handbook, const QString &directory)
{
    if (!QDir().mkpath(directory)) {
        return qcError(u"Cannot create %1"_s.arg(directory));
    }

    const QList<File> written = files(handbook);
    for (const File &file : written) {
        QSaveFile out(QDir(directory).filePath(file.name));
        if (!out.open(QIODevice::WriteOnly) || out.write(file.content) != file.content.size() || !out.commit()) {
            return qcError(u"Cannot write %1: %2"_s.arg(out.fileName(), out.errorString()));
        }
    }
    return {};
}

} // namespace QcHandbookMarkdown
