// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcDebugOptions.h"
#include "clientcore/settings/QcGameSettings.h"

#include <array>
#include <optional>

// The run's scene options of the command line (view distance, camera mode, debug level, each
// visualisation; QcSettingsController::keepSceneOptionsUnsaved()) that stand for the stored values
// until the player changes them. Each field is held on its own and released by the player's first
// change of that field, whatever value he picks: picking the scene's value again stores it, and a
// switched visualisation does not store the scene's level.
class QcSceneOverrides
{
public:
    // Holds the view distance given on the command line.
    void holdViewDistance() { m_viewDistance = true; }
    // Holds the camera mode.
    void holdCameraMode() { m_cameraMode = true; }
    // Holds the fields of the debug display \a scene that differ from \a stored.
    void holdDebug(QcDebugOptions scene, QcDebugOptions stored);

    // The player changed the view distance.
    void releaseViewDistance() { m_viewDistance = false; }
    // The player changed the camera mode.
    void releaseCameraMode() { m_cameraMode = false; }
    // The player changed the debug level.
    void releaseDebugLevel() { m_debugLevel = false; }
    // The player switched visualisations: releases those held whose value in \a now differs.
    void releaseChangedVisuals(QcDebugOptions now);

    // Returns \a current with each held field taken from \a stored: what the file gets.
    [[nodiscard]] QcGameSettings toStore(QcGameSettings current, const QcGameSettings &stored) const;

private:
    // Whether each field is held; hold...() and release...() are the accessors of these flags.
    bool m_viewDistance = false; // NOLINT(qc-member-access): set by holdViewDistance()/releaseViewDistance() only.
    bool m_cameraMode = false;   // NOLINT(qc-member-access): set by holdCameraMode()/releaseCameraMode() only.
    bool m_debugLevel = false;   // NOLINT(qc-member-access): set by holdDebug()/releaseDebugLevel() only.
    // The held scene value of each visualisation, indexed by QcDebugOptions::Visual.
    // NOLINT(qc-member-access): set by holdDebug()/releaseChangedVisuals() only, read by toStore().
    std::array<std::optional<bool>, QcDebugOptions::visualCount> m_visuals{};
};
