// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payload of the boss bar message (QcBossBarMessages.h, decision E-1127).

#include "common/content/mobs/QcBossBarDefinition.h"
#include "common/protocol/QcProtocolCodec.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

namespace {

// The longest kind and name a boss bar carries.
constexpr qsizetype maxBossBarText = 256;

} // namespace

void encode(QDataStream &stream, const QcMsg::BossBar &m)
{
    stream << m.entity << m.kind << m.name << m.progress << m.colour << m.shown;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BossBar>, QDataStream &stream)
{
    QcMsg::BossBar m;
    stream >> m.entity >> m.kind >> m.name >> m.progress >> m.colour >> m.shown;
    if (m.entity == 0 || m.kind.size() > maxBossBarText || m.name.size() > maxBossBarText || !std::isfinite(m.progress)
        || m.progress < 0.0f || m.progress > 1.0f || m.colour >= qcBossBarColours) {
        return qcError(u"Invalid boss bar"_s);
    }
    return m;
}

} // namespace QcProtocolCodec
