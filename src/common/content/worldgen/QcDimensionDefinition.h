// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>

/*! \brief A dimension of a world (dimensions/<name>.json of a pack, decision E-978): a world of
    its own beside the others, with its own chunks, generator, sky and rules.

    Every world has the overworld (\c {qtcraft:overworld}); a world without a definition of it
    (a pack without dimensions/, the content of tests) plays the overworld with the defaults of
    this struct, which are the overworld's. Mods add dimensions or replace one by name. Every
    dimension is 256 blocks high like the overworld; a ceiling puts a bedrock roof below the
    top. The server keeps a QcServerWorld per dimension; the fields that only the client, the
    portals or later stages read are parsed and kept here already, so the format stays the same
    from stage 83 on. */
struct QcDimensionDefinition
{
    /*! The name of the overworld, the dimension every world has. */
    static constexpr auto overworldName = "qtcraft:overworld";
    /*! The generator type that stands for the world's own type (\c {"generator": {"type":
        "world"}}): the overworld generates as the world was created (terrain, flat, void,
        showcase). */
    static constexpr auto worldGenerator = "world";
    /*! The longest qualified name of a dimension, as the parser, world.json and player files
        accept it. */
    static constexpr qsizetype maxNameLength = 128;
    /*! The longest translation key of a dimension (\c {"translation"}), as the parser and the
        protocol accept it. */
    static constexpr qsizetype maxTranslationKey = 256;
    /*! The largest coordinate scale against the overworld (\c {"coordinateScale"}). */
    static constexpr double maxCoordinateScale = 128.0;
    /*! The largest damage per second of the void below a dimension without floor. */
    static constexpr double maxVoidDamage = 1000.0;
    /*! The most liquid overrides of a dimension. */
    static constexpr int maxLiquidFlows = 16;
    /*! The largest radius of an arrival platform. */
    static constexpr int maxPlatformRadius = 16;

    /*! How the sky of the dimension looks to the client (\c {"sky"}). */
    enum class Sky : quint8 {
        /*! Sun, moon, stars and clouds (\c {"overworld"}, the default). */
        Overworld,
        /*! No sky: a fog in the dimension's fog colour (\c {"nether"}). */
        Nether,
        /*! A dark, starless vault (\c {"end"}). */
        End,
    };

    /*! What lies below the lowest block (\c {"floor"}). */
    enum class Floor : quint8 {
        /*! The generator closes the bottom with bedrock (\c {"bedrock"}, the default). */
        Bedrock,
        /*! Nothing: bodies that fall below the world take voidDamage (\c {"void"}). */
        Void,
    };

    /*! \brief How a liquid flows in the dimension instead of its own values (\c {"liquids"}:
        {"<liquid block>": {"distance": n, "ticks": n}}), e.g. lava in the nether: further and
        faster. Missing values keep the liquid's own. \qcfields */
    struct LiquidFlow
    {
        QString liquid;
        std::optional<int> distance;
        std::optional<int> ticks;
    };

    /*! \brief A fixed point where travellers arrive (\c {"arrival"}: {"x", "y", "z",
        "platform", "platformRadius"}), with a platform of \c {platform} blocks (none: no
        platform) of \c {platformRadius} blocks around it, made on arrival when missing; for
        dimensions like the end. Without it, portals link by the coordinate scale. \qcfields */
    struct Arrival
    {
        QcBlockPos position;
        QString platform;
        int platformRadius = 2;
    };

    /*! Qualified name (\c {"name"}, required; at most maxNameLength characters, without '/' and
        '.', it names the dimension's folder in the world). */
    QString name;
    /*! Translation key of its display name (\c {"translation"}); defaults to
        \c {dimension.<namespace>.<name>}. */
    QString translationKey;
    /*! The generator type (\c {"generator"}/\c {"type"}): worldGenerator or a world type of
        QcWorldGenerators (\c {"void"}, \c {"flat"} …); only the overworld may use
        worldGenerator. */
    QString generatorType = QString::fromLatin1(worldGenerator);
    /*! The generator's options (\c {"generator"}/\c {"options"}, as world.json stores them for a
        world of that type). */
    QString generatorOptions;
    /*! The height of the bedrock roof (\c {"ceiling"}, 1 to 255); 0 for an open sky. */
    int ceiling = 0;
    /*! How many overworld blocks one block of this dimension stands for (\c {"coordinateScale"};
        nether: 8). */
    double coordinateScale = 1.0;
    /*! How the sky looks. */
    Sky sky = Sky::Overworld;
    /*! The colour of the fog, 0xRRGGBB (\c {"fogColour"}); none: the sky's own. */
    std::optional<quint32> fogColour;
    /*! The light every block has at least, 0 to 15 (\c {"ambientLight"}). */
    int ambientLight = 0;
    /*! Whether there is sky light (\c {"skyLight"}). */
    bool skyLight = true;
    /*! Whether the time of day runs and shows (\c {"dayCycle"}). */
    bool dayCycle = true;
    /*! Whether there is weather (\c {"weather"}). */
    bool weather = true;
    /*! Whether water placed from a bucket evaporates (\c {"waterEvaporates"}). */
    bool waterEvaporates = false;
    /*! How liquids flow here instead of their own values. */
    QList<LiquidFlow> liquidFlows;
    /*! Whether a bed explodes when used (\c {"bedsExplode"}). */
    bool bedsExplode = false;
    /*! Whether compasses and clocks spin instead of pointing and telling the time
        (\c {"compassesSpin"}). */
    bool compassesSpin = false;
    /*! What lies below the lowest block. */
    Floor floor = Floor::Bedrock;
    /*! Damage per second below a void floor (\c {"voidDamage"}). */
    double voidDamage = 4.0;
    /*! The fixed arrival point, if any. */
    std::optional<Arrival> arrival;

    /*! Returns whether this is the overworld. */
    bool isOverworld() const { return name == QLatin1StringView(overworldName); }
    /*! Returns the overworld as a world without its definition plays it: the defaults. */
    static QcDimensionDefinition defaultOverworld()
    {
        QcDimensionDefinition overworld;
        overworld.name = QString::fromLatin1(overworldName);
        overworld.translationKey = QStringLiteral("dimension.qtcraft.overworld");
        return overworld;
    }
};
