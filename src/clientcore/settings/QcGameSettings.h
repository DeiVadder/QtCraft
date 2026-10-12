#pragma once

#include "QcAppearance.h"
#include "QcCharacterProfile.h"
#include "QcDebugOptions.h"
#include "QcKeyBindings.h"
#include "QcResult.h"
#include "QcViewCamera.h"

#include <QString>

// Player settings of the graphical game, stored as an INI file in the data directory. Values read
// from the file are clamped to their ranges; anything unusable falls back to the default.
struct QcGameSettings
{
    static constexpr int defaultViewDistance = 8;
    static constexpr int minViewDistance = 2;
    static constexpr int maxViewDistance = 16;
    static constexpr double defaultFieldOfView = 70.0;
    static constexpr double minFieldOfView = 50.0;
    static constexpr double maxFieldOfView = 110.0;
    // Share of the field-of-view widening while sprinting (QcFovEffect, decision E-280), 0..1.
    static constexpr double defaultFovEffects = 1.0;
    static constexpr double defaultMouseSensitivity = 0.15;
    static constexpr double minMouseSensitivity = 0.02;
    static constexpr double maxMouseSensitivity = 1.0;
    // Volumes are shares in 0..1.
    static constexpr double defaultMasterVolume = 0.8;
    static constexpr double defaultEffectsVolume = 1.0;
    static constexpr double defaultMusicVolume = 0.5;
    static constexpr double defaultAmbientVolume = 0.7;

    int viewDistance = defaultViewDistance;
    double fieldOfView = defaultFieldOfView;
    double fovEffects = defaultFovEffects;
    double mouseSensitivity = defaultMouseSensitivity;
    bool invertMouse = false;
    // Debug display level and visualisations (decision E-225).
    QcDebugOptions debug;
    // Labels floating in the world (showcase names, decision E-222).
    bool showLabels = true;
    // "" follows the system language, otherwise a two-letter language code such as "de".
    QString language;
    QcCameraMode cameraMode = QcCameraMode::FirstPerson;
    // The player's look; names are checked against the content by the server.
    QcAppearance appearance;
    // Looks the player saved under a name (at most QcCharacterProfile::maxPresets).
    QList<QcCharacterPreset> characterPresets;
    double masterVolume = defaultMasterVolume;
    double effectsVolume = defaultEffectsVolume;
    double musicVolume = defaultMusicVolume;
    double ambientVolume = defaultAmbientVolume;
    bool muted = false;
    // Stored as "keys/<action>" (primary) and "keys/<action>.2" (secondary) with portable key names,
    // "" for no key. A missing or unknown name gives that slot's default; keys an earlier action
    // already uses are dropped. The pause key (Esc) is fixed.
    QcKeyBindings keys = QcKeyBindings::defaults();

    static int clampViewDistance(int chunks);
    static double clampFieldOfView(double degrees);
    // Clamps to 0..1; NaN and infinities give the default (full effects), like the other clamps.
    static double clampFovEffects(double share);
    static double clampMouseSensitivity(double sensitivity);
    static QString cleanLanguage(const QString &language);
    // Clamps to 0..1; non-finite values give `fallback`.
    static double clampVolume(double volume, double fallback);

    // A missing or unreadable file gives the defaults.
    static QcGameSettings load(const QString &path);
    QcStatus save(const QString &path) const;

    friend bool operator==(const QcGameSettings &, const QcGameSettings &) = default;
};
