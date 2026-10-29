// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcGamepadProfile.h"

#include <QList>
#include <QString>
#include <QStringList>

class QSettings;

// The gamepad part of the settings (decisions E-541, E-610, E-611): the profile all pads use
// (`standard`, stored as gamepad/<key> as before), the pad models with a profile of their own
// (by SDL's GUID, stored as gamepadControllers/<n>/<key>, at most maxControllers) and whether the
// hotbar shows only the slot in the hand (`radialOnly`, decision E-614).
struct QcGamepadSettings
{
    static constexpr qsizetype maxControllers = 32;

    // A pad model with its own profile; `name` is the pad's name when the profile was made.
    struct Controller
    {
        QString guid;
        QString name;
        QcGamepadProfile profile;

        friend bool operator==(const Controller &, const Controller &) = default;
    };

    QcGamepadProfile standard;
    QList<Controller> controllers;
    bool radialOnly = false;

    // The profile of the model `guid`: its own, else the standard one.
    const QcGamepadProfile &profileFor(const QString &guid) const;
    // The model's own entry; nullptr when it uses the standard profile (or `guid` is empty).
    const Controller *controller(const QString &guid) const;
    // Stores `profile` as the model's own and moves its entry last: the entries are kept in the
    // order of their last change. Beyond maxControllers the entry changed longest ago goes, but
    // never one of the models in `keep` (the connected pads).
    void setProfile(const QString &guid, const QString &name, const QcGamepadProfile &profile, const QStringList &keep = {});
    // The model uses the standard profile again.
    void removeProfile(const QString &guid);

    static QcGamepadSettings load(const QSettings &file);
    void save(QSettings &file) const;

    friend bool operator==(const QcGamepadSettings &, const QcGamepadSettings &) = default;
};
