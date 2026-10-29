// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/QcAudioRoute.h"

namespace {

// The default output if it is there, else the first device; empty without devices.
QByteArray preferred(const QList<QByteArray> &outputs, const QByteArray &defaultOutput)
{
    if (!defaultOutput.isEmpty() && outputs.contains(defaultOutput)) {
        return defaultOutput;
    }
    return outputs.isEmpty() ? QByteArray() : outputs.constFirst();
}

} // namespace

QcAudioRoute::Decision QcAudioRoute::decide(const QByteArray &current, const QList<QByteArray> &outputs, const QByteArray &defaultOutput)
{
    using Action = Decision::Action;
    const QByteArray next = preferred(outputs, defaultOutput);
    if (current.isEmpty()) {
        return next.isEmpty() ? Decision{} : Decision{Action::Switch, next};
    }

    if (!outputs.contains(current)) {
        return next.isEmpty() ? Decision{Action::Close, {}} : Decision{Action::Switch, next};
    }

    // A new default the system knows of; a default that is not listed (yet) is not followed.
    if (next != current && next == defaultOutput) {
        return {Action::Switch, next};
    }
    return {};
}
