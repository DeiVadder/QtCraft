// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/handbook/QcHandbook.h"

#include <QByteArray>
#include <QList>
#include <QString>

/*! \brief Markdown export of a handbook: index.md and one file per non-empty category, with
    relative links between entries.

    The output depends only on the handbook, so the same content gives the same bytes. */
namespace QcHandbookMarkdown {

/*! \brief One file of the export. */
struct File
{
    /*! The file name. */
    QString name;
    /*! The file's bytes. */
    QByteArray content;
};

/*! Returns the files of the export of \a handbook: index.md first, then one per non-empty
    category. */
QList<File> files(const QcHandbook &handbook);
/*! Returns the file name of \a category ("blocks.md"). */
QString fileName(const QString &category);
/*! Returns the anchor of the entry \a entryId ("block-qtcraft-stone"). */
QString anchor(const QString &entryId);
/*! Writes files() of \a handbook into \a directory (created when missing); existing files of the
    same names are replaced. */
[[nodiscard]] QcStatus write(const QcHandbook &handbook, const QString &directory);

} // namespace QcHandbookMarkdown
