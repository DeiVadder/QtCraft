// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

struct QcServerClient;
class QcServerState;

// Name tags on creatures (decision E-866): a name tag with a custom name (written at the anvil) used
// on a creature gives it that name, shown above it and in its death message; a named creature is
// kept (QcCreature::persistent), so it never despawns and is saved with the world. The tag is used
// up outside creative mode. Any name works the same; no name has a meaning of its own. A guardian
// (QcCreature::guards, decision E-591) leaves its zone with the name and becomes an ordinary kept
// creature; its zone may bring a successor (decision E-873).
namespace QcNameTags {

// Names the creature `id` with the name tag in the player's hand; false (nothing happens) when the
// hand holds no named name tag or `id` is no creature within reach.
bool use(QcServerState &state, QcServerClient &client, quint32 id);

} // namespace QcNameTags
