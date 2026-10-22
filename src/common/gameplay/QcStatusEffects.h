#pragma once

#include <QDataStream>
#include <QList>
#include <QString>

#include <optional>

class QcContentRegistry;

// One status effect a body has (decision E-450): its kind (QcEffectDefinition::name), its level
// (1 = level I) and the ticks it still lasts.
struct QcStatusEffect
{
    QString name;
    int level = 1;
    int ticksLeft = 0;

    bool operator==(const QcStatusEffect &) const = default;
};

// The status effects of a player or a creature (decision E-450): at most one of each kind. They
// count down with the server's ticks; the server applies what they do (QcServerEffects) and tells
// the player of his own with his health.
class QcStatusEffects
{
public:
    // Bounds of what is given, saved and sent (about 14 hours; more kinds than any pack has).
    static constexpr int maxTicks = 1'000'000;
    static constexpr int maxEffects = 32;

    // Gives an effect like the reference: a higher level replaces the one of its kind, the same
    // level lasts the longer of both, a lower one does nothing. Returns whether anything changed.
    bool give(const QcStatusEffect &effect);
    // Takes the effect of kind `name` away; false when there was none.
    bool remove(const QString &name);
    // Takes all away; false when there were none.
    bool clear();
    // Counts every effect down by one tick and drops the ended ones; true when one ended.
    bool tick();

    const QcStatusEffect *find(const QString &name) const;
    const QList<QcStatusEffect> &all() const { return m_effects; }
    bool isEmpty() const { return m_effects.isEmpty(); }
    // The factor on the speed of the body that has them (slowness, decision E-838): the product of
    // their kinds' QcEffectDefinition::speedFactorAt; 1 without any, kinds no pack has do nothing.
    double speedFactor(const QcContentRegistry &content) const { return speedFactor(content, m_effects); }
    static double speedFactor(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
    // Replaces the list as read back (saved effects): out-of-range entries and duplicates are left out.
    void restore(const QList<QcStatusEffect> &effects);
    // Whether a read effect is within the bounds.
    static bool plausible(const QcStatusEffect &effect, int maxLevel);
    // A level as the HUD shows it: Roman numerals from I to X (QcEffectDefinition::maxLevel), the
    // number beyond.
    static QString levelName(int level);
    // The saved form (creature components, player files): a count, then name, level and ticks of
    // each. Reading gives std::nullopt for damaged data (too many, out of bounds).
    static void write(QDataStream &stream, const QList<QcStatusEffect> &effects);
    [[nodiscard]] static std::optional<QList<QcStatusEffect>> read(QDataStream &stream);

    bool operator==(const QcStatusEffects &) const = default;

private:
    QList<QcStatusEffect> m_effects;
};
