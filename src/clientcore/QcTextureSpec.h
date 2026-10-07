#pragma once

#include <QColor>
#include <QString>

#include <optional>

// Data description of a procedurally generated block/item texture.
struct QcTextureSpec
{
    enum class Pattern {
        Noise,
        Speckle,
        Planks,
        Bricks,
        LogSide,
        LogTop,
        Leaves,
        Glass,
        GrassSide,
        Ore,
        Water,
        Tool,
        Lump,
    };

    Pattern pattern = Pattern::Noise;
    QColor primary = QColor(128, 128, 128);
    QColor secondary = QColor(96, 96, 96);
    double variation = 0.15;
    quint32 seed = 0;
};

std::optional<QcTextureSpec::Pattern> qcTexturePatternFromName(const QString &name);
