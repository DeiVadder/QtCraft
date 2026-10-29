// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QString>
#include <QStringView>

#include <optional>
#include <span>

// The generator values a world option may set (decision E-917): one row each, with what it reads
// from the content as the default and how it writes a world's value into that world's copy of the
// content. A new option on an existing target needs only worldgen/options.json; a new target is one
// row here.
namespace QcWorldOptionTargets {

enum class Kind {
    // A number within [low, high].
    Number,
    // On (1) or off (0).
    Switch,
};

struct Target
{
    QStringView name;
    Kind kind = Kind::Number;
    // The target is about one definition (an ore), named by the option's `subject`.
    bool needsSubject = false;
    // Values outside these never reach the content; an option's range must lie within them.
    double low = 0.0;
    double high = 1.0;
    // The value in `content`; empty when the subject is unknown. Factors read 1.
    std::optional<double> (*read)(const QcContentRegistry &content, const QString &subject) = nullptr;
    // Sets `value` (within [low, high]) in `content`.
    void (*write)(QcContentRegistry &content, const QString &subject, double value) = nullptr;
};

std::span<const Target> all();
// The target named `name`; null when there is none.
[[nodiscard]] const Target *find(QStringView name);

} // namespace QcWorldOptionTargets
