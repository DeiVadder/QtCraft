// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/handbook/QcHandbook.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/*! \brief Data of the web handbook (web/handbook/data, shown by web/handbook/handbook.js like the
    handbook screen of the game): one JSON file per language with the packs, the categories with
    their entries in list order and every page in the form of QcHandbookJson::entry, each with the
    English title the search compares (QcHandbook::search).

    The same handbook gives the same bytes. */
namespace QcHandbookWeb {

/*! \brief One file of the data. */
struct File
{
    /*! The file name. */
    QString name;
    /*! The file's bytes. */
    QByteArray content;
};

/*! Returns the data file of \a handbook: {"language", "packs": [{"id", "name", "version"}],
    "categories": [{"id", "title", "entries": [id]}], "entries": {id: page with "englishTitle"}};
    categories without entries are left out. */
File data(const QcHandbook &handbook);
/*! Returns languages.json for the languages \a ids: {"languages": [{"id", "name"}]} with each
    language's own name ("Deutsch"). */
File languages(const QStringList &ids);
/*! Item names whose icons the pages of \a handbook show (entries, links, recipe cells), sorted. */
QStringList icons(const QcHandbook &handbook);
/*! Writes \a files into \a directory (created when missing); existing files of the same names
    are replaced. */
[[nodiscard]] QcStatus write(const QList<File> &files, const QString &directory);

} // namespace QcHandbookWeb
