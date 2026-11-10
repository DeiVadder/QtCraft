// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/inventory/QcBeaconController.h"

#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "app/settings/QcSettingsController.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcBeacon.h"

#include <QColor>
#include <QVariantMap>

using namespace Qt::StringLiterals;

QcBeaconController::QcBeaconController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
{
}

bool QcBeaconController::isOpen() const
{
    return m_open;
}

int QcBeaconController::maxLevel()
{
    return QcBeacon::maxLevel;
}

QString QcBeaconController::paymentItems() const
{
    const auto content = m_parts.session ? m_parts.session->content() : nullptr;
    if (!content) {
        return {};
    }
    QStringList names;
    for (qsizetype index = 1; index <= content->itemCount(); ++index) {
        const auto id = static_cast<QcItemId>(index);
        if (QcBeacon::isPayment(*content, id)) {
            names.append(content->translations().translate(content->item(id).translationKey, m_settings.contentLanguage()));
        }
    }
    return names.join(u", "_s);
}

QVariantMap QcBeaconController::optionOf(const QString &effect, bool available) const
{
    const auto content = m_parts.session ? m_parts.session->content() : nullptr;
    const QcEffectDefinition *definition = content ? content->effect(effect) : nullptr;
    if (!definition) {
        return {};
    }
    return {{u"name"_s, effect},
            {u"label"_s, content->translations().translate(definition->translationKey, m_settings.contentLanguage())},
            {u"icon"_s, definition->icon},
            {u"colour"_s, QColor(static_cast<QRgb>(definition->colour)).name()},
            {u"tier"_s, QcBeacon::levelOf(effect)},
            {u"available"_s, available}};
}

QVariantList QcBeaconController::primaryOptions() const
{
    QVariantList options;
    const QStringList all = QcBeacon::primaryEffects(QcBeacon::maxLevel);
    for (const QString &effect : all) {
        const QVariantMap option = optionOf(effect, QcBeacon::levelOf(effect) <= level());
        if (!option.isEmpty()) {
            options.append(option);
        }
    }
    return options;
}

QVariantList QcBeaconController::secondaryOptions() const
{
    QVariantList options;
    const bool available = level() >= QcBeacon::secondaryLevel;
    QVariantMap regeneration = optionOf(QcBeacon::secondaryEffect(), available);
    if (!regeneration.isEmpty()) {
        regeneration.insert(u"levelTwo"_s, false);
        options.append(regeneration);
    }
    // The primary effect again, at level II.
    QVariantMap again = optionOf(primary(), available);
    if (!again.isEmpty()) {
        again.insert(u"levelTwo"_s, true);
        options.append(again);
    }
    return options;
}

bool QcBeaconController::canConfirm() const
{
    return isOpen() && hasPayment() && QcBeacon::mayChoose(level(), primary(), secondary());
}

void QcBeaconController::choosePrimary(const QString &effect)
{
    if (!QcBeacon::primaryEffects(level()).contains(effect)) {
        qCWarning(lcGame) << "Beacon: primary effect" << effect << "refused, not offered at level" << level();
        return;
    }
    // A secondary effect that was the old primary one at level II follows it.
    const bool followed = !secondary().isEmpty() && secondary() == primary();
    setChoice(effect, followed ? effect : secondary());
}

void QcBeaconController::chooseSecondary(const QString &effect)
{
    if (!effect.isEmpty() && !QcBeacon::mayChoose(level(), primary(), effect)) {
        qCWarning(lcGame) << "Beacon: secondary effect" << effect << "refused at level" << level() << "with primary" << primary();
        return;
    }
    setChoice(primary(), effect);
}

void QcBeaconController::confirm() const
{
    if (!canConfirm()) {
        qCWarning(lcGame) << "Beacon: nothing to confirm (open" << isOpen() << "payment" << hasPayment() << "effects" << primary() << secondary() << ")";
        return;
    }
    m_parts.session->beacon().choose(primary(), secondary());
}

void QcBeaconController::setChoice(const QString &primary, const QString &secondary)
{
    if (primary == m_primary && secondary == m_secondary) {
        return;
    }
    const bool primaryChanged = primary != m_primary;
    m_primary = primary;
    m_secondary = secondary;
    emit choiceChanged();
    // The secondary options offer the primary effect at level II.
    if (primaryChanged) {
        emit optionsChanged();
    }
    emit changed();
}

void QcBeaconController::opened()
{
    // The server sends the beacon's state right after the opening; until then the screen shows
    // the beacon's state as last known.
    m_awaitingState = true;
    refresh(true);
}

void QcBeaconController::update()
{
    // The first state after an opening (or a closing) brings the beacon's own effects; later
    // states (the payment moving) keep what the player chose on the screen.
    const bool take = m_awaitingState || !(m_parts.session && m_parts.session->beacon().isOpen());
    m_awaitingState = false;
    refresh(take);
}

void QcBeaconController::refresh(bool takeChoice)
{
    const bool open = m_parts.session && m_parts.session->beacon().isOpen();
    const QcMsg::BeaconState state = open ? m_parts.session->beacon().state() : QcMsg::BeaconState{};
    const bool levelChanged = state.level != m_level;
    const bool stateChanged = levelChanged || open != m_open || state.payment.isEmpty() == m_hasPayment;
    m_open = open;
    m_level = state.level;
    m_hasPayment = !state.payment.isEmpty();
    if (levelChanged) {
        emit optionsChanged();
    }
    if (takeChoice) {
        setChoice(state.primary, state.secondary);
    }
    if (stateChanged) {
        emit changed();
    }
}
