#pragma once

#include "QcResult.h"

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
    static constexpr double defaultMouseSensitivity = 0.15;
    static constexpr double minMouseSensitivity = 0.02;
    static constexpr double maxMouseSensitivity = 1.0;

    int viewDistance = defaultViewDistance;
    double fieldOfView = defaultFieldOfView;
    double mouseSensitivity = defaultMouseSensitivity;
    bool invertMouse = false;
    bool showDebug = true;
    // "" follows the system language, otherwise a two-letter language code such as "de".
    QString language;

    static int clampViewDistance(int chunks);
    static double clampFieldOfView(double degrees);
    static double clampMouseSensitivity(double sensitivity);
    static QString cleanLanguage(const QString &language);

    // A missing or unreadable file gives the defaults.
    static QcGameSettings load(const QString &path);
    QcStatus save(const QString &path) const;

    friend bool operator==(const QcGameSettings &, const QcGameSettings &) = default;
};
