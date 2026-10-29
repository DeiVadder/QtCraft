// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/control/QcControlGamepadCommands.h"

#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/session/QcClientSession.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

QcControlGamepadCommands::QcControlGamepadCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlGamepadCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"gamepads"_s, [this](const QJsonObject &request) { gamepads(request); });
    table.add(u"gamepadConnect"_s, [this](const QJsonObject &request) { gamepadConnect(request); });
    table.add(u"gamepadDisconnect"_s, [this](const QJsonObject &request) { gamepadDisconnect(request); });
    table.add(u"gamepadSet"_s, [this](const QJsonObject &request) { gamepadSet(request); });
}

QJsonObject QcControlGamepadCommands::padJson(const QcGamepadInfo &pad) const
{
    const QcGamepadState state = m_source.state(pad.id);
    QJsonArray pressed;
    for (std::size_t i = 0; i < qcGamepadButtonCount; ++i) {
        if (state.pressed(static_cast<QcGamepadButton>(i))) {
            pressed.append(QcGamepadNames::button(static_cast<QcGamepadButton>(i)));
        }
    }

    QJsonObject axes;
    for (std::size_t i = 0; i < qcGamepadAxisCount; ++i) {
        axes.insert(QcGamepadNames::axis(static_cast<QcGamepadAxis>(i)), static_cast<double>(state.axis(static_cast<QcGamepadAxis>(i))));
    }
    return {{u"pad"_s, pad.id},
            {u"name"_s, pad.name},
            {u"type"_s, QcGamepadNames::type(pad.type)},
            {u"guid"_s, pad.guid},
            {u"pressed"_s, pressed},
            {u"axes"_s, axes}};
}

void QcControlGamepadCommands::gamepads(const QJsonObject &request)
{
    QJsonArray pads;
    const QList<QcGamepadInfo> connected = m_source.pads();
    for (const QcGamepadInfo &pad : connected) {
        pads.append(padJson(pad));
    }
    m_context.finish(request, {{u"backend"_s, m_source.backendName()}, {u"pads"_s, pads}});
}

void QcControlGamepadCommands::gamepadConnect(const QJsonObject &request)
{
    const QString typeName = request.value(u"type"_s).toString(u"xbox"_s);
    const std::optional<QcGamepadType> type = QcGamepadNames::typeFromName(typeName);
    if (!type) {
        m_context.fail(request, u"Unknown gamepad type '%1'"_s.arg(typeName));
        return;
    }

    const QString name = request.value(u"name"_s).toString(u"Virtual %1"_s.arg(typeName));
    const int id = m_source.connectPad(name, *type, request.value(u"guid"_s).toString());
    m_context.finish(request, padJson(m_source.info(id).value_or(QcGamepadInfo{.id = id, .name = name, .type = *type, .guid = {}, .rumble = true})));
}

void QcControlGamepadCommands::gamepadDisconnect(const QJsonObject &request)
{
    const int id = request.value(u"pad"_s).toInt(-1);
    if (!m_source.disconnectPad(id)) {
        m_context.fail(request, u"No virtual gamepad %1"_s.arg(id));
        return;
    }
    m_context.finish(request, {{u"pad"_s, id}});
}

std::optional<QString> QcControlGamepadCommands::applyChanges(const QJsonObject &request, QcGamepadState &state)
{
    // Both members are optional, but when present they must be objects of name and value.
    const QJsonValue buttonsValue = request.value(u"buttons"_s);
    const QJsonValue axesValue = request.value(u"axes"_s);
    if (!buttonsValue.isUndefined() && !buttonsValue.isObject()) {
        return u"'buttons' must be an object of button names and true/false"_s;
    }
    if (!axesValue.isUndefined() && !axesValue.isObject()) {
        return u"'axes' must be an object of axis names and numbers"_s;
    }

    const QJsonObject buttons = buttonsValue.toObject();
    for (auto it = buttons.begin(); it != buttons.end(); ++it) {
        const std::optional<QcGamepadButton> button = QcGamepadNames::buttonFromName(it.key());
        if (!button || !it.value().isBool()) {
            return u"Button '%1' unknown or not true/false"_s.arg(it.key());
        }
        state.setPressed(*button, it.value().toBool());
    }

    const QJsonObject axes = axesValue.toObject();
    for (auto it = axes.begin(); it != axes.end(); ++it) {
        const std::optional<QcGamepadAxis> axis = QcGamepadNames::axisFromName(it.key());
        if (!axis || !it.value().isDouble()) {
            return u"Axis '%1' unknown or not a number"_s.arg(it.key());
        }
        state.setAxis(*axis, static_cast<float>(it.value().toDouble()));
    }
    return std::nullopt;
}

void QcControlGamepadCommands::gamepadSet(const QJsonObject &request)
{
    const int id = request.value(u"pad"_s).toInt(-1);
    if (!m_source.contains(id)) {
        m_context.fail(request, u"No virtual gamepad %1"_s.arg(id));
        return;
    }

    // `release: true` starts from a released, centred pad instead of the current state.
    QcGamepadState state = request.value(u"release"_s).toBool() ? QcGamepadState{} : m_source.state(id);
    if (const std::optional<QString> error = applyChanges(request, state)) {
        m_context.fail(request, *error);
        return;
    }

    m_source.setState(id, state);
    const QList<QcGamepadInfo> connected = m_source.pads();
    for (const QcGamepadInfo &pad : connected) {
        if (pad.id == id) {
            m_context.finish(request, padJson(pad));
            return;
        }
    }

    // Every request gets an answer, even if the pad vanished while it was set.
    m_context.fail(request, u"No virtual gamepad %1"_s.arg(id));
}
