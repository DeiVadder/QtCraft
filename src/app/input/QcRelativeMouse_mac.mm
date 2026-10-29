// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/input/QcRelativeMouse.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

// AppKit delivers every mouse motion with its delta, also while the cursor is detached and stands
// still; the local monitor sees them before Qt does.
struct QcRelativeMouse::Platform
{
    id monitor = nil;
    bool captured = false;
    double dx = 0.0;
    double dy = 0.0;
};

QcRelativeMouse::QcRelativeMouse()
    : m_platform(std::make_unique<Platform>())
{
    Platform *platform = m_platform.get();
    const NSEventMask motion = NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged
                               | NSEventMaskOtherMouseDragged;

    platform->monitor = [NSEvent addLocalMonitorForEventsMatchingMask:motion
                                                              handler:^NSEvent *(NSEvent *event) {
                                                                  if (platform->captured) {
                                                                      platform->dx += event.deltaX;
                                                                      platform->dy += event.deltaY;
                                                                  }
                                                                  return event;
                                                              }];
}

QcRelativeMouse::~QcRelativeMouse()
{
    setCaptured(false);
    if (m_platform->monitor) {
        [NSEvent removeMonitor:m_platform->monitor];
    }
}

bool QcRelativeMouse::hasPlatformDeltas()
{
    return true;
}

void QcRelativeMouse::setCaptured(bool captured)
{
    if (m_platform->captured == captured) {
        return;
    }

    m_platform->captured = captured;
    m_platform->dx = 0.0;
    m_platform->dy = 0.0;
    // Detached, the cursor stays where it is while the mouse moves; no warping, no lost motion.
    CGAssociateMouseAndMouseCursorPosition(!captured);
}

QPointF QcRelativeMouse::takeDelta()
{
    const QPointF delta(m_platform->dx, m_platform->dy);
    m_platform->dx = 0.0;
    m_platform->dy = 0.0;
    return delta;
}
