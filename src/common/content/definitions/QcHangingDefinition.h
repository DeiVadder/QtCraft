// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

#include <optional>

// What an item hung on a block face becomes (decision E-790): an item frame, which holds one item
// it shows turned in eighths, or a painting, which shows one of its item's motifs.
enum class QcHangingType : quint8 {
    ItemFrame,
    Painting,
};

inline constexpr int qcHangingTypeCount = 2;

// One picture a painting can show (decision E-791): its size in blocks and its texture. A texture
// larger than one block is one procedural texture with `tiles` (QcContentParser::textureTiles),
// drawn block by block from the tiles "<texture>_<column>_<row>" (column 0 at the left, row 0 at
// the top as seen from the front).
struct QcPaintingMotif
{
    // Paintings are at most this many blocks wide and high.
    static constexpr int maxSide = 4;

    QString name;
    QString texture;
    int width = 1;
    int height = 1;
    // The names of the block textures, row by row (nameTiles()); empty for a motif of one block.
    QList<QString> tiles;

    // The name of the block texture in `column` and `row` of the procedural `texture`.
    static QString tileName(const QString &texture, int column, int row)
    {
        return texture + QLatin1Char('_') + QString::number(column) + QLatin1Char('_') + QString::number(row);
    }

    // Names the block textures once from the texture and the size; whoever makes a motif calls it
    // (parser, item serializer), so drawing builds no names.
    void nameTiles()
    {
        tiles.clear();
        if (width * height <= 1) {
            return;
        }
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                tiles.append(tileName(texture, column, row));
            }
        }
    }

    // The texture of the block in `column` and `row`; the texture itself for a motif of one block.
    const QString &tileTexture(int column, int row) const { return tiles.isEmpty() ? texture : tiles.at(static_cast<qsizetype>(row) * width + column); }

    bool operator==(const QcPaintingMotif &) const = default;
};

// The field "hangs" of an item (decision E-790): used on a block face, the item hangs an entity of
// `type` there that drops the item again.
struct QcHangingDefinition
{
    // Motifs one painting item offers at most.
    static constexpr int maxMotifs = 64;

    QcHangingType type = QcHangingType::ItemFrame;
    // Paintings only: the motifs to choose from on placement.
    QList<QcPaintingMotif> motifs;
    // Paintings only: the texture of its back and edges.
    QString back;

    bool operator==(const QcHangingDefinition &) const = default;
};
