// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QcContentRegistry;
struct QcMobDefinition;

/*! \brief The monster list of every biome (decision E-838), as in the original game: each monster
    kind has a weight in a biome, and an attempt picks one kind of the whole list by weight and then
    tests it at the place; a kind that may not spawn there wastes the attempt.

    The weight of a kind in a biome is the biome's own (\c {monsters} of the biome) or else the
    highest weight among the kind's spawn rules that allow the biome; a kind whose rules all have
    weight 0 never spawns, whatever a biome says. Worlds without biomes (the test terrain) use
    the empty name. Shared by the server's spawning and the handbook's biome pages. */
class QcSpawnWeights
{
public:
    explicit QcSpawnWeights(const QcContentRegistry &content);

    /*! Returns the weight of monster kind \a type in \a biome; 0 for other kinds. */
    double weight(int type, const QString &biome) const;
    /*! Returns the sum of the weights of all monster kinds in \a biome. */
    double total(const QString &biome) const;
    /*! Returns the highest weight among the spawn rules of \a mob that allow \a biome, without
        the biome's own. */
    static double ruleWeight(const QcMobDefinition &mob, const QString &biome);
    /*! Returns the warnings for \c {monsters} entries of the biomes in \a content that name no
        creature or one that is no monster (they are left out by the lists). */
    static QStringList problems(const QcContentRegistry &content);

private:
    struct Table
    {
        QList<double> weights;
        double total = 0.0;
    };

    static Table tableOf(const QcContentRegistry &content, const QString &biome);
    const Table &find(const QString &biome) const;

    // Every biome of the content and the empty name.
    QHash<QString, Table> m_tables;
};
