#include "QcAppIcon.h"

#include "QcTextureAtlas.h"

#include <QDir>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <QTransform>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace {

// Proportions of the macOS icon grid: the tile covers 824 of 1024 pixels with rounded corners.
constexpr double tileInset = 100.0 / 1024.0;
constexpr double cornerRadius = 185.0 / 1024.0;
// Edge length of the block and the centre of its top face's lower corner, relative to the size.
constexpr double blockEdge = 0.27;
constexpr double blockCentreX = 0.45;
constexpr double blockCentreY = 0.52;
// Face shading as in the world: top 1.0, the two visible sides darker.
constexpr int leftShadeAlpha = 50;
constexpr int rightShadeAlpha = 100;

// Parallelogram with corner `origin` and edges `u` (texture x) and `v` (texture y).
struct Parallelogram
{
    QPointF origin;
    QPointF u;
    QPointF v;
};

// Draws `texture` (square, pixel art) onto `face`, darkened by `shadeAlpha`.
void drawFace(QPainter &painter, const QImage &texture, const Parallelogram &face, int shadeAlpha)
{
    const auto &[origin, u, v] = face;
    const double pixels = texture.width();
    const QTransform transform(u.x() / pixels, u.y() / pixels, v.x() / pixels, v.y() / pixels, origin.x(), origin.y());
    painter.save();
    painter.setTransform(transform, false);
    painter.drawImage(QRectF(0, 0, pixels, pixels), texture);
    if (shadeAlpha > 0) {
        painter.fillRect(QRectF(0, 0, pixels, pixels), QColor(0, 0, 0, shadeAlpha));
    }
    painter.restore();
}

} // namespace

namespace QcAppIcon {

QList<IconFile> iconSetFiles()
{
    QList<IconFile> files;
    for (const int points : {16, 32, 128, 256, 512}) {
        files.append({u"icon_%1x%1.png"_s.arg(points), points});
        files.append({u"icon_%1x%1@2x.png"_s.arg(points), points * 2});
    }
    return files;
}

QImage render(const QImage &top, const QImage &side, const QImage &tool, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Pixel art stays crisp at every size.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const double s = size;
    const QRectF tile(s * tileInset, s * tileInset, s * (1.0 - 2.0 * tileInset), s * (1.0 - 2.0 * tileInset));
    QPainterPath rounded;
    rounded.addRoundedRect(tile, s * cornerRadius, s * cornerRadius);
    QLinearGradient sky(tile.topLeft(), tile.bottomLeft());
    sky.setColorAt(0.0, QColor(0x8f, 0xd3, 0xf5));
    sky.setColorAt(1.0, QColor(0x2f, 0x7f, 0xc8));
    painter.fillPath(rounded, sky);
    painter.setClipPath(rounded);

    const double edge = s * blockEdge;
    const double across = edge * std::cos(std::numbers::pi / 6.0);
    const QPointF centre(s * blockCentreX, s * blockCentreY);
    const QPointF left(centre.x() - across, centre.y() - edge / 2.0);
    const QPointF right(centre.x() + across, centre.y() - edge / 2.0);
    const QPointF down(0.0, edge);

    // Soft shadow under the block: a radial gradient squashed into an ellipse.
    painter.save();
    painter.translate(centre.x() + edge * 0.1, centre.y() + edge * 1.02);
    painter.scale(1.0, 0.22);
    QRadialGradient shadow(QPointF(0.0, 0.0), edge * 1.1);
    shadow.setColorAt(0.0, QColor(0, 0, 0, 110));
    shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(shadow);
    painter.drawEllipse(QPointF(0.0, 0.0), edge * 1.1, edge * 1.1);
    painter.restore();

    drawFace(painter, top, {left, QPointF(across, -edge / 2.0), centre - left}, 0);
    drawFace(painter, side, {left, centre - left, down}, leftShadeAlpha);
    drawFace(painter, side, {centre, right - centre, down}, rightShadeAlpha);

    // The pickaxe leans against the block's right side; its texture runs from the handle at the
    // bottom left to the head at the top right.
    const double toolSize = s * 0.40;
    painter.drawImage(QRectF(s * 0.50, s * 0.44, toolSize, toolSize), tool);
    painter.end();
    return image;
}

QcStatus writeIconSet(const QcTextureAtlas &atlas, const QString &directory)
{
    const QString topName = u"qtcraft:grass_top"_s;
    const QString sideName = u"qtcraft:grass_side"_s;
    const QString toolName = u"qtcraft:iron_pickaxe"_s;

    for (const QString &name : {topName, sideName, toolName}) {
        if (!atlas.contains(name)) {
            return qcError(u"Texture %1 is missing"_s.arg(name));
        }
    }

    if (!QDir().mkpath(directory)) {
        return qcError(u"Cannot create %1"_s.arg(directory));
    }

    const QImage top = atlas.tile(topName);
    const QImage side = atlas.tile(sideName);
    const QImage tool = atlas.tile(toolName);
    const QList<IconFile> files = iconSetFiles();
    for (const IconFile &file : files) {
        const QString path = QDir(directory).filePath(file.name);
        if (!render(top, side, tool, file.pixels).save(path, "PNG")) {
            return qcError(u"Cannot write %1"_s.arg(path));
        }
    }
    return {};
}

} // namespace QcAppIcon
