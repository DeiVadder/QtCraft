// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcWorldInfo.h"
#include "server/dimensions/QcServerDimension.h"

#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>
#include <utility>
#include <vector>

/*! \brief The dimensions the server has open (decision E-978): the overworld from the start, the
    others once opened, each a QcServerDimension with its own block world.

    Lives in the server's thread as part of QcServerState. A dimension stays open once opened.
    Which generator version a dimension generates as comes from world.json (decision E-979,
    versionIn()). */
class QcDimensions
{
public:
    /*! The most dimensions open at once. */
    static constexpr int maxOpen = 64;

    /*! Opens the overworld of \a content (its definition or the default one) with
        \a overworldGenerator, the generator of the world; \a seed is the world's. */
    QcDimensions(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> overworldGenerator, quint64 seed);
    // The dimensions are referred to by the parts of the server.
    Q_DISABLE_COPY_MOVE(QcDimensions)
    ~QcDimensions() = default;

    /*! Returns the overworld. */
    QcServerDimension &overworld() { return *m_open.front(); }
    /*! Returns the overworld, read-only. */
    const QcServerDimension &overworld() const { return *m_open.front(); }
    /*! Returns the open dimension \a name; null when it is not open. */
    QcServerDimension *find(const QString &name) { return openNamed(name); }
    /*! Returns the open dimension \a name, read-only; null when it is not open. */
    const QcServerDimension *find(const QString &name) const { return openNamed(name); }
    /*! Returns the open dimensions, the overworld first, the others in the order they opened. */
    const std::vector<std::unique_ptr<QcServerDimension>> &all() const { return m_open; }
    /*! Returns the generator type \a dimension generates with in the world \a info: its own, or
        for the world's own type (QcDimensionDefinition::worldGenerator) the world's. */
    static QString generatorTypeIn(const QcWorldInfo &info, const QcDimensionDefinition &dimension);
    /*! Returns the generator version \a dimension generates as in the world \a info (decision
        E-979): the overworld's is generatorVersion; another one's as world.json names it, else
        the series' newest frozen version (QcGeneratorVersion::Series::forLateDimension). In a
        showcase world every dimension generates as the current version (decision E-922). */
    static int versionIn(const QcWorldInfo &info, const QcDimensionDefinition &dimension);
    /*! Returns whether the world \a info keeps the generator versions of its dimensions: every
        world but a showcase world, which always generates as the current version. */
    static bool isVersioned(const QcWorldInfo &info);
    /*! Returns the generator versions of the new world \a info: what it has, and for the other
        dimensions of \a content the current version of their series, up to
        QcWorldInfo::maxDimensionVersions entries (a dimension left out is treated as a late one
        and may be refused when it opens). A showcase world gets none. */
    static QMap<QString, int> versionsForNewWorld(const QcContentRegistry &content, const QcWorldInfo &info);
    /*! Returns what is wrong with the dimensions of \a content, for the log when a server starts
        (decision E-988): a generator type or options that cannot generate, a generator other
        than QcDimensionDefinition::worldGenerator for the overworld (the world's own plays) or
        that one for another dimension, unknown blocks of the arrival platform and the liquids. A
        dimension with a wrong generator cannot open; the server still starts. */
    [[nodiscard]] static QStringList definitionWarnings(const QcContentRegistry &content);
    /*! Returns a warning for each dimension of \a content the world \a info cannot record any
        more (QcWorldInfo::maxDimensionVersions), which will not open (decision E-988). */
    [[nodiscard]] static QStringList versionWarnings(const QcContentRegistry &content, const QcWorldInfo &info);

private:
    // QcServerState::openDimension is the way in: it records the version and sets the limits.
    friend class QcServerState;

    // Opens the dimension `name` of the content, which is not open yet, generated from `seed` as
    // versionIn() of the world `info`. Returns an error for a dimension the content does not have,
    // a generator type or options it cannot make, a version this game does not generate, too many
    // open dimensions, or a world that cannot record another version.
    [[nodiscard]] QcResult<QcServerDimension *> open(const QString &name, qint64 seed, const QcWorldInfo &info);
    // The open dimension `name`; null when it is not open.
    QcServerDimension *openNamed(const QString &name) const;
    const std::shared_ptr<const QcContentRegistry> &content() const { return m_content; }
    void addOpen(std::unique_ptr<QcServerDimension> dimension);
    // Closes the dimension opened last, whose storage could not open: it never played.
    void dropLast() { m_open.pop_back(); }

    std::shared_ptr<const QcContentRegistry> m_content;
    // Entity ids are unique across the dimensions: an entity keeps its id on the way to another one,
    // and what a part keeps by id never mixes the dimensions up. Before m_open, which refers to it.
    quint32 m_nextEntityId = 1;
    quint64 m_nextCreatureUid = 1;
    // The overworld first.
    std::vector<std::unique_ptr<QcServerDimension>> m_open;
};
