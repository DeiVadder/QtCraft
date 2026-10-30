// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcAppearanceDefinition.h"
#include "common/content/villages/QcTradeDefinitions.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief What the villagers of one village style wear (decision E-411): an outfit, hair styles or
    headwear to choose from (an empty name: none) and the colours of the tinted layers (0xRRGGBB).

    Each villager draws one of every list from its own seed; empty lists take the default look's.
    Read from an object with \c {outfit}, \c {hair} (names), \c {skin}, \c {hairColor},
    \c {shirt} and \c {pants} (lists of "#rrggbb"). \qcfields */
struct QcVillagerLook
{
    /*! Most entries of each list. */
    static constexpr int maxChoices = 16;

    QString outfit;
    QStringList hair;
    QList<quint32> skin;
    QList<quint32> hairColor;
    QList<quint32> shirt;
    QList<quint32> pants;

    /*! Returns whether the look is given (it names an outfit). */
    bool isSet() const { return !outfit.isEmpty(); }
};

/*! \brief What a villager does at a time of day (QcVillagerSettings::routine). */
enum class QcVillagerActivity : quint8 {
    /*! Strolls about the village near its home. */
    Wander,
    /*! At its workstation (the unemployed stroll). */
    Work,
    /*! At the meeting point (the bell). */
    Meet,
    /*! Goes home and stays by its bed. */
    Home,
    /*! Lies in its bed. */
    Sleep,
};

/*! \brief From \c {from} (a share of the day: 0 sunrise, 0.25 noon, 0.5 sunset, 0.75 midnight)
    until the next step the villagers do \c {activity}.

    Read from \c {{"from": share, "do": activity}}, \c {from} in [0, 1). \qcfields */
struct QcVillagerRoutineStep
{
    double from = 0.0;
    QcVillagerActivity activity = QcVillagerActivity::Wander;
};

/*! \brief A profession (professions/<name>.json, decision E-411, E-421): the blocks it works at
    (its workstation kind is then its own name, the kind QcVillagePlan and QcVillage keep for a
    workstation; no blocks: the unemployed), layers laid over the style's outfit (an apron on the
    torso; QcVillagerOutfits joins them), headwear in place of the style's hair (a hair-kind
    appearance; empty keeps the style's), whether it tends the crops of the village fields
    (farmers), and optionally its own routine and number of offers per level in place of its
    species'. */
struct QcVillagerProfession
{
    /*! Most workstation blocks of a profession. */
    static constexpr int maxWorkstationBlocks = 16;

    /*! Content name of the profession (\c {"name"}). */
    QString name;
    /*! Translation key of its name (\c {"translation"}, else derived from the name). */
    QString translationKey;
    /*! The workstation kind (the profession's name) when it has blocks; empty otherwise. */
    QString workstation;
    /*! The blocks it works at (\c {"workstations"}), at most maxWorkstationBlocks. */
    QStringList workstationBlocks;
    /*! Layers laid over the style's outfit (\c {"layers"}, read like an outfit's). */
    QList<QcAppearanceLayer> layers;
    /*! Headwear in place of the style's hair (\c {"hat"}); empty keeps the style's hair. */
    QString hat;
    /*! It tends the crops of the village fields (\c {"tendsCrops"}). */
    bool tendsCrops = false;
    /*! Sorted by \c {from}; empty: the routine of the villager's species. */
    QList<QcVillagerRoutineStep> routine;
    /*! Offers of each level a villager draws; 0: QcTradingSettings::offersPerLevel of its
        species. */
    int offersPerLevel = 0;
};

/*! \brief How villagers breed (villagers/<name>.json "breeding", decision E-415).

    Two adults of a village that are willing (each holds \c {food} produce: the crops farmers
    harvest, shared by farmers holding more than \c {shareAbove} with villagers within
    \c {shareDistance} that lack them) and see each other within \c {partnerDistance} by day, in
    dry weather, get a child when their village has a free bed: it claims the bed, has no
    profession and grows up after \c {growUpSeconds}. Parents rest for \c {cooldownSeconds}.
    Distances are in blocks; the JSON keys are the field names. \qcfields */
struct QcVillagerBreedingSettings
{
    int food = 3;
    double growUpSeconds = 1200.0;
    double cooldownSeconds = 300.0;
    double partnerDistance = 5.0;
    int shareAbove = 6;
    double shareDistance = 4.0;
    /*! Children walk this much faster than adults (they run about). */
    double youngSpeed = 1.3;
    /*! Items adult villagers pick up as food (decision E-490), by qualified item name: the
        produce each is worth (a harvest is worth 1). They pick up food while they hold less than
        shareAbove + food, so players feed them by throwing it. */
    QHash<QString, int> foods;
    /*! Most entries of \c {foods}. */
    static constexpr qsizetype maxFoods = 64;
};

/*! \brief The golems villagers summon (worldgen/villages.json "golem" or a style's "golem",
    decisions E-421, E-470), like in the reference game: a villager wants a golem while it slept in
    its bed within \c {sleptSeconds}.

    A panicking one (it panicked at a hostile creature within \c {panicSeconds}) with at least
    \c {minVillagers} such villagers within \c {gatherDistance} (itself included) always summons
    one; otherwise \c {gossipVillagers} such villagers together summon one with \c {gossipChance}
    per check. No golem of the creature kind \c {mob} may stand within \c {golemDistance}; the
    golem appears within \c {spawnRadius} (horizontally; spawnHeight up and down) of the
    summoner. A village summons at most once per \c {cooldownSeconds}. A player who hurts its
    villagers or golems is its enemy for \c {grudgeSeconds} (decision E-471). Without that
    creature kind in the content the rule rests. The JSON keys are the field names. \qcfields */
struct QcVillageGolemSettings
{
    static constexpr int spawnHeight = 6;

    QString mob;
    int minVillagers = 3;
    int gossipVillagers = 5;
    double gatherDistance = 10.0;
    double golemDistance = 16.0;
    double panicSeconds = 10.0;
    double sleptSeconds = 600.0;
    double gossipChance = 0.1;
    double cooldownSeconds = 30.0;
    int spawnRadius = 8;
    double grudgeSeconds = 300.0;
};

/*! \brief A style's own golems (its "golem", decision E-421): the fields it gives, each over the
    settings' golems (worldgen/villages.json), so {"minVillagers": 5} keeps the settings' creature
    kind.

    \c {false} in the file is a patch with an empty \c {mob}: no golems. Each field, when set,
    replaces the field of the same name of QcVillageGolemSettings. \qcfields */
struct QcVillageGolemPatch
{
    std::optional<QString> mob;
    std::optional<int> minVillagers;
    std::optional<int> gossipVillagers;
    std::optional<double> gatherDistance;
    std::optional<double> golemDistance;
    std::optional<double> panicSeconds;
    std::optional<double> sleptSeconds;
    std::optional<double> gossipChance;
    std::optional<double> cooldownSeconds;
    std::optional<int> spawnRadius;
    std::optional<double> grudgeSeconds;

    /*! Returns \a base with the given fields replaced. */
    QcVillageGolemSettings over(QcVillageGolemSettings base) const
    {
        base.mob = mob.value_or(base.mob);
        base.minVillagers = minVillagers.value_or(base.minVillagers);
        base.gossipVillagers = gossipVillagers.value_or(base.gossipVillagers);
        base.gatherDistance = gatherDistance.value_or(base.gatherDistance);
        base.golemDistance = golemDistance.value_or(base.golemDistance);
        base.panicSeconds = panicSeconds.value_or(base.panicSeconds);
        base.sleptSeconds = sleptSeconds.value_or(base.sleptSeconds);
        base.gossipChance = gossipChance.value_or(base.gossipChance);
        base.cooldownSeconds = cooldownSeconds.value_or(base.cooldownSeconds);
        base.spawnRadius = spawnRadius.value_or(base.spawnRadius);
        base.grudgeSeconds = grudgeSeconds.value_or(base.grudgeSeconds);
        return base;
    }
};

/*! \brief The settings of one villager species (villagers/<name>.json, decision E-411, E-421;
    server side only): a creature kind with "villager": "<name>" lives by them.

    They hold the daily routine, the default look of styles without their own, how villagers keep
    away from danger, how they trade and breed. \c {professions} are all professions of the
    content (QcContentRegistry::finish fills them in), shared by every species. */
struct QcVillagerSettings
{
    /*! Most steps of a routine. */
    static constexpr int maxRoutineSteps = 24;

    /*! Content name of the species (\c {"name"}). */
    QString name;
    /*! All professions of the content, filled in by QcContentRegistry::finish. */
    QList<QcVillagerProfession> professions;
    /*! The profession of villagers without a workstation. */
    QString unemployed;
    /*! Sorted by \c {from}; the last step lasts until the first of the next day. */
    QList<QcVillagerRoutineStep> routine;
    /*! The default look of styles without their own (\c {"look"}). */
    QcVillagerLook look;
    /*! Villagers flee home from hostile creatures within this many blocks. */
    double fleeDistance = 10.0;
    /*! Villagers go home when it rains in the village (at its meeting point's column). */
    bool shelterFromRain = true;
    /*! How its villagers trade (\c {"trading"}). */
    QcTradingSettings trading;
    /*! How its villagers breed (\c {"breeding"}). */
    QcVillagerBreedingSettings breeding;

    /*! Returns the profession \a professionName, or \c {nullptr} if there is none. */
    const QcVillagerProfession *profession(const QString &professionName) const
    {
        for (const QcVillagerProfession &entry : professions) {
            if (entry.name == professionName) {
                return &entry;
            }
        }
        return nullptr;
    }
    /*! Returns the profession that works at workstations of \a kind; none for an unknown kind. */
    const QcVillagerProfession *forWorkstation(const QString &kind) const
    {
        for (const QcVillagerProfession &entry : professions) {
            if (!kind.isEmpty() && entry.workstation == kind) {
                return &entry;
            }
        }
        return nullptr;
    }
    /*! Returns the activity at \a phase (QcDayCycle::phase) by the routine of \a profession or,
        when it has none, of the species; Wander without a routine. */
    QcVillagerActivity activityAt(double phase, const QcVillagerProfession *profession = nullptr) const
    {
        const QList<QcVillagerRoutineStep> &steps = profession && !profession->routine.isEmpty() ? profession->routine : routine;
        if (steps.isEmpty()) {
            return QcVillagerActivity::Wander;
        }
        QcVillagerActivity activity = steps.constLast().activity;
        for (const QcVillagerRoutineStep &step : steps) {
            if (step.from <= phase) {
                activity = step.activity;
            }
        }
        return activity;
    }
    /*! Returns the activity of a villager of profession \a professionName at \a phase. */
    QcVillagerActivity activityAt(double phase, const QString &professionName) const { return activityAt(phase, profession(professionName)); }
};
