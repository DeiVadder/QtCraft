// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/input/QcPadGlyphPainter.h"
#include "clientcore/settings/QcUiPalette.h"

#include <QImage>
#include <QPainter>
#include <QTest>

#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

using Role = QcUiPalette::Role;
using Shape = QcPadGlyphPainter::Shape;

// The glyph `symbol` painted offscreen into a transparent image of `size`.
QImage paintGlyph(const QVariantMap &symbol, QSize size)
{
    QcPadGlyphPainter glyph;
    glyph.setSize(size);
    glyph.setSymbol(symbol);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    glyph.paint(&painter);
    painter.end();
    return image;
}

// Whether `actual` is `expected` up to rounding (each channel within 2).
bool near(QColor actual, QColor expected)
{
    constexpr int tolerance = 2;
    return std::abs(actual.red() - expected.red()) <= tolerance && std::abs(actual.green() - expected.green()) <= tolerance
           && std::abs(actual.blue() - expected.blue()) <= tolerance && std::abs(actual.alpha() - expected.alpha()) <= tolerance;
}

} // namespace

// The pad glyphs as pixels (QcPadGlyphPainter, decision E-612): a trigger's tab and the d-pad's
// arms land where the shapes put them, in the palette's colours.
class TestPadGlyphPainter : public QObject
{
    Q_OBJECT

private slots:
    // A trigger is a tab with a round top: its body and bottom corners are the backing, the top
    // corners stay clear, and it has no mark of its own.
    void triggerIsATab()
    {
        const QImage image = paintGlyph({{u"shape"_s, static_cast<int>(Shape::Trigger)}, {u"label"_s, u"RT"_s}}, QSize(40, 30));
        const QColor backing = QcUiPalette::color(Role::PadGlyph);
        QVERIFY2(near(image.pixelColor(20, 15), backing), qPrintable(image.pixelColor(20, 15).name(QColor::HexArgb)));
        QVERIFY(near(image.pixelColor(3, 27), backing));
        QVERIFY(near(image.pixelColor(36, 27), backing));
        QCOMPARE(image.pixelColor(1, 1).alpha(), 0);
        QCOMPARE(image.pixelColor(38, 1).alpha(), 0);
    }

    // The d-pad's lit arm (up) is drawn in the text colour, the others faint over the backing, and
    // the middle between them is the backing; outside the disc stays clear.
    void dpadLightsItsDirection()
    {
        const QImage image = paintGlyph({{u"shape"_s, static_cast<int>(Shape::Dpad)}, {u"direction"_s, u"up"_s}}, QSize(40, 40));
        const QColor backing = QcUiPalette::color(Role::PadGlyph);
        const QColor text = QcUiPalette::color(Role::Text);
        QVERIFY2(near(image.pixelColor(20, 10), text), qPrintable(image.pixelColor(20, 10).name(QColor::HexArgb)));
        QVERIFY(near(image.pixelColor(20, 20), backing));

        const QColor faint = image.pixelColor(20, 30);
        QVERIFY(!near(faint, backing));
        QVERIFY(!near(faint, QcUiPalette::color(Role::TextMuted)));
        QVERIFY(faint.lightness() > backing.lightness());
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(39, 39).alpha(), 0);
    }
};

QTEST_MAIN(TestPadGlyphPainter)
#include "test_pad_glyph_painter.moc"
