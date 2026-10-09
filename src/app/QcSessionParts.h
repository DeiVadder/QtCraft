#pragma once

#include "QcWorldView.h"

#include <QPointer>
#include <QQuickWindow>

#include <memory>

class QcClientSession;
class QcLocalServerHost;
class QcMeshScheduler;

// The running world as QcGameController and its collaborators see it: the in-process server, the
// client session and its mesh scheduler (all null in the menu), plus the world view and its window.
// QcGameController owns and changes it; the collaborators only read it.
struct QcSessionParts
{
    std::unique_ptr<QcLocalServerHost> server;
    QcClientSession *session = nullptr;
    QcMeshScheduler *meshes = nullptr;
    QPointer<QcWorldView> view;
    QPointer<QQuickWindow> window;
};
