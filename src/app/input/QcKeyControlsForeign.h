#pragma once

#include "QcKeyControls.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes QcKeyControls (qc_clientcore) known to QML as KeyControls.
struct QcKeyControlsForeign
{
    Q_GADGET
    QML_FOREIGN(QcKeyControls)
    QML_NAMED_ELEMENT(KeyControls)
    QML_UNCREATABLE("Provided by GameController.settings.keys")
};
