#pragma once

#include <QString>

#include <expected>

// Result of an operation that can fail at a system boundary; the error is a human-readable message.
template<typename T>
using QcResult = std::expected<T, QString>;

using QcStatus = std::expected<void, QString>;

inline std::unexpected<QString> qcError(const QString &message)
{
    return std::unexpected<QString>(message);
}
