// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <expected>

/*! Result of an operation that can fail at a system boundary; the error is a human-readable
    message.
    \typealias QcResult
    \relates <common/core/QcResult.h> */
template<typename T>
using QcResult = std::expected<T, QString>;

/*! Result of an operation without a value that can fail; the error is a human-readable message. */
using QcStatus = std::expected<void, QString>;

/*! Returns the error \a message, to return from a function with a QcResult or QcStatus. */
inline std::unexpected<QString> qcError(const QString &message)
{
    return std::unexpected<QString>(message);
}
