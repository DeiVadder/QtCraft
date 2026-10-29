// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientDebugSubscription.h"

void QcClientDebugSubscription::setFlags(quint8 flags)
{
    const quint8 before = this->flags();
    m_flags = flags;
    sendChange(before);
}

void QcClientDebugSubscription::setBaseFlags(quint8 flags)
{
    const quint8 before = this->flags();
    m_baseFlags = flags;
    sendChange(before);
}

void QcClientDebugSubscription::welcomed()
{
    m_welcomed = true;
    if (flags() != 0 && m_send) {
        m_send(QcMsg::DebugSubscribe{flags()});
    }
}

void QcClientDebugSubscription::sendChange(quint8 before)
{
    const quint8 now = flags();
    if (now == before) {
        return;
    }

    if (now == 0) {
        m_mirror.clear();
    }
    // Before the welcome the server expects nothing but Hello; welcomed() sends it then.
    if (m_welcomed && m_send) {
        m_send(QcMsg::DebugSubscribe{now});
    }
}
