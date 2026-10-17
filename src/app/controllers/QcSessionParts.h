#pragma once

#include "app/rendering/QcWorldView.h"

#include <QPointer>
#include <QQuickWindow>

#include <memory>

class QcClientSession;
class QcLocalServerHost;
class QcMeshScheduler;

// The running world as QcGameController and its collaborators see it: the in-process server, the
// client session and its mesh scheduler (all null in the menu), plus the world view and its window.
// QcGameController owns and changes it; the collaborators only read it. The members are declared in
// the order they depend on each other: the scheduler goes before the session, the session before
// the server.
struct QcSessionParts
{
    std::unique_ptr<QcLocalServerHost> server;
    std::unique_ptr<QcClientSession> session;
    std::unique_ptr<QcMeshScheduler> meshes;
    QPointer<QcWorldView> view;
    QPointer<QQuickWindow> window;
};
