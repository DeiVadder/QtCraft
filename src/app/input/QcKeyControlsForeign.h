// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcKeyControls.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes QcKeyControls (qc_clientcore) known to QML as KeyControls.
struct QcKeyControlsForeign
{
    Q_GADGET
    QML_FOREIGN(QcKeyControls)
    QML_NAMED_ELEMENT(KeyControls)
    QML_UNCREATABLE("Provided by GameController.settings.keys")
};
