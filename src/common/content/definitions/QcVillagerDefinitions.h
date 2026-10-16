#pragma once

#include "QcAppearanceDefinition.h"
#include "QcTradeDefinitions.h"

#include <QList>
#include <QString>
#include <QStringList>

// What the villagers of one village style wear (decision E-411): an outfit, hair styles or headwear
// to choose from (an empty name: none) and the colours of the tinted layers (0xRRGGBB). Each
// villager draws one of every list from its own seed; empty lists take the default look's.
struct QcVillagerLook
{
    static constexpr int maxChoices = 16;

    QString outfit;
    QStringList hair;
    QList<quint32> skin;
    QList<quint32> hairColor;
    QList<quint32> shirt;
    QList<quint32> pants;

    bool isSet() const { return !outfit.isEmpty(); }
};

// A profession (decision E-411): the kind of workstation it works at (a value of
// QcVillageSettings::workstations; empty for the unemployed), layers laid over the style's outfit
// (an apron on the torso; QcVillagerOutfits joins them), headwear in place of the style's hair (a
// hair-kind appearance; empty keeps the style's) and whether it tends the crops of the village
// fields (farmers).
struct QcVillagerProfession
{
    static constexpr int maxProfessions = 32;

    QString name;
    QString translationKey;
    QString workstation;
    QList<QcAppearanceLayer> layers;
    QString hat;
    bool tendsCrops = false;
};

// What a villager does at a time of day (QcVillagerSettings::routine).
enum class QcVillagerActivity : quint8 {
    // Strolls about the village near its home.
    Wander,
    // At its workstation (the unemployed stroll).
    Work,
    // At the meeting point (the bell).
    Meet,
    // Goes home and stays by its bed.
    Home,
    // Lies in its bed.
    Sleep,
};

// From `from` (a share of the day: 0 sunrise, 0.25 noon, 0.5 sunset, 0.75 midnight) until the next
// step the villagers do `activity`.
struct QcVillagerRoutineStep
{
    double from = 0.0;
    QcVillagerActivity activity = QcVillagerActivity::Wander;
};

// The villagers of all villages (villagers.json, decision E-411; server side only): the creature
// kind they are, their professions, the daily routine, the default look of styles without their
// own, how they keep away from danger and how they trade.
struct QcVillagerSettings
{
    static constexpr int maxRoutineSteps = 24;

    // The creature kind that lives in villages (a mob with "villager": true).
    QString mob;
    QList<QcVillagerProfession> professions;
    // The profession of villagers without a workstation.
    QString unemployed;
    // Sorted by `from`; the last step lasts until the first of the next day.
    QList<QcVillagerRoutineStep> routine;
    QcVillagerLook look;
    // Villagers flee home from hostile creatures within this many blocks.
    double fleeDistance = 10.0;
    // Villagers go home when it rains in the village (at its meeting point's column).
    bool shelterFromRain = true;
    QcTradingSettings trading;

    const QcVillagerProfession *profession(const QString &name) const
    {
        for (const QcVillagerProfession &entry : professions) {
            if (entry.name == name) {
                return &entry;
            }
        }
        return nullptr;
    }
    // The profession that works at workstations of `kind`; none for an unknown kind.
    const QcVillagerProfession *forWorkstation(const QString &kind) const
    {
        for (const QcVillagerProfession &entry : professions) {
            if (!kind.isEmpty() && entry.workstation == kind) {
                return &entry;
            }
        }
        return nullptr;
    }
    // The activity at `phase` (QcDayCycle::phase); Wander without a routine.
    QcVillagerActivity activityAt(double phase) const
    {
        if (routine.isEmpty()) {
            return QcVillagerActivity::Wander;
        }
        QcVillagerActivity activity = routine.constLast().activity;
        for (const QcVillagerRoutineStep &step : routine) {
            if (step.from <= phase) {
                activity = step.activity;
            }
        }
        return activity;
    }
};
