#include "QcLocalSounds.h"

#include "QcContentRegistry.h"
#include "QcSoundKeys.h"

#include <algorithm>
#include <cmath>

std::optional<QcBlockId> QcLocalSounds::groundBelow(const QcVec3 &feet, const QcBlockAccess &world)
{
    // A little below the feet: standing entities rest exactly on the block's top face.
    const QcBlockPos pos{int(std::floor(feet.x)), int(std::floor(feet.y - 0.05)), int(std::floor(feet.z))};
    return world.blockAt(pos);
}

QString QcLocalSounds::stepSound(const QcVec3 &feet, const QcBlockAccess &world)
{
    const std::optional<QcBlockId> ground = groundBelow(feet, world);
    if (!ground || !world.content().isValidBlock(*ground)) {
        return {};
    }
    return QcSoundKeys::blockSound(world.content().block(*ground), QcBlockSoundAction::Step);
}

void QcLocalSounds::reset()
{
    m_sinceStep = 0.0;
    m_sinceStroke = 0.0;
}

QList<QcSoundCue> QcLocalSounds::update(const QcEntityMotion &before, const QcEntityMotion &after, bool sneaking,
                                        const QcBlockAccess &world)
{
    QList<QcSoundCue> cues;
    const auto cue = [&cues](const QString &key, const QcVec3 &position, float volume, float pitch) {
        if (!key.isEmpty()) {
            cues.append({key, position, volume, pitch, QcSoundCue::Origin::Local, 0});
        }
    };

    const QcVec3 &feet = after.position;
    if (after.inWater) {
        const double entrySpeed = -before.velocity.y;
        if (!before.inWater && entrySpeed > splashSpeed) {
            cue(QString::fromLatin1(QcSoundKeys::waterSplash), feet, float(std::clamp(entrySpeed / 12.0, 0.3, 1.0)), 1.0f);
        }
        m_sinceStroke += (after.position - before.position).length();
        if (m_sinceStroke >= strokeMetres) {
            m_sinceStroke = std::fmod(m_sinceStroke, strokeMetres);
            cue(QString::fromLatin1(QcSoundKeys::waterSwim), feet, 0.4f, 1.0f);
        }
        m_sinceStep = 0.0;
        return cues;
    }

    m_sinceStroke = 0.0;
    const double moved = std::hypot(after.position.x - before.position.x, after.position.z - before.position.z);
    if (after.onGround && !before.onGround) {
        const double fallSpeed = -before.velocity.y;
        const bool hard = fallSpeed > landingSpeed;
        cue(stepSound(feet, world), feet, hard ? float(std::clamp(fallSpeed / 15.0, 0.5, 1.0)) : stepVolume, hard ? 0.85f : 1.0f);
        m_sinceStep = 0.0;
    } else if (before.onGround && !after.onGround && after.velocity.y > 0.0) {
        cue(stepSound(before.position, world), before.position, jumpVolume, 1.1f);
        m_sinceStep = 0.0;
    } else if (after.onGround) {
        m_sinceStep += moved;
        if (m_sinceStep >= strideMetres) {
            m_sinceStep = std::fmod(m_sinceStep, strideMetres);
            cue(stepSound(feet, world), feet, sneaking ? sneakStepVolume : stepVolume, 1.0f);
        }
    }
    return cues;
}
