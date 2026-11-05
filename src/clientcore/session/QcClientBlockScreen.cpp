// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientBlockScreen.h"

bool QcClientBlockScreen::sendWhileOpen(const QcMessage &message) const
{
    if (!isOpen() || !messageSender()) {
        return false;
    }
    messageSender()(message);
    return true;
}

void QcClientBlockScreen::markOpened(QcBlockPos pos)
{
    setOpen(true);
    m_pos = pos;
    emit opened();
}

void QcClientBlockScreen::markClosedByServer()
{
    setOpen(false);
    emit closed();
}

void QcClientBlockScreen::markClosedByPlayer(const QcMessage &message)
{
    setOpen(false);
    if (messageSender()) {
        messageSender()(message);
    }
    emit closed();
}
