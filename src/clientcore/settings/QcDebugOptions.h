#pragma once

#include <QString>

#include <array>
#include <optional>

class QSettings;

// What the debug display shows (decision E-225): its level, switched by the debug key, and the
// visualisations, each switched on its own in the debug options (like Factorio's F4). Stored in
// settings.ini as debug/level and debug/<option>.
struct QcDebugOptions
{
    enum class Level { Off, Compact, Full };
    // The visualisations drawn into the world.
    enum class Visual { ChunkBorders, Hitboxes, LightValues, MobGoals, RemeshFlash };
    static constexpr int visualCount = int(Visual::RemeshFlash) + 1;

    Level level = Level::Compact;
    std::array<bool, visualCount> visuals{};

    // "off", "compact", "full".
    static QString levelName(Level level);
    [[nodiscard]] static std::optional<Level> levelFromName(const QString &name);
    // Off, compact, full, off ... (the debug key).
    static Level nextLevel(Level level);
    // "chunkBorders", "hitboxes", "lightValues", "mobGoals", "remeshFlash".
    static QString visualId(Visual visual);
    [[nodiscard]] static std::optional<Visual> visualFromId(const QString &id);

    bool shows(Visual visual) const { return visuals[std::size_t(visual)]; }
    void set(Visual visual, bool on) { visuals[std::size_t(visual)] = on; }
    bool anyVisual() const;

    // Missing or unknown values give the defaults; a file of an older version that has only
    // showDebug (true or false) gets the compact level or none.
    static QcDebugOptions load(const QSettings &file);
    void save(QSettings &file) const;

    friend bool operator==(const QcDebugOptions &, const QcDebugOptions &) = default;
};
