// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientAnvil.h"

bool QcClientAnvil::sameState(const QcMsg::AnvilState &a, const QcMsg::AnvilState &b) const
{
    return a.left == b.left && a.right == b.right && a.name == b.name && a.output == b.output && a.levelCost == b.levelCost;
}

bool QcClientAnvil::rename(const QString &name)
{
    return sendWhileOpen(QcMsg::RenameAnvil{name.left(QcMsg::RenameAnvil::maxText)});
}

bool QcClientAnvil::forge()
{
    if (state().output.isEmpty()) {
        return false;
    }
    return sendWhileOpen(QcMsg::ForgeAnvil{state().levelCost, state().output});
}
