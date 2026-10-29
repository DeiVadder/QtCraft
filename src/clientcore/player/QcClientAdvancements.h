// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcAdvancementProgress.h"
#include "common/protocol/QcAdvancementMessages.h"

#include <QSet>
#include <QStringList>

// The local player's progress in the adventure guide as the server told it (decision E-923,
// QcMsg::AdvancementProgress); the definitions come with the content (QcContentRegistry::advancements).
class QcClientAdvancements
{
public:
    // Takes over a message; the advancements the server announced (reached just now, for the toast),
    // in the order of the message. Only advancements of `content` count as reached (none without it).
    QStringList apply(const QcMsg::AdvancementProgress &message, const QcContentRegistry *content);

    const QcAdvancementProgress &progress() const { return m_progress; }
    bool isDone(const QString &advancement) const { return m_done.contains(advancement); }
    qsizetype doneCount() const { return m_done.size(); }

private:
    QcAdvancementProgress m_progress;
    QSet<QString> m_done;  // NOLINT(qc-member-access): the whole state of the class; its methods are the accessors
};
