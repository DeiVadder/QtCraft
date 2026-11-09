// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/items/QcItemDefinition.h"
#include "common/core/QcAabb.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcStatusEffects.h"

#include <QLatin1StringView>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

class QcContentRegistry;

/*! \brief A section of a beacon's beam (decision E-1143): its colour (0xRRGGBB) and how many
    blocks it reaches up, from where the section below it ends (the first from the beacon). */
struct QcBeamSection
{
    /*! The colour as 0xRRGGBB. */
    quint32 colour = 0;
    /*! The blocks it reaches up. */
    int height = 0;

    bool operator==(const QcBeamSection &) const = default;
};
Q_DECLARE_TYPEINFO(QcBeamSection, Q_PRIMITIVE_TYPE);

/*! \brief The beacon's rules as in the reference game (decisions E-1142 to E-1144): the pyramid
    of base blocks under it gives it a level from 1 to 4, its beam needs open sky above it
    (stained glass colours it), and while both hold it gives the players within its range the
    status effects chosen on its screen.

    Pure, so the same blocks give the same level, beam and effects on the server, in tests and
    on the screen. Effects are named by their kinds (QcEffectDefinition::name). */
namespace QcBeacon {

/*! The highest level: four layers of the pyramid. */
inline constexpr int maxLevel = 4;
/*! The level of the secondary effect (and of the primary effect at level II). */
inline constexpr int secondaryLevel = 4;
/*! Ticks between two looks at the pyramid, the beam and the players (the reference's 80). */
inline constexpr int checkIntervalTicks = 80;
/*! The item tag of the blocks a pyramid is built of (iron, gold, emerald, diamond and netherite
    blocks in the base game, any mix). */
inline constexpr QLatin1StringView baseTag("qtcraft:beacon_base");
/*! The item tag of the items that pay for a choice of effects. */
inline constexpr QLatin1StringView paymentTag("qtcraft:beacon_payment");
/*! The colour of a beam that passes through no coloured block (the reference's white dye). */
inline constexpr quint32 plainColour = 0xF9FFFE;
/*! The block that does not stop a beam although light cannot pass it (the reference's bedrock,
    so a beacon under the Nether's roof still shines into the void above). */
inline constexpr QLatin1StringView beamPassesBlock("qtcraft:bedrock");

/*! What stands at a position of a world (none: not loaded there). */
using BlockAt = std::function<std::optional<QcBlockId>(QcBlockPos)>;

/*! Returns the effects that may be the primary effect of a beacon of \a level, in the order of
    the screen: speed and haste from level 1, resistance and jump boost from level 2, strength
    from level 3. */
[[nodiscard]] QStringList primaryEffects(int level);
/*! Returns the effect that may be the secondary effect at secondaryLevel besides the primary
    one at level II: regeneration. */
[[nodiscard]] QString secondaryEffect();
/*! Returns the level from which \a effect may be chosen (1 to 4), 0 for an effect a beacon never
    gives. */
[[nodiscard]] int levelOf(const QString &effect);
/*! Returns whether a beacon of \a level may take \a primary and \a secondary (empty: none): the
    primary one of primaryEffects(level), the secondary one empty or, from secondaryLevel, the
    primary one again (level II) or secondaryEffect(). A level of 0 takes nothing. */
[[nodiscard]] bool mayChoose(int level, const QString &primary, const QString &secondary);

/*! Returns the blocks around a beacon of \a level within which players get its effects: 10 + 10
    per level (20, 30, 40, 50). */
[[nodiscard]] constexpr int rangeOf(int level)
{
    return 10 + 10 * level;
}
/*! Returns the ticks the effects of a beacon of \a level last: 9 + 2 per level seconds, so they
    outlast checkIntervalTicks and players in range keep them. */
[[nodiscard]] constexpr int durationTicksOf(int level)
{
    return (9 + 2 * level) * 20;
}
/*! Returns whether a player whose collision box is \a body is within the reach of a beacon at
    \a beacon of \a level: the box touches the beacon's cell grown by rangeOf() blocks on every
    side and without end upwards (the reference grows its box by the world's height upwards). */
[[nodiscard]] bool reaches(QcBlockPos beacon, int level, const QcAabb &body);
/*! Returns the effects a beacon of \a level with \a primary and \a secondary gives each player
    in range: the primary one at level I (level II when the secondary one is the same and the
    level is at least secondaryLevel) and from secondaryLevel the secondary one at level I,
    each for durationTicksOf(level); nothing at level 0 or without a primary effect. */
[[nodiscard]] QList<QcStatusEffect> effectsOf(int level, const QString &primary, const QString &secondary);

/*! Returns whether \a item of \a content pays for a choice of effects (the paymentTag). */
[[nodiscard]] bool isPayment(const QcContentRegistry &content, QcItemId item);
/*! Returns the level of a beacon at \a beacon in the world \a blockAt tells, by the pyramid of
    \a content's base blocks below it: layer n (1 to maxLevel) is the square of side 2n + 1
    centred n blocks under it, all of base blocks; the level is the number of full layers from
    the top, stopping at the first that is not full, not loaded or below the world. */
[[nodiscard]] int levelAt(const QcContentRegistry &content, QcBlockPos beacon, const BlockAt &blockAt);
/*! Returns the beam of a beacon at \a beacon in the world \a blockAt tells, by the blocks of
    \a content, as the reference finds it: from the beacon up to the top of the world, a block
    with a QcBlockDefinition::beamColour starts a section of its colour (the first one of its own
    colour, later ones mixed half and half with the section below), every other block lengthens
    the section; a block that light cannot pass (light opacity 15, except beamPassesBlock) or one
    not loaded stops the beam: then there is none. */
[[nodiscard]] QList<QcBeamSection> beamAt(const QcContentRegistry &content, QcBlockPos beacon, const BlockAt &blockAt);

} // namespace QcBeacon
