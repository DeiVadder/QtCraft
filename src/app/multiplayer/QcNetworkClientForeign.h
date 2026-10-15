#pragma once

#include "QcNetworkClient.h"

#include <QtQmlIntegration/qqmlintegration.h>

// Makes QcNetworkClient (qc_clientcore) known to QML as NetworkClient, for its Stage values
// (GameController.multiplayer.joinStage).
struct QcNetworkClientForeign
{
    Q_GADGET
    QML_FOREIGN(QcNetworkClient)
    QML_NAMED_ELEMENT(NetworkClient)
    QML_UNCREATABLE("Used by GameController.multiplayer")
};
