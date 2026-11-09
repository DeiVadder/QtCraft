// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The boss of QcMobReader (decisions E-1125, E-1127): "hydra" and "bossBar".

#include "common/content/mobs/QcMobReader.h"

#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace {

// The names of the boss bar colours, in the order of QcBossBarColour.
constexpr std::array<const char *, qcBossBarColours> colourNames{"pink", "blue", "red", "green", "yellow", "purple", "white"};

// The blocks of the summoning T: one or more qualified block names.
QStringList summonBody(const QJsonValue &value, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QStringList names;
    const QJsonArray entries = value.toArray();
    for (const QJsonValueConstRef entry : entries) {
        const QString name = QcContentNames::qualify(entry.toString(), packNamespace);
        if (!entry.isString() || name.isEmpty()) {
            reader.fail(u"'summon.body' must name blocks"_s);
            return {};
        }
        names.append(name);
    }
    return names;
}

// "hydra": {"summon": {"body": [...], "head"}, "chargeSeconds", "chargeHeal", "blastPower",
// "summonSound", "regeneration", "armourShare", "hoverAbove", "attackRange",
// "attackSeconds", "skull", "chargedSkull", "skullSpeed", "chargedSkullSpeed", "chargedChance",
// "breakSound"}.
QcHydraDefinition parseHydra(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcHydraDefinition hydra;
    reader.nestedObject(u"summon"_s, [&](const QJsonObject &summon, QcJsonFieldReader &nested) {
        hydra.summonBody = summonBody(summon.value(u"body"_s), packNamespace, nested);
        hydra.summonHead = nested.name(u"head"_s, packNamespace);
    });
    hydra.chargeSeconds = reader.number(u"chargeSeconds"_s, hydra.chargeSeconds, 0.0, 60.0);
    hydra.chargeHeal = reader.number(u"chargeHeal"_s, hydra.chargeHeal, 0.0, 1000.0);
    hydra.blastPower = reader.number(u"blastPower"_s, hydra.blastPower, 0.0, 8.0);
    hydra.summonSound = reader.name(u"summonSound"_s, packNamespace);
    hydra.regeneration = reader.number(u"regeneration"_s, hydra.regeneration, 0.0, 100.0);
    hydra.armourShare = reader.number(u"armourShare"_s, hydra.armourShare, 0.0, 1.0);
    hydra.hoverAbove = reader.number(u"hoverAbove"_s, hydra.hoverAbove, 0.0, 24.0);
    hydra.attackRange = reader.number(u"attackRange"_s, hydra.attackRange, 1.0, 64.0);
    hydra.attackSeconds = reader.number(u"attackSeconds"_s, hydra.attackSeconds, 0.05, 60.0);
    hydra.skull = reader.name(u"skull"_s, packNamespace);
    hydra.chargedSkull = reader.name(u"chargedSkull"_s, packNamespace, hydra.skull);
    hydra.skullSpeed = reader.number(u"skullSpeed"_s, hydra.skullSpeed, 0.5, 100.0);
    hydra.chargedSkullSpeed = reader.number(u"chargedSkullSpeed"_s, hydra.chargedSkullSpeed, 0.5, 100.0);
    hydra.chargedChance = reader.number(u"chargedChance"_s, hydra.chargedChance, 0.0, 1.0);
    hydra.breakSound = reader.name(u"breakSound"_s, packNamespace);
    if (reader.ok() && (hydra.summonBody.isEmpty() || hydra.summonHead.isEmpty() || hydra.summonBody.contains(hydra.summonHead))) {
        reader.fail(u"needs a 'summon' with 'body' blocks and another 'head' block"_s);
    }
    if (reader.ok() && hydra.skull.isEmpty()) {
        reader.fail(u"needs a 'skull' to shoot"_s);
    }
    return hydra;
}

QcBossBarDefinition parseBossBar(QcJsonFieldReader &reader)
{
    QcBossBarDefinition bar;
    const QString colour = reader.string(u"colour"_s, u"purple"_s);
    const auto it = std::ranges::find_if(colourNames, [&](const char *name) { return colour == QLatin1StringView(name); });
    if (it == colourNames.end()) {
        reader.fail(u"'colour' must be pink, blue, red, green, yellow, purple or white"_s);
    } else {
        bar.colour = static_cast<QcBossBarColour>(it - colourNames.begin());
    }
    bar.range = reader.number(u"range"_s, bar.range, 1.0, 512.0);
    return bar;
}

} // namespace

void QcMobReader::parseBoss()
{
    m_reader.nestedObject(u"hydra"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.hydra = parseHydra(nested, m_namespace);
    });
    if (m_reader.ok() && m_mob.hydra.has_value() != (m_mob.ai == QcMobDefinition::Ai::Hydra)) {
        m_reader.fail(u"the hydra AI goes with 'hydra' settings, and they with it"_s);
    }
    if (m_reader.ok() && m_mob.hydra && (!m_mob.hostile || !m_mob.hover)) {
        m_reader.fail(u"the hydra AI needs 'hostile': true and 'hover'"_s);
    }
    m_reader.nestedObject(u"bossBar"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { m_mob.bossBar = parseBossBar(nested); });
}
