#pragma once

#include "clientcore/camera/QcViewCamera.h"
#include "clientcore/settings/QcCharacterProfile.h"
#include "clientcore/settings/QcDebugOptions.h"
#include "clientcore/settings/QcGamepadSettings.h"
#include "clientcore/settings/QcUiScale.h"
#include "common/content/definitions/QcAppearance.h"
#include "common/core/QcResult.h"
#include "common/gameplay/QcKeyBindings.h"

#include <QString>
#include <QStringList>

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
    // Most samples the block shader takes along a slanted footprint (decision E-721): 1 (off), 2,
    // 4 or 8.
    static constexpr int defaultTextureFiltering = 4;
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
    // Photo mode (decision E-692): hiding the HUD hides the hand as well.
    bool photoHidesHand = true;
    // A world hosted in the game opens its pause menu, and so pauses, when the window loses the
    // focus (decision E-877).
    bool pauseOnFocusLoss = false;
    // Explosions near the player shake the camera (decision E-879).
    bool cameraShake = true;
    // "" follows the system language, otherwise a two-letter language code such as "de".
    QString language;
    QcCameraMode cameraMode = QcCameraMode::FirstPerson;
    // Size of the HUD and the screens (decision E-725), stored as "uiScale" ("auto", "75" … "200");
    // an unknown value gives the default.
    QcUiScale::Step uiScale = QcUiScale::defaultStep;
    // Texture filtering of surfaces seen at a flat angle, stored as "textureFiltering" (1, 2, 4 or
    // 8 samples); another value gives the default.
    int textureFiltering = defaultTextureFiltering;
    // The player's look; names are checked against the content by the server.
    QcAppearance appearance;
    // Looks the player saved under a name (at most QcCharacterProfile::maxPresets).
    QList<QcCharacterPreset> characterPresets;
    // The volumes of the sound and whether it is muted, stored as audio/<key>.
    struct AudioVolumes
    {
        double master = defaultMasterVolume;
        double effects = defaultEffectsVolume;
        double music = defaultMusicVolume;
        double ambient = defaultAmbientVolume;
        bool muted = false;

        bool operator==(const AudioVolumes &) const = default;
    };
    AudioVolumes audio;
    // Stored as "keys/<action>" (primary) and "keys/<action>.2" (secondary) with portable key names,
    // "" for no key. A missing or unknown name gives that slot's default; keys an earlier action
    // already uses are dropped. The pause key (Esc) is fixed.
    QcKeyBindings keys = QcKeyBindings::defaults();
    // Dead zones, curve and look speed of the gamepad (decision E-541), stored as gamepad/<key>.
    QcGamepadSettings gamepad;
    // Active texture packs by id, the highest priority first (decision E-297); empty: the game's own
    // textures. Stored as the INI array "texturePacks/<n>/id"; at most maxTexturePacks, duplicates dropped.
    QStringList texturePacks;
    static constexpr int maxTexturePacks = 16;

    static int clampViewDistance(int chunks);
    // Whether `samples` is one of the texture filtering choices (1, 2, 4 or 8).
    static bool isTextureFiltering(int samples) { return samples == 1 || samples == 2 || samples == 4 || samples == 8; }
    static double clampFieldOfView(double degrees);
    // Clamps to 0..1; NaN and infinities give the default (full effects), like the other clamps.
    static double clampFovEffects(double share);
    static double clampMouseSensitivity(double sensitivity);
    static QString cleanLanguage(const QString &language);
    // Clamps to 0..1; non-finite values give `fallback`.
    static double clampVolume(double volume, double fallback);
    // Drops empty and repeated ids and keeps at most maxTexturePacks.
    static QStringList cleanTexturePacks(const QStringList &ids);

    // A missing or unreadable file gives the defaults.
    static QcGameSettings load(const QString &path);
    [[nodiscard]] QcStatus save(const QString &path) const;

    friend bool operator==(const QcGameSettings &, const QcGameSettings &) = default;
};
