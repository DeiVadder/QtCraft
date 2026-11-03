// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/input/QcPadGlyphPainter.h"
#include "app/debug/QcAppLogging.h"
#include "clientcore/input/QcGamepadGlyphs.h"
#include "clientcore/settings/QcUiPalette.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace {

using Glyphs = QcGamepadGlyphs::Shape;
using Shape = QcPadGlyphPainter::Shape;
static_assert(static_cast<int>(Shape::Label) == static_cast<int>(Glyphs::Label));
static_assert(static_cast<int>(Shape::Cross) == static_cast<int>(Glyphs::Cross));
static_assert(static_cast<int>(Shape::Circle) == static_cast<int>(Glyphs::Circle));
static_assert(static_cast<int>(Shape::Square) == static_cast<int>(Glyphs::Square));
static_assert(static_cast<int>(Shape::Triangle) == static_cast<int>(Glyphs::Triangle));
static_assert(static_cast<int>(Shape::Position) == static_cast<int>(Glyphs::Position));
static_assert(static_cast<int>(Shape::Shoulder) == static_cast<int>(Glyphs::Shoulder));
static_assert(static_cast<int>(Shape::Trigger) == static_cast<int>(Glyphs::Trigger));
static_assert(static_cast<int>(Shape::Stick) == static_cast<int>(Glyphs::Stick));
static_assert(static_cast<int>(Shape::Dpad) == static_cast<int>(Glyphs::Dpad));

// The palette role of a glyph's colour name; the text's for none or an unknown one.
[[nodiscard]] QcUiPalette::Role inkRole(const QString &colour)
{
    static const QHash<QString, QcUiPalette::Role> roles{{u"green"_s, QcUiPalette::Role::PadGreen},
                                                        {u"red"_s, QcUiPalette::Role::PadRed},
                                                        {u"blue"_s, QcUiPalette::Role::PadBlue},
                                                        {u"yellow"_s, QcUiPalette::Role::PadYellow},
                                                        {u"pink"_s, QcUiPalette::Role::PadPink}};
    return roles.value(colour, QcUiPalette::Role::Text);
}

// One arm of the d-pad or one dot of the diamond: its direction's name and where it points.
struct Arm
{
    QStringView name;
    double dx = 0.0;
    double dy = 0.0;
};

constexpr std::array<Arm, 4> arms{{{u"up", 0.0, -1.0}, {u"down", 0.0, 1.0}, {u"left", -1.0, 0.0}, {u"right", 1.0, 0.0}}};

} // namespace

QcPadGlyphPainter::QcPadGlyphPainter(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_ink(QcUiPalette::color(QcUiPalette::Role::Text))
{
    setAntialiasing(true);
}

void QcPadGlyphPainter::setSymbol(const QVariantMap &symbol)
{
    if (symbol == m_symbol) {
        return; // nothing changed
    }
    m_symbol = symbol;
    const int value = symbol.value(u"shape"_s, static_cast<int>(Shape::Label)).toInt();
    const bool known = value >= static_cast<int>(Shape::Label) && value <= static_cast<int>(Shape::Dpad);
    if (!known) {
        qCWarning(lcGamepad) << "pad glyph: unknown shape" << symbol.value(u"shape"_s) << "drawn as a disc";
    }
    m_shape = known ? static_cast<Shape>(value) : Shape::Label;
    m_ink = QcUiPalette::color(inkRole(symbol.value(u"colour"_s).toString()));
    emit symbolChanged();
    update();
}

bool QcPadGlyphPainter::isWide() const
{
    return shape() == Shape::Shoulder || shape() == Shape::Trigger
           || (shape() == Shape::Label && symbol().value(u"label"_s).toString().size() > 1);
}

void QcPadGlyphPainter::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillPath(backingPath(), QcUiPalette::color(QcUiPalette::Role::PadGlyph));

    if (shape() == Shape::Dpad || shape() == Shape::Position) {
        paintDirections(painter);
    } else {
        paintMark(painter);
    }
}

QPainterPath QcPadGlyphPainter::backingPath() const
{
    const double w = width();
    const double h = height();
    QPainterPath path;
    if (shape() == Shape::Trigger) {
        // A tab with a round top.
        path.moveTo(1.0, h - 1.0);
        path.lineTo(1.0, h * 0.45);
        path.quadTo(1.0, 1.0, w * 0.45, 1.0);
        path.lineTo(w * 0.55, 1.0);
        path.quadTo(w - 1.0, 1.0, w - 1.0, h * 0.45);
        path.lineTo(w - 1.0, h - 1.0);
        path.closeSubpath();
    } else if (isWide()) {
        // A shoulder's tab with round corners, or the pill of a word.
        const double corner = shape() == Shape::Shoulder ? h / 4.0 : h / 2.0 - 1.0;
        path.addRoundedRect(QRectF(1.0, 1.0, w - 2.0, h - 2.0), corner, corner);
    } else {
        const double radius = std::min(w, h) / 2.0 - 1.0;
        path.addEllipse(QPointF(w / 2.0, h / 2.0), radius, radius);
    }
    return path;
}

void QcPadGlyphPainter::paintMark(QPainter *painter) const
{
    const QPointF centre(width() / 2.0, height() / 2.0);
    const double r = std::min(width(), height()) / 2.0;
    const double s = r * 0.45;
    painter->setPen(QPen(ink(), std::max(1.5, height() / 11.0)));
    painter->setBrush(Qt::NoBrush);

    switch (shape()) {
    case Shape::Cross:
        painter->drawLine(centre + QPointF(-s, -s), centre + QPointF(s, s));
        painter->drawLine(centre + QPointF(s, -s), centre + QPointF(-s, s));
        break;
    case Shape::Circle:
        painter->drawEllipse(centre, s, s);
        break;
    case Shape::Square:
        painter->drawRect(QRectF(centre.x() - s * 0.85, centre.y() - s * 0.85, s * 1.7, s * 1.7));
        break;
    case Shape::Triangle: {
        const std::array<QPointF, 3> corners{centre + QPointF(0.0, -s), centre + QPointF(s * 1.05, s * 0.75),
                                             centre + QPointF(-s * 1.05, s * 0.75)};
        painter->drawPolygon(corners.data(), static_cast<int>(corners.size()));
        break;
    }
    case Shape::Stick:
        painter->drawEllipse(centre, r * 0.72, r * 0.72);
        break;
    default:
        break; // a disc, a tab or a pill with letters only
    }
}

void QcPadGlyphPainter::paintDirections(QPainter *painter) const
{
    const QPointF centre(width() / 2.0, height() / 2.0);
    const double r = std::min(width(), height()) / 2.0;
    const QString direction = symbol().value(u"direction"_s).toString();
    painter->setPen(Qt::NoPen);

    for (const Arm &arm : arms) {
        const bool lit = arm.name == direction;
        const QPointF at = centre + QPointF(arm.dx, arm.dy) * r * 0.5;
        painter->setOpacity(lit ? 1.0 : 0.3);
        painter->setBrush(QcUiPalette::color(lit ? QcUiPalette::Role::Text : QcUiPalette::Role::TextMuted));
        if (shape() == Shape::Dpad) {
            const double half = r * (lit ? 0.34 : 0.22);
            painter->drawRect(QRectF(at.x() - half, at.y() - half, 2.0 * half, 2.0 * half));
        } else {
            const double radius = r * (lit ? 0.26 : 0.17);
            painter->drawEllipse(at, radius, radius);
        }
    }
    painter->setOpacity(1.0);
}
