#pragma once

#include "QcBlockFace.h"
#include "QcBlockPos.h"

#include <QJsonObject>
#include <QString>

#include <optional>

// Typed access to the parameters of a control request.
class QcControlArguments
{
public:
    // Longest wait a tick-based command accepts.
    static constexpr int maxCommandTicks = 20 * 600;

    // Error message for the first parameter of the wrong JSON type, if any.
    static std::optional<QString> typeError(const QJsonObject &request);
    static int intField(const QJsonObject &request, const QString &key, int fallback);
    // `ticks`, clamped to 0..maxCommandTicks.
    static int ticks(const QJsonObject &request, int fallback);
    // `x`, `y`, `z`.
    static QcBlockPos block(const QJsonObject &request);
    // `face`, `up` when omitted; nullopt for an unknown name.
    static std::optional<QcBlockFace> face(const QJsonObject &request);
    static QString faceName(QcBlockFace face);
};
