#include "QcRelativeMouse.h"

struct QcRelativeMouse::Platform
{
};

QcRelativeMouse::QcRelativeMouse() = default;

QcRelativeMouse::~QcRelativeMouse() = default;

bool QcRelativeMouse::hasPlatformDeltas()
{
    return false;
}

void QcRelativeMouse::setCaptured(bool)
{
}

QPointF QcRelativeMouse::takeDelta()
{
    return {};
}
