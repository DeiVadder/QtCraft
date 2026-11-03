// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QPainterPath;

// Draws one pad button's symbol (decision E-612, PadGlyph.qml): the backing (a tab for shoulders
// and triggers, a pill for words, else a disc) and the mark on it (the four PlayStation shapes as
// lines, a stick's ring, the d-pad's arms or the four dots of a generic pad with the one of the
// glyph's direction lit). The letters lie on it as a Text in QML. The glyph is
// GameController.gamepad.pads.glyph(family, input); the colours are the palette's (QcUiPalette).
class QcPadGlyphPainter : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PadGlyphPainter)
    Q_CLASSINFO("RegisterEnumClassesUnscoped", "false")
    // {shape (Shape), label, colour, direction ("up", "down", "left", "right" or empty)}.
    Q_PROPERTY(QVariantMap symbol READ symbol WRITE setSymbol NOTIFY symbolChanged FINAL)
    Q_PROPERTY(Shape shape READ shape NOTIFY symbolChanged FINAL)
    // Shoulders, triggers and words are wider than a disc.
    Q_PROPERTY(bool wide READ isWide NOTIFY symbolChanged FINAL)
    // The colour of the mark and the letters: the glyph's colour role, else the theme's text.
    Q_PROPERTY(QColor ink READ ink NOTIFY symbolChanged FINAL)

public:
    // The shapes of QcGamepadGlyphs::Shape, in its order.
    enum class Shape {
        Label,
        Cross,
        Circle,
        Square,
        Triangle,
        Position,
        Shoulder,
        Trigger,
        Stick,
        Dpad,
    };
    Q_ENUM(Shape)

    explicit QcPadGlyphPainter(QQuickItem *parent = nullptr);

    QVariantMap symbol() const { return m_symbol; }
    // Takes a glyph; an unknown shape is drawn as a disc and logged.
    void setSymbol(const QVariantMap &symbol);
    Shape shape() const { return m_shape; }
    bool isWide() const;
    QColor ink() const { return m_ink; }

    void paint(QPainter *painter) override;

signals:
    void symbolChanged();

private:
    // The outline of the backing in an item of the current size.
    QPainterPath backingPath() const;
    // The PlayStation shapes and the stick's ring, stroked in `ink`.
    void paintMark(QPainter *painter) const;
    // The d-pad's arms or the four dots, the one of the glyph's direction larger and in full
    // contrast, the others faint, so the direction reads at hint size.
    void paintDirections(QPainter *painter) const;

    QVariantMap m_symbol;
    Shape m_shape = Shape::Label;
    QColor m_ink;
};
