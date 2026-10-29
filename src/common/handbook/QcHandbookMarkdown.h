// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/handbook/QcHandbook.h"

#include <QByteArray>
#include <QList>
#include <QString>

// Markdown export of a handbook: index.md and one file per non-empty category, with relative links
// between entries. The output depends only on the handbook, so the same content gives the same bytes.
namespace QcHandbookMarkdown {

struct File
{
    QString name;
    QByteArray content;
};

QList<File> files(const QcHandbook &handbook);
// File name of a category ("blocks.md") and the anchor of an entry ("block-qtcraft-stone").
QString fileName(const QString &category);
QString anchor(const QString &entryId);
// Writes files() into `directory` (created when missing); existing files of the same names are replaced.
[[nodiscard]] QcStatus write(const QcHandbook &handbook, const QString &directory);

} // namespace QcHandbookMarkdown
