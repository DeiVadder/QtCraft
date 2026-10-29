// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/preview/QcPreviewHook.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] std::optional<double> number(const QString &text, double low, double high)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok && std::isfinite(value) && value >= low && value <= high ? std::optional(value) : std::nullopt;
}

[[nodiscard]] std::optional<std::pair<double, double>> point(const QString &text)
{
    const QStringList parts = text.split(u':');
    if (parts.size() != 2) {
        return std::nullopt;
    }
    const std::optional<double> x = number(parts[0], -1.0, 1.0);
    const std::optional<double> y = number(parts[1], -1.0, 1.0);
    return x && y ? std::optional(std::pair{*x, *y}) : std::nullopt;
}

[[nodiscard]] std::optional<QcPreviewCamera::Side> sideNamed(const QString &text)
{
    if (text == u"front"_s) {
        return QcPreviewCamera::Side::Front;
    }
    if (text == u"back"_s) {
        return QcPreviewCamera::Side::Back;
    }
    return std::nullopt;
}

} // namespace

bool QcPreviewHook::set(const QString &key, const QString &value)
{
    if (key == u"animation"_s) {
        animation = QcPreviewPose::fromName(value);
        return animation.has_value();
    }
    if (key == u"side"_s) {
        side = sideNamed(value);
        return side.has_value();
    }
    if (key == u"turn"_s) {
        turn = number(value, -360.0, 360.0);
        return turn.has_value();
    }
    if (key == u"zoom"_s) {
        zoom = number(value, 0.0, 1.0);
        return zoom.has_value();
    }
    if (key == u"gaze"_s) {
        gaze = point(value);
        return gaze.has_value();
    }
    if (key == u"time"_s) {
        time = number(value, 0.0, 3600.0);
        return time.has_value();
    }
    return false;
}

QcResult<QcPreviewHook> QcPreviewHook::parse(const QString &text)
{
    QcPreviewHook hook;
    const QStringList pairs = text.split(u',', Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        const qsizetype equals = pair.indexOf(u'=');
        const QString key = pair.left(equals).trimmed();
        if (equals < 0 || !hook.set(key, pair.mid(equals + 1).trimmed())) {
            return qcError(u"Invalid --preview setting '%1' (animation=stand|walk|mine, side=front|back, turn=<degrees>, zoom=<0..1>, gaze=<x>:<y>, time=<seconds>)"_s.arg(pair.trimmed()));
        }
    }
    return hook;
}
