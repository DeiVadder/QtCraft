#pragma once

#include "common/core/QcResult.h"

#include <QImage>
#include <QList>
#include <QString>

class QcTextureAtlas;

// The application icon: a grass block with an iron pickaxe on a rounded sky tile, drawn from the
// game's own procedural textures, so it needs no image files.
namespace QcAppIcon {

struct IconFile
{
    // File name inside an .iconset directory, as iconutil expects it.
    QString name;
    int pixels = 0;
};

// The ten images of a macOS .iconset (16 to 512 points at 1x and 2x).
QList<IconFile> iconSetFiles();

// Square icon of `size` pixels from the three texture tiles (top and side of the block, the tool).
QImage render(const QImage &top, const QImage &side, const QImage &tool, int size);

// Renders the icon from the atlas textures qtcraft:grass_top, qtcraft:grass_side and
// qtcraft:iron_pickaxe and writes every image of iconSetFiles() into `directory`.
[[nodiscard]] QcStatus writeIconSet(const QcTextureAtlas &atlas, const QString &directory);

} // namespace QcAppIcon
