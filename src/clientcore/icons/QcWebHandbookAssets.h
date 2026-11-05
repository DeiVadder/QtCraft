// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbookMarkdown.h"
#include "common/handbook/QcHandbookWeb.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>

/*! \brief The pictures and colours of the web handbook (web/handbook/data): all item icons on
    one sheet, so the page loads one image instead of hundreds, and the colours of the game's
    screens as CSS variables, so the page looks like the handbook in the game (QcUiPalette). */
namespace QcWebHandbookAssets {

/*! Side of a cell of the icon sheet; every icon is drawn at this size; and the number of cells
    per row. \qcshared */
inline constexpr int iconSize = 64;
inline constexpr int sheetColumns = 16;

/*! Returns icons.png of \a icons (cells row by row, items sorted by name) and icons.json
    {"size", "columns", "icons": {item name: cell index}}. */
QList<QcHandbookMarkdown::File> iconSheet(const QHash<QString, QImage> &icons);
/*! Returns the cells of the icon sheet \a sheet back as images by item name, read with its
    \a index (icons.json; for tests). */
QHash<QString, QImage> readSheet(const QImage &sheet, const QByteArray &index);
/*! Returns theme.css: one variable --qc-<name> per QcUiPalette role (Theme.<name> in QML). */
QcHandbookMarkdown::File theme();

} // namespace QcWebHandbookAssets
