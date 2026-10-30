// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QString>
#include <QStringView>

#include <optional>
#include <span>

/*! \brief The generator values a world option may set (decision E-917): one row each, with what it
    reads from the content as the default and how it writes a world's value into that world's copy
    of the content.

    A new option on an existing target needs only worldgen/options.json; a new target is one row
    here. */
namespace QcWorldOptionTargets {

/*! \brief What values a target takes. */
enum class Kind {
    /*! A number within [low, high]. */
    Number,
    /*! On (1) or off (0). */
    Switch,
};

/*! \brief One generator value a world option may set. */
struct Target
{
    /*! The name options use for the target (\c {"villages.spacing"}). */
    QStringView name;
    /*! What values the target takes. */
    Kind kind = Kind::Number;
    /*! The target is about one definition (an ore), named by the option's \c {subject}. */
    bool needsSubject = false;
    /*! The lowest and the highest value: values outside these never reach the content; an
        option's range must lie within them. \qcshared */
    double low = 0.0;
    double high = 1.0;
    /*! The value in \c {content}; empty when the subject is unknown. Factors read 1. */
    std::optional<double> (*read)(const QcContentRegistry &content, const QString &subject) = nullptr;
    /*! Sets \c {value} (within [low, high]) in \c {content}. */
    void (*write)(QcContentRegistry &content, const QString &subject, double value) = nullptr;
};

/*! Returns every target. */
std::span<const Target> all();
/*! Returns the target named \a name; null when there is none. */
[[nodiscard]] const Target *find(QStringView name);

} // namespace QcWorldOptionTargets
