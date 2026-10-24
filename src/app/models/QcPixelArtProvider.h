#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QString>

// Serves the pixel art of the workbench screens (enchanting table and anvil, decision E-884) to QML
// as image://pixelart/<name>[/<colour role>], drawn by the game itself like its textures:
// - rune/<index>/<role>: a rune of the enchanting table (QcRuneGlyphs) in a colour of the palette
//   (QcUiPalette, by its QML name such as "ink"),
// - arrow/<role>, plus/<role>, cross/<role>, spark/<role>: one-colour symbols (the anvil's plus and
//   arrow, the cross over a forbidden result, the spark beside a level),
// - book: the open book of the enchanting table in its own colours, like an item texture.
// Each picture comes at its pixel size; QML scales it up without smoothing (PixelArt.qml). An unknown
// name or role gives an empty image and a warning.
class QcPixelArtProvider : public QQuickImageProvider
{
    Q_OBJECT

public:
    static constexpr auto providerId = "pixelart";

    QcPixelArtProvider();

    // The picture of `id` ("rune/12/ink", "book"); a null image for an unknown one.
    [[nodiscard]] static QImage picture(const QString &id);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
