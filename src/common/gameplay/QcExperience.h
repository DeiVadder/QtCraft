#pragma once

#include <QList>
#include <QtGlobal>

// A player's experience (decision E-398): a level and the points gathered towards the next one, with
// the reference game's cost curve (7 + 2 L points per level below 15, 37 + 5 (L - 15) up to 29, then
// 112 + 9 (L - 30)). The server owns it; clients only show it. Experience comes from orbs
// (QcExperienceOrbManager) and /xp; the spending side (trading, enchanting, the anvil) takes levels
// with spendLevels().
class QcExperience
{
public:
    // Highest level; its total points still fit 32 bits easily (about 4.5e8).
    static constexpr int maxLevel = 10000;
    // On death a player drops 7 points per level, at most this many (the reference's rule).
    static constexpr int deathPointsPerLevel = 7;
    static constexpr int maxDeathPoints = 100;

    QcExperience() = default;
    // A level and points within it, clamped into range.
    QcExperience(int level, int points);

    // Points needed from `level` to the next level.
    static int pointsForNext(int level);
    // The most points a player can hold within `level` (none at maxLevel).
    static int mostPoints(int level);
    // Points from level 0 up to the start of `level`.
    static qint64 totalForLevel(int level);
    // The level and points of a total.
    static QcExperience fromTotal(qint64 total);
    // The values of orbs that `points` split into, largest first, as the reference game splits them
    // (2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1); at most `maxOrbs` orbs, the last one takes
    // the rest.
    static QList<int> orbValues(int points, int maxOrbs);

    int level() const { return m_level; }
    int points() const { return m_points; }
    qint64 total() const { return totalForLevel(m_level) + m_points; }
    // Share of the way to the next level, 0..1 (the bar in the HUD).
    double progress() const { return double(m_points) / double(pointsForNext(m_level)); }

    // Adds points (negative ones take away, down to nothing); returns the levels gained (negative
    // when lost).
    int addPoints(qint64 points);
    // Adds levels (negative ones take away, down to level 0); the share of the way to the next level
    // stays.
    void addLevels(int levels);
    // Takes `levels` when the player has them (a trade, an enchantment); false and no change
    // otherwise.
    [[nodiscard]] bool spendLevels(int levels);
    // Sets the level; the share of the way to the next level stays (as with addLevels).
    void setLevel(int level);
    // Points within the current level; false and no change when they reach the next level.
    [[nodiscard]] bool setPoints(int points);
    // Points dropped as orbs on death: 7 per level, at most 100.
    int deathDrop() const;

    bool operator==(const QcExperience &) const = default;

private:
    int m_level = 0;
    int m_points = 0;
};
