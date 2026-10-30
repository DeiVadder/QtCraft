// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDataStream>
#include <QList>
#include <QString>

#include <optional>

class QcContentRegistry;

/*! \brief One status effect a body has (decision E-450): its kind (QcEffectDefinition::name),
    its level (1 = level I) and the ticks it still lasts. */
struct QcStatusEffect
{
    /*! The kind (QcEffectDefinition::name). */
    QString name;
    /*! The level, 1 = level I. */
    int level = 1;
    /*! The ticks it still lasts. */
    int ticksLeft = 0;

    bool operator==(const QcStatusEffect &) const = default;
};

/*! \brief The status effects of a player or a creature (decision E-450): at most one of each kind.

    They count down with the server's ticks; the server applies what they do (QcServerEffects)
    and tells the player of his own with his health. */
class QcStatusEffects
{
public:
    /*! Bounds of what is given, saved and sent (about 14 hours; more kinds than any pack has).
        \qcshared */
    static constexpr int maxTicks = 1'000'000;
    static constexpr int maxEffects = 32;

    /*! Gives \a effect like the reference: a higher level replaces the one of its kind, the same
        level lasts the longer of both, a lower one does nothing. Returns whether anything
        changed. */
    bool give(const QcStatusEffect &effect);
    /*! Takes the effect of kind \a name away. Returns false when there was none. */
    bool remove(const QString &name);
    /*! Takes all away. Returns false when there were none. */
    bool clear();
    /*! Counts every effect down by one tick and drops the ended ones. Returns true when one
        ended. */
    bool tick();

    /*! Returns the effect of kind \a name; null when the body has none. */
    const QcStatusEffect *find(const QString &name) const;
    /*! Returns every effect. */
    const QList<QcStatusEffect> &all() const { return m_effects; }
    /*! Returns whether the body has no effect. */
    bool isEmpty() const { return m_effects.isEmpty(); }
    /*! Returns the factor on the speed of the body that has them (slowness, decision E-838): the
        product of their kinds' QcEffectDefinition::speedFactorAt in \a content; 1 without any,
        kinds no pack has do nothing. */
    double speedFactor(const QcContentRegistry &content) const { return speedFactor(content, m_effects); }
    /*! Returns the speed factor of a body with \a effects, as the member speedFactor() does with
        the kinds of \a content. */
    static double speedFactor(const QcContentRegistry &content, const QList<QcStatusEffect> &effects);
    /*! Replaces the list with \a effects as read back (saved effects): of duplicate kinds the
        first stays, and at most maxEffects are kept (read() checked the bounds). */
    void restore(const QList<QcStatusEffect> &effects);
    /*! Returns whether a read \a effect is within the bounds: a name, a level from 1 to
        \a maxLevel and 1 to maxTicks ticks left. */
    static bool plausible(const QcStatusEffect &effect, int maxLevel);
    /*! Returns \a level as the HUD shows it: Roman numerals from I to X
        (QcEffectDefinition::maxLevel), the number beyond. */
    static QString levelName(int level);
    /*! Writes \a effects to \a stream in the saved form (creature components, player files): a
        count, then name, level and ticks of each. */
    static void write(QDataStream &stream, const QList<QcStatusEffect> &effects);
    /*! Returns the effects read from \a stream in the form of write(); std::nullopt for damaged
        data (too many, out of bounds). */
    [[nodiscard]] static std::optional<QList<QcStatusEffect>> read(QDataStream &stream);

    bool operator==(const QcStatusEffects &) const = default;

private:
    QList<QcStatusEffect> m_effects;
};
