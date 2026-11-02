// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/preview/QcPreviewCamera.h"
#include "clientcore/preview/QcPreviewPose.h"
#include "common/core/QcResult.h"

#include <optional>

/*! \brief Test hook --preview: fixes what every character preview shows, for reproducible
    screenshots (one per animation, side, zoom and gaze).

    Given as "key=value" pairs separated by commas: animation=stand|walk|mine, side=front|back,
    turn=<degrees from the front>, zoom=<0..1>, gaze=<x>:<y> (the pointer in the view, -1..1 from
    the centre, y up), time=<seconds of the animation, which then stands still>. */
struct QcPreviewHook
{
    /*! Fixed animation. */
    std::optional<QcPreviewPose::Animation> animation;
    /*! Fixed side. */
    std::optional<QcPreviewCamera::Side> side;
    /*! Fixed turn in degrees from the front. */
    std::optional<double> turn;
    /*! Fixed zoom (0..1). */
    std::optional<double> zoom;
    /*! Fixed pointer in the view (x, y in -1..1 from the centre, y up). */
    std::optional<std::pair<double, double>> gaze;
    /*! Fixed time of the animation in seconds. */
    std::optional<double> time;

    /*! Returns whether the hook fixes nothing. */
    bool isEmpty() const { return !animation && !side && !turn && !zoom && !gaze && !time; }

    /*! Returns the hook given by \a text; an unknown key or a value out of range is an error
        naming it. */
    [[nodiscard]] static QcResult<QcPreviewHook> parse(const QString &text);

private:
    // Sets the field of `key` from `value`; false when the key is unknown or the value invalid.
    [[nodiscard]] bool set(const QString &key, const QString &value);
};
