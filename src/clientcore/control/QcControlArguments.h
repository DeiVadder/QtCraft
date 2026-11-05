// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"

#include <QJsonObject>
#include <QString>

#include <optional>

/*! \brief Typed access to the parameters of a control request. */
class QcControlArguments
{
public:
    /*! Longest wait a tick-based command accepts. */
    static constexpr int maxCommandTicks = 20 * 600;

    /*! Returns the error message for the first parameter of the wrong JSON type of \a request, if
        any. */
    [[nodiscard]] static std::optional<QString> typeError(const QJsonObject &request);
    /*! Returns the integer \a key of \a request; \a fallback when it is missing or not a whole
        number. */
    static int intField(const QJsonObject &request, const QString &key, int fallback);
    /*! Returns the parameter \c {ticks} of \a request clamped to 0..maxCommandTicks; \a fallback
        when it is missing. */
    static int ticks(const QJsonObject &request, int fallback);
    /*! Returns the cell the parameters \c {x}, \c {y}, \c {z} of \a request name. */
    static QcBlockPos block(const QJsonObject &request);
    /*! Returns the parameter \c {face} of \a request, \c {up} when omitted; nullopt for an
        unknown name. */
    [[nodiscard]] static std::optional<QcBlockFace> face(const QJsonObject &request);
    /*! Returns the name of \a face in requests ("up", "north", ...). */
    static QString faceName(QcBlockFace face);
};
