// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <functional>

/*! \brief How a conversion of a closed world reports its progress and is stopped (decision
    E-1014): the switch of a generator version with its backup, in the menu and in
    qtcraft-headless.

    Both functions are called from the converting thread; either may be empty. */
struct QcConversionControl
{
    /*! The part of a conversion that runs. */
    enum class Stage {
        /*! The backup of the world folder is copied. */
        Backup,
        /*! The explored area and the structures reaching out of it are surveyed. */
        Survey,
        /*! The ring of a switch with a transition is generated and blended. */
        Blend,
        /*! The version map, the ring and world.json are written; no stop any more. */
        Write
    };

    /*! \brief Where a conversion stands: its stage and the steps done of the steps in it (0 of 0
        where a stage cannot count). \qcfields */
    struct Step
    {
        Stage stage = Stage::Backup;
        qint64 done = 0;
        qint64 total = 0;
    };

    /*! Called with each step reached. */
    std::function<void(const Step &step)> progress;
    /*! Returns whether the conversion is to stop; asked between steps, never once writing began. */
    std::function<bool()> cancelled;

    /*! Reports \a step to progress, if set. */
    void report(const Step &step) const
    {
        if (progress) {
            progress(step);
        }
    }
    /*! Returns whether cancelled says to stop (never without it). */
    [[nodiscard]] bool stopped() const { return cancelled && cancelled(); }
};
