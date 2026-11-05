// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

class QcTextureAtlas;

/*! \brief The application icon: a grass block with an iron pickaxe on a rounded sky tile, drawn
    from the game's own procedural textures, so it needs no image files. */
namespace QcAppIcon {

/*! \brief One image of a macOS .iconset. */
struct IconFile
{
    /*! File name inside an .iconset directory, as iconutil expects it. */
    QString name;
    /*! Edge length of the image in pixels. */
    int pixels = 0;
};

/*! Returns the ten images of a macOS .iconset (16 to 512 points at 1x and 2x). */
QList<IconFile> iconSetFiles();

/*! Returns the square icon of \a size pixels from the three texture tiles \a top and \a side of the
    block and \a tool. */
QImage render(const QImage &top, const QImage &side, const QImage &tool, int size);

/*! Returns the edge lengths of the images in the Windows icon (.ico): 16 to 256 pixels, the
    sizes Explorer, the taskbar and the title bar pick from at 100 % to 200 % display scaling. */
QList<int> icoSizes();

/*! Returns a Windows .ico file holding \a images (square, at most 256 pixels): 32-bit BMP
    entries with an all-opaque mask below 256 pixels, a PNG entry for 256 pixels (as Windows
    Vista and later read it). */
QByteArray encodeIco(const QList<QImage> &images);

/*! Renders the icon from the textures of \a atlas qtcraft:grass_top, qtcraft:grass_side and
    qtcraft:iron_pickaxe and writes every image of iconSetFiles() into \a directory; returns the
    error when a file cannot be written. */
[[nodiscard]] QcStatus writeIconSet(const QcTextureAtlas &atlas, const QString &directory);

/*! Renders the icon from \a atlas at every size of icoSizes() and writes it as a Windows .ico
    file to \a path (cmake/windows/QtCraft.ico, embedded into the Windows executables); returns
    the error when it cannot be written. */
[[nodiscard]] QcStatus writeIco(const QcTextureAtlas &atlas, const QString &path);

} // namespace QcAppIcon
