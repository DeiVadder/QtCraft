// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcGamepadProfile.h"

#include <QList>
#include <QString>
#include <QStringList>

class QSettings;

/*! \brief The gamepad part of the settings (decisions E-541, E-610, E-611): the profile all pads
    use (\c {standard}, stored as gamepad/<key> as before), the pad models with a profile of
    their own (by SDL's GUID, stored as gamepadControllers/<n>/<key>, at most maxControllers) and
    whether the hotbar shows only the slot in the hand (\c {radialOnly}, decision E-614). */
struct QcGamepadSettings
{
    /*! Pad models with a profile of their own, at most. */
    static constexpr qsizetype maxControllers = 32;

    /*! \brief A pad model with its own profile; \c {name} is the pad's name when the profile was
        made. */
    struct Controller
    {
        /*! SDL's GUID of the pad model. */
        QString guid;
        /*! The pad's name when the profile was made. */
        QString name;
        /*! The model's own profile. */
        QcGamepadProfile profile;

        friend bool operator==(const Controller &, const Controller &) = default;
    };

    /*! The profile all pads use that have none of their own. */
    QcGamepadProfile standard;
    /*! The pad models with a profile of their own, the one changed longest ago first. */
    QList<Controller> controllers;
    /*! Whether the hotbar shows only the slot in the hand. */
    bool radialOnly = false;

    /*! Returns the profile of the model \a guid: its own, else the standard one. */
    const QcGamepadProfile &profileFor(const QString &guid) const;
    /*! Returns the model's own entry; nullptr when it uses the standard profile (or \a guid is
        empty). */
    const Controller *controller(const QString &guid) const;
    /*! Stores \a profile as the own of the model \a guid named \a name and moves its entry last:
        the entries are kept in the order of their last change. Beyond maxControllers the entry
        changed longest ago goes, but never one of the models in \a keep (the connected pads). */
    void setProfile(const QString &guid, const QString &name, const QcGamepadProfile &profile, const QStringList &keep = {});
    /*! The model \a guid uses the standard profile again. */
    void removeProfile(const QString &guid);

    /*! Returns the gamepad settings stored in \a file; defaults for missing values. */
    static QcGamepadSettings load(const QSettings &file);
    /*! Stores the gamepad settings in \a file. */
    void save(QSettings &file) const;

    friend bool operator==(const QcGamepadSettings &, const QcGamepadSettings &) = default;
};
