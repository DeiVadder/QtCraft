#pragma once

#include "QcHandbook.h"
#include "QcResult.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Data of the web handbook (web/handbook/data, shown by web/handbook/handbook.js like the handbook
// screen of the game): one JSON file per language with the packs, the categories with their entries
// in list order and every page in the form of QcHandbookJson::entry, each with the English title the
// search compares (QcHandbook::search). The same handbook gives the same bytes.
namespace QcHandbookWeb {

struct File
{
    QString name;
    QByteArray content;
};

// {"language", "packs": [{"id", "name", "version"}], "categories": [{"id", "title", "entries": [id]}],
//  "entries": {id: page with "englishTitle"}}; categories without entries are left out.
File data(const QcHandbook &handbook);
// languages.json: {"languages": [{"id", "name"}]} with each language's own name ("Deutsch").
File languages(const QStringList &ids);
// Item names whose icons the pages of `handbook` show (entries, links, recipe cells), sorted.
QStringList icons(const QcHandbook &handbook);
// Writes `files` into `directory` (created when missing); existing files of the same names are replaced.
QcStatus write(const QList<File> &files, const QString &directory);

} // namespace QcHandbookWeb
