// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCoreApplication>
#include <QString>
#include <QVariantList>

class QcContentRegistry;
struct QcWorldMapResult;

// The texts of the map preview in the player's language (decision E-921): the names of the biomes
// and of the kinds of sites from the content's translations, of the sea from the interface's, the
// legend of a map and what a cell of it shows.
class QcWorldMapTexts
{
    Q_DECLARE_TR_FUNCTIONS(QcWorldMapTexts)

public:
    // `content` may be null (released): every name is then empty.
    QcWorldMapTexts(const QcContentRegistry *content, QString language);

    // The name of biome `index`.
    [[nodiscard]] QString biome(int index) const;
    // The name of a kind of site (QcBiomeMap::siteKinds).
    [[nodiscard]] QString kind(const QString &kind) const;
    // What covers cell (cellX, cellZ) of `map`: its biome, or the sea, frozen or not; empty outside.
    [[nodiscard]] QString cell(const QcWorldMapResult &map, int cellX, int cellZ) const;
    // The biomes and the sea of `map`, most cells first: maps with name, colour, share and percent.
    [[nodiscard]] QVariantList legend(const QcWorldMapResult &map) const;
    // A name with the block column it stands for ("Plains, x 120, z -48").
    [[nodiscard]] static QString place(const QString &name, int x, int z);

private:
    // The content's text of `key` in the language; empty when no language has it.
    [[nodiscard]] QString text(const QString &key) const;

    const QcContentRegistry *m_content;
    QString m_language;
};
