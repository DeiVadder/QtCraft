#pragma once

#include <QPointF>

#include <memory>

// Relative mouse motion while the game holds the mouse. Where the platform reports motion deltas
// (macOS), the cursor is detached from the mouse and the deltas are summed up; elsewhere the
// caller measures motion against the window centre and warps the cursor back.
class QcRelativeMouse
{
public:
    QcRelativeMouse();
    ~QcRelativeMouse();
    QcRelativeMouse(const QcRelativeMouse &) = delete;
    QcRelativeMouse &operator=(const QcRelativeMouse &) = delete;

    static bool hasPlatformDeltas();
    void setCaptured(bool captured);
    // Motion since the last call, in points (y down).
    QPointF takeDelta();

private:
    struct Platform;
    std::unique_ptr<Platform> m_platform;
};
