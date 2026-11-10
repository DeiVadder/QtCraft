// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <array>
#include <optional>

/*! \brief What a sound belongs to for the player's volume sliders (decision E-1150): each has its
    own volume in the mixer, under the master volume.

    The order is the order of the sliders on the audio page. QcSoundCategorizer finds the category
    of a sound from its name and the content. */
enum class QcSoundCategory : quint8 {
    Music,     // The background music.
    Weather,   // Rain and thunder.
    Blocks,    // Blocks broken, placed, stepped on and working; liquids, note blocks, explosions.
    Hostile,   // Creatures hostile to players.
    Friendly,  // All other creatures.
    Players,   // The players: hurt, eating, items, thrown and shot things, swimming.
    Ambient,   // Ambience loops and the sounds around them (wind, caves, water, biomes).
    Interface, // Clicks in menus and screens.
};

/*! The number of QcSoundCategory values. */
inline constexpr int qcSoundCategoryCount = 8;

/*! \brief The names of QcSoundCategory in the settings file ("audio/<name>"). */
namespace QcSoundCategories {

/*! Returns every category in its order. */
[[nodiscard]] constexpr std::array<QcSoundCategory, qcSoundCategoryCount> all()
{
    return {QcSoundCategory::Music,    QcSoundCategory::Weather, QcSoundCategory::Blocks,  QcSoundCategory::Hostile,
            QcSoundCategory::Friendly, QcSoundCategory::Players, QcSoundCategory::Ambient, QcSoundCategory::Interface};
}

/*! Returns the name of \a category in the settings file. */
[[nodiscard]] QString name(QcSoundCategory category);

/*! Returns the category named \a text; none for an unknown name. */
[[nodiscard]] std::optional<QcSoundCategory> fromName(const QString &text);

/*! Returns the category with the index \a index (its place in all()); none outside
    0..qcSoundCategoryCount - 1. */
[[nodiscard]] std::optional<QcSoundCategory> fromIndex(int index);

} // namespace QcSoundCategories

/*! \brief A volume in 0..1 for each QcSoundCategory. */
class QcSoundVolumes
{
public:
    /*! Every category at full volume. */
    QcSoundVolumes() { m_volumes.fill(1.0); }

    /*! Returns the volume of \a category. */
    [[nodiscard]] double volume(QcSoundCategory category) const { return m_volumes[static_cast<std::size_t>(category)]; }
    /*! Sets the volume of \a category to \a volume (clamped to 0..1; not finite: unchanged). */
    void setVolume(QcSoundCategory category, double volume);
    /*! Returns \a volume clamped to 0..1; \a fallback when it is no finite number. */
    [[nodiscard]] static double clamped(double volume, double fallback);

    /*! Returns whether every volume equals that of the other. */
    bool operator==(const QcSoundVolumes &) const = default;

private:
    std::array<double, qcSoundCategoryCount> m_volumes{};
};
