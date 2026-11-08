// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientSmithing.h"

bool QcClientSmithing::sameState(const QcMsg::SmithingState &a, const QcMsg::SmithingState &b) const
{
    return a.templateStack == b.templateStack && a.base == b.base && a.addition == b.addition && a.output == b.output;
}

bool QcClientSmithing::forge()
{
    if (state().output.isEmpty()) {
        return false;
    }
    return sendWhileOpen(QcMsg::ForgeSmithing{state().output});
}
