// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/settings/QcSceneOverrides.h"

void QcSceneOverrides::holdDebug(QcDebugOptions scene, QcDebugOptions stored)
{
    if (scene.level != stored.level) {
        m_debugLevel = true;
    }
    for (std::size_t index = 0; index < m_visuals.size(); ++index) {
        if (scene.visuals[index] != stored.visuals[index]) {
            m_visuals[index] = scene.visuals[index];
        }
    }
}

void QcSceneOverrides::releaseChangedVisuals(QcDebugOptions now)
{
    for (std::size_t index = 0; index < m_visuals.size(); ++index) {
        std::optional<bool> &held = m_visuals[index];
        if (held && *held != now.visuals[index]) {
            held.reset();
        }
    }
}

QcGameSettings QcSceneOverrides::toStore(QcGameSettings current, const QcGameSettings &stored) const
{
    if (m_viewDistance) {
        current.viewDistance = stored.viewDistance;
    }
    if (m_cameraMode) {
        current.cameraMode = stored.cameraMode;
    }
    if (m_debugLevel) {
        current.debug.level = stored.debug.level;
    }
    for (std::size_t index = 0; index < m_visuals.size(); ++index) {
        if (m_visuals[index]) {
            current.debug.visuals[index] = stored.debug.visuals[index];
        }
    }
    return current;
}
