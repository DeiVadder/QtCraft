// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcVirtualGamepadSource.h"

#include <QJsonObject>
#include <QString>

#include <optional>

class QcControlCommandTable;
class QcControlContext;

// Control commands for virtual gamepads (decision E-540): connect and disconnect pads and set
// their buttons and axes, for scenario tests with a controller. The pads live in a
// QcVirtualGamepadSource owned here; the input mapping of the client reads it like the SDL3
// backend. The commands do not touch the session.
class QcControlGamepadCommands
{
public:
    explicit QcControlGamepadCommands(QcControlContext &context);

    void registerCommands(QcControlCommandTable &table);
    QcVirtualGamepadSource &source() { return m_source; }

private:
    void gamepads(const QJsonObject &request);
    void gamepadConnect(const QJsonObject &request);
    void gamepadDisconnect(const QJsonObject &request);
    void gamepadSet(const QJsonObject &request);
    // Applies `buttons` ({"south": true}) and `axes` ({"leftX": 0.5}) of the request to a state;
    // the error for an unknown name or a wrong value type.
    static std::optional<QString> applyChanges(const QJsonObject &request, QcGamepadState &state);
    QJsonObject padJson(const QcGamepadInfo &pad) const;

    QcControlContext &m_context;
    QcVirtualGamepadSource m_source;
};
