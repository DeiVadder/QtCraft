// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcSoundCategorizer.h"

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace {

// Sounds of blocks and what works like one (decision E-1150).
bool isBlockSound(const QString &path)
{
    static const std::array<QString, 5> prefixes = {u"block."_s, u"machine."_s, u"liquid."_s, u"note."_s, u"entity.explosive."_s};
    return std::ranges::any_of(prefixes, [&path](const QString &prefix) { return path.startsWith(prefix); });
}

} // namespace

QcSoundCategorizer::QcSoundCategorizer(const QcContentRegistry &content)
{
    for (const QcMobDefinition &mob : content.mobs()) {
        m_creatureHostile.insert(mob.name, mob.hostile);
        for (const QString &sound : {mob.ambientSound, mob.hurtSound, mob.deathSound}) {
            if (!sound.isEmpty()) {
                m_soundHostile.insert(sound, m_soundHostile.value(sound, true) && mob.hostile);
            }
        }
    }
}

QcSoundCategory QcSoundCategorizer::category(const QcSoundDefinition &sound) const
{
    const qsizetype colon = sound.name.indexOf(u':');
    const QString space = colon < 0 ? QString{} : sound.name.left(colon);
    const QString path = sound.name.mid(colon + 1);

    if (path.startsWith(u"ui."_s)) {
        return QcSoundCategory::Interface;
    }
    if (path.startsWith(u"weather."_s) || path == u"ambient.rain"_s) {
        return QcSoundCategory::Weather;
    }
    if (sound.bus == QcSoundBus::Music) {
        return QcSoundCategory::Music;
    }
    if (sound.bus == QcSoundBus::Ambient || path.startsWith(u"ambient."_s)) {
        return QcSoundCategory::Ambient;
    }
    if (path.startsWith(u"mob."_s)) {
        return creatureCategory(space, path, sound.name);
    }
    if (isBlockSound(path)) {
        return QcSoundCategory::Blocks;
    }

    // A creature sound named outside the mob scheme (a mod's "beetle.hiss"); block sounds a
    // creature borrows (the mannequin's wood) stay with the blocks above.
    const auto named = m_soundHostile.constFind(sound.name);
    if (named != m_soundHostile.constEnd()) {
        return *named ? QcSoundCategory::Hostile : QcSoundCategory::Friendly;
    }
    return QcSoundCategory::Players;
}

QcSoundCategory QcSoundCategorizer::creatureCategory(const QString &space, const QString &path, const QString &name) const
{
    // "mob.<creature>.<what>": the creature of that name, if there is one.
    const QString creature = path.section(u'.', 1, 1);
    const QString qualified = space.isEmpty() ? creature : space + u':' + creature;
    const auto known = m_creatureHostile.constFind(qualified);
    if (known != m_creatureHostile.constEnd()) {
        return *known ? QcSoundCategory::Hostile : QcSoundCategory::Friendly;
    }

    // A shared sound (fish, the common hurt and death sounds): hostile only if only hostile
    // creatures name it.
    return m_soundHostile.value(name, false) ? QcSoundCategory::Hostile : QcSoundCategory::Friendly;
}
