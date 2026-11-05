// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcBlockPrediction.h"

using namespace Qt::StringLiterals;

bool QcBlockPrediction::wanted()
{
    return qEnvironmentVariable("QTCRAFT_BLOCK_PREDICTION") != u"0"_s;
}

quint32 QcBlockPrediction::next(const QList<QcMsg::BlockUpdate> &blocks)
{
    if (!isOn() || blocks.isEmpty()) {
        return 0;
    }
    return ++m_sequence;
}
