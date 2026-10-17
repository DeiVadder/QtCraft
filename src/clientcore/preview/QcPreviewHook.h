#pragma once

#include "clientcore/preview/QcPreviewCamera.h"
#include "clientcore/preview/QcPreviewPose.h"
#include "common/core/QcResult.h"

#include <optional>

// Test hook --preview: fixes what every character preview shows, for reproducible screenshots
// (one per animation, side, zoom and gaze). Given as "key=value" pairs separated by commas:
// animation=stand|walk|mine, side=front|back, turn=<degrees from the front>, zoom=<0..1>,
// gaze=<x>:<y> (the pointer in the view, -1..1 from the centre, y up), time=<seconds of the
// animation, which then stands still>.
struct QcPreviewHook
{
    std::optional<QcPreviewPose::Animation> animation;
    std::optional<QcPreviewCamera::Side> side;
    std::optional<double> turn;
    std::optional<double> zoom;
    std::optional<std::pair<double, double>> gaze;
    std::optional<double> time;

    bool isEmpty() const { return !animation && !side && !turn && !zoom && !gaze && !time; }

    // An unknown key or a value out of range is an error naming it.
    [[nodiscard]] static QcResult<QcPreviewHook> parse(const QString &text);
};
