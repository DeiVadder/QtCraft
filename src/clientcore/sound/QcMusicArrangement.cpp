// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcMusicArrangement.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

QcMusicStyle styleOf(QcMusicStyle::Scale scale, std::pair<int, int> roots, std::pair<int, int> tempos, QcSoundWave lead)
{
    QcMusicStyle style;
    style.scale = scale;
    style.lowestRoot = roots.first;
    style.highestRoot = roots.second;
    style.slowestTempo = tempos.first;
    style.fastestTempo = tempos.second;
    style.lead = lead;
    return style;
}

// The overworld's and the menu's places. The measured traits per place, which these values
// follow, are in docs/Musik-Merkmale.md (decision E-1156).
QList<QcMusicArrangement> overworldPlaces()
{
    using Scale = QcMusicStyle::Scale;
    QcMusicStyle cave = styleOf(Scale::Any, {41, 46}, {54, 64}, QcSoundWave::Sine);
    cave.leadLowpass = 1800.0;
    QcMusicStyle night = styleOf(Scale::Minor, {41, 46}, {58, 70}, QcSoundWave::Sine);
    night.leadLowpass = 2200.0;
    QcMusicStyle menu = styleOf(Scale::Major, {43, 49}, {60, 72}, QcSoundWave::Triangle);
    menu.leadLowpass = 2400.0;
    QcMusicStyle day = styleOf(Scale::Major, {43, 49}, {66, 80}, QcSoundWave::Triangle);
    day.leadLowpass = 2800.0;

    return {
        {.place = u"menu"_s, .style = menu, .shortestSeconds = 280.0, .longestSeconds = 300.0, .barsPerChord = 2,
         .leadNotesPerBar = 1.25, .leadShare = 0.6, .leadOctaves = 1, .keysNotesPerBar = 3.0, .padLowpass = 1600.0,
         .reverbSeconds = 3.3, .reverbMix = 0.25, .peakAt = 0.7, .fadeInSeconds = 15.0, .fadeOutSeconds = 20.0},
        {.place = u"day"_s, .style = day, .shortestSeconds = 200.0, .longestSeconds = 300.0, .barsPerChord = 2,
         .leadNotesPerBar = 1.5, .leadShare = 0.7, .leadOctaves = 1, .keysNotesPerBar = 2.5, .padLowpass = 1800.0,
         .reverbSeconds = 2.3, .reverbMix = 0.2, .peakAt = 0.55, .fadeInSeconds = 3.0, .fadeOutSeconds = 12.0},
        {.place = u"night"_s, .style = night, .shortestSeconds = 254.0, .longestSeconds = 300.0, .barsPerChord = 2,
         .leadNotesPerBar = 1.25, .leadShare = 0.6, .leadOctaves = 2, .keysNotesPerBar = 3.0, .padLowpass = 1500.0,
         .reverbSeconds = 3.3, .reverbMix = 0.25, .peakAt = 0.4, .fadeInSeconds = 7.0, .fadeOutSeconds = 12.0},
        {.place = u"cave"_s, .style = cave, .shortestSeconds = 258.0, .longestSeconds = 300.0, .barsPerChord = 2,
         .leadNotesPerBar = 1.0, .leadShare = 0.5, .leadOctaves = 1, .keysNotesPerBar = 2.5, .padLowpass = 1400.0,
         .reverbSeconds = 3.3, .reverbMix = 0.28, .peakAt = 0.8, .fadeInSeconds = 20.0, .fadeOutSeconds = 18.0},
    };
}

// The nether's biomes take scale, root, tempo and lead from their atmosphere (content), only the
// layout from here.
QList<QcMusicArrangement> netherPlaces()
{
    return {
        {.place = u"biome:qtcraft:nether_wastes"_s, .shortestSeconds = 280.0, .longestSeconds = 300.0, .barsPerChord = 1,
         .leadNotesPerBar = 1.0, .leadShare = 0.5, .leadOctaves = 2, .keysNotesPerBar = 2.0, .padLowpass = 1600.0,
         .reverbSeconds = 4.3, .reverbMix = 0.3, .peakAt = 0.15, .fadeInSeconds = 5.0, .fadeOutSeconds = 7.0},
        {.place = u"biome:qtcraft:soul_sand_valley"_s, .shortestSeconds = 280.0, .longestSeconds = 300.0, .barsPerChord = 2,
         .leadNotesPerBar = 0.5, .leadShare = 0.15, .leadOctaves = 2, .padLowpass = 1400.0, .reverbSeconds = 4.4,
         .reverbMix = 0.32, .peakAt = 0.35, .fadeInSeconds = 25.0, .fadeOutSeconds = 19.0},
        {.place = u"biome:qtcraft:crimson_forest"_s, .shortestSeconds = 280.0, .longestSeconds = 300.0, .barsPerChord = 2,
         .leadNotesPerBar = 1.25, .leadShare = 0.6, .leadOctaves = 2, .keysNotesPerBar = 1.5, .padLowpass = 1800.0,
         .reverbSeconds = 4.6, .reverbMix = 0.32, .peakAt = 0.65, .fadeInSeconds = 16.0, .fadeOutSeconds = 11.0},
        {.place = u"biome:qtcraft:warped_forest"_s, .shortestSeconds = 280.0, .longestSeconds = 300.0, .barsPerChord = 2,
         .leadNotesPerBar = 0.75, .leadShare = 0.5, .leadOctaves = 2, .keysNotesPerBar = 2.0, .padLowpass = 1400.0,
         .reverbSeconds = 4.0, .reverbMix = 0.3, .peakAt = 0.95, .fadeInSeconds = 14.0, .fadeOutSeconds = 4.0},
        {.place = u"biome:qtcraft:basalt_deltas"_s, .shortestSeconds = 280.0, .longestSeconds = 291.0, .barsPerChord = 4,
         .leadNotesPerBar = 1.0, .leadShare = 0.5, .leadOctaves = 2, .keysNotesPerBar = 3.0, .padLowpass = 1200.0,
         .reverbSeconds = 3.5, .reverbMix = 0.28, .peakAt = 0.75, .fadeInSeconds = 1.0, .fadeOutSeconds = 7.0},
        {.place = u"dimension:qtcraft:nether"_s, .shortestSeconds = 280.0, .longestSeconds = 300.0, .barsPerChord = 2,
         .leadNotesPerBar = 1.0, .leadShare = 0.5, .leadOctaves = 2, .keysNotesPerBar = 2.0, .padLowpass = 1500.0,
         .reverbSeconds = 4.2, .reverbMix = 0.3, .peakAt = 0.55, .fadeInSeconds = 12.0, .fadeOutSeconds = 10.0},
    };
}

} // namespace

QcMusicStyle QcMusicArrangement::styleFor(const std::optional<QcMusicStyle> &biome) const
{
    if (biome) {
        return *biome;
    }
    return style.value_or(QcMusicStyle{});
}

const QList<QcMusicArrangement> &QcMusicArrangement::all()
{
    static const QList<QcMusicArrangement> arrangements = overworldPlaces() + netherPlaces();
    return arrangements;
}

const QcMusicArrangement *QcMusicArrangement::find(const QString &place)
{
    const auto found = std::ranges::find(all(), place, &QcMusicArrangement::place);
    return found == all().end() ? nullptr : &*found;
}

const QcMusicArrangement &QcMusicArrangement::forPlaces(const QStringList &places)
{
    for (const QString &place : places) {
        if (const QcMusicArrangement *found = find(place)) {
            return *found;
        }
    }
    return *find(u"day"_s);
}
