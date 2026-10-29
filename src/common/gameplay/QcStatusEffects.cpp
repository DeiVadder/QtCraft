// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcStatusEffects.h"

#include "common/content/definitions/QcEffectDefinition.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <array>
#include <utility>

bool QcStatusEffects::give(const QcStatusEffect &effect)
{
    if (!plausible(effect, QcEffectDefinition::maxLevel)) {
        return false;
    }

    const auto it = std::ranges::find(m_effects, effect.name, &QcStatusEffect::name);
    if (it == m_effects.end()) {
        if (m_effects.size() >= maxEffects) {
            return false;
        }
        m_effects.append(effect);
        return true;
    }

    if (effect.level > it->level || (effect.level == it->level && effect.ticksLeft > it->ticksLeft)) {
        *it = effect;
        return true;
    }
    return false;
}

bool QcStatusEffects::remove(const QString &name)
{
    return m_effects.removeIf([&name](const QcStatusEffect &effect) { return effect.name == name; }) > 0;
}

bool QcStatusEffects::clear()
{
    const bool had = !m_effects.isEmpty();
    m_effects.clear();
    return had;
}

bool QcStatusEffects::tick()
{
    for (QcStatusEffect &effect : m_effects) {
        --effect.ticksLeft;
    }
    return m_effects.removeIf([](const QcStatusEffect &effect) { return effect.ticksLeft <= 0; }) > 0;
}

const QcStatusEffect *QcStatusEffects::find(const QString &name) const
{
    const auto it = std::ranges::find(m_effects, name, &QcStatusEffect::name);
    return it == m_effects.end() ? nullptr : &*it;
}

void QcStatusEffects::restore(const QList<QcStatusEffect> &effects)
{
    m_effects.clear();
    for (const QcStatusEffect &effect : effects) {
        if (!find(effect.name)) {
            give(effect);
        }
    }
}

bool QcStatusEffects::plausible(const QcStatusEffect &effect, int maxLevel)
{
    return !effect.name.isEmpty() && effect.level >= 1 && effect.level <= maxLevel && effect.ticksLeft > 0 && effect.ticksLeft <= maxTicks;
}

QString QcStatusEffects::levelName(int level)
{
    static constexpr std::array<const char *, 10> roman{"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    if (level >= 1 && std::cmp_less_equal(level, roman.size())) {
        return QString::fromLatin1(roman[static_cast<std::size_t>(level - 1)]);
    }
    return QString::number(level);
}

void QcStatusEffects::write(QDataStream &stream, const QList<QcStatusEffect> &effects)
{
    stream << static_cast<quint32>(effects.size());
    for (const QcStatusEffect &effect : effects) {
        stream << effect.name << static_cast<qint32>(effect.level) << static_cast<qint32>(effect.ticksLeft);
    }
}

std::optional<QList<QcStatusEffect>> QcStatusEffects::read(QDataStream &stream)
{
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > static_cast<quint32>(maxEffects)) {
        return std::nullopt;
    }
    QList<QcStatusEffect> effects;
    effects.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        QcStatusEffect effect;
        qint32 level = 0;
        qint32 ticks = 0;
        stream >> effect.name >> level >> ticks;
        effect.level = level;
        effect.ticksLeft = ticks;
        if (stream.status() != QDataStream::Ok || !plausible(effect, QcEffectDefinition::maxLevel)) {
            return std::nullopt;
        }
        effects.append(effect);
    }
    return effects;
}

double QcStatusEffects::speedFactor(const QcContentRegistry &content, const QList<QcStatusEffect> &effects)
{
    double factor = 1.0;
    for (const QcStatusEffect &effect : effects) {
        if (const QcEffectDefinition *kind = content.effect(effect.name)) {
            factor *= kind->speedFactorAt(effect.level);
        }
    }
    return factor;
}
