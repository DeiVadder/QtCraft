// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcClientAdvancements.h"

QStringList QcClientAdvancements::apply(const QcMsg::AdvancementProgress &message, const QcContentRegistry *content)
{
    if (message.reset) {
        m_progress = {};
        m_done.clear();
    }

    QStringList announced;
    for (const QcMsg::AdvancementProgress::Entry &entry : message.entries) {
        const bool accepted = m_progress.setCriteria(entry.advancement, QSet<QString>(entry.criteria.cbegin(), entry.criteria.cend()));
        const bool known = content && content->advancements().find(entry.advancement);
        if (entry.done && accepted && known) {
            m_done.insert(entry.advancement);
        } else {
            m_done.remove(entry.advancement);
        }
        if (entry.announce && m_done.contains(entry.advancement)) {
            announced.append(entry.advancement);
        }
    }
    return announced;
}
