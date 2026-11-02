// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <array>
#include <optional>

class QSettings;

/*! \brief What the debug display shows (decision E-225): its level, switched by the debug key, and
    the visualisations, each switched on its own in the debug options (like Factorio's F4).

    Stored in settings.ini as debug/level and debug/<option>. */
struct QcDebugOptions
{
    /*! How much the debug display shows. */
    enum class Level {
        Off,     // nothing
        Compact, // frame rate and frame times
        Full     // everything: position, target, chunks, timings, graphs
    };
    /*! \brief The visualisations drawn into the world. */
    enum class Visual {
        ChunkBorders, // chunk and section borders
        Hitboxes,     // entity boxes with their facing
        LightValues,  // sky and block light around the target
        MobGoals,     // the creatures' goals (hunted player, direction)
        RemeshFlash   // sections flash when they are meshed again
    };
    /*! Number of visualisations. */
    static constexpr int visualCount = static_cast<int>(Visual::RemeshFlash) + 1;

    /*! The level of the debug display. */
    Level level = Level::Compact;
    /*! Which visualisations are on, indexed by Visual. */
    std::array<bool, visualCount> visuals{};

    /*! Returns the name of \a level in settings.ini: "off", "compact" or "full". */
    static QString levelName(Level level);
    /*! Returns the level named \a name (levelName()); nothing for an unknown name. */
    [[nodiscard]] static std::optional<Level> levelFromName(const QString &name);
    /*! Returns the level after \a level: off, compact, full, off ... (the debug key). */
    static Level nextLevel(Level level);
    /*! Returns the id of \a visual in settings.ini: "chunkBorders", "hitboxes", "lightValues",
        "mobGoals" or "remeshFlash". */
    static QString visualId(Visual visual);
    /*! Returns the visualisation with the id \a id (visualId()); nothing for an unknown id. */
    [[nodiscard]] static std::optional<Visual> visualFromId(const QString &id);

    /*! Returns whether \a visual is on. */
    bool shows(Visual visual) const { return visuals[static_cast<std::size_t>(visual)]; }
    /*! Switches \a visual on or off (\a on). */
    void set(Visual visual, bool on) { visuals[static_cast<std::size_t>(visual)] = on; }
    /*! Returns whether any visualisation is on. */
    bool anyVisual() const;

    /*! Returns the options stored in \a file. Missing or unknown values give the defaults; a
        file of an older version that has only showDebug (true or false) gets the compact level
        or none. */
    static QcDebugOptions load(const QSettings &file);
    /*! Stores the options in \a file. */
    void save(QSettings &file) const;

    friend bool operator==(const QcDebugOptions &, const QcDebugOptions &) = default;
};
