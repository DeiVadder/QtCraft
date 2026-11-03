// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/settings/QcKeyControls.h"

#include "clientcore/debug/QcClientLogging.h"
#include "clientcore/settings/QcKeyLabels.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

// Keys that only change others: bound when released alone.
bool isModifier(int key)
{
    static constexpr std::array modifiers{Qt::Key_Shift, Qt::Key_Control, Qt::Key_Meta, Qt::Key_Alt, Qt::Key_AltGr};
    return std::ranges::find(modifiers, key) != modifiers.end();
}

// The action whose key closes a screen besides Esc, in the order of QcKeyControls::ClosingKey.
constexpr std::array<std::optional<QcKeyBindings::Action>, 4> closingActions{
    std::nullopt,
    QcKeyBindings::Action::Inventory,
    QcKeyBindings::Action::Advancements,
    QcKeyBindings::Action::Handbook,
};

} // namespace

QcKeyControls::QcKeyControls(QObject *parent)
    : QObject(parent)
{
}

void QcKeyControls::setBindings(const QcKeyBindings &bindings)
{
    if (m_bindings != bindings) {
        m_bindings = bindings;
        emit changed();
    }
}

QVariantList QcKeyControls::rows() const
{
    QVariantList rows;
    for (const QcKeyBindings::Action action : QcKeyBindings::actions()) {
        rows.append(QVariantMap{{u"action"_s, QcKeyBindings::id(action)},
                                {u"label"_s, QcKeyLabels::action(action)},
                                {u"primary"_s, QcKeyLabels::key(bindings().key(action, 0))},
                                {u"secondary"_s, QcKeyLabels::key(bindings().key(action, 1))},
                                {u"primaryBound"_s, bindings().key(action, 0) != 0},
                                {u"secondaryBound"_s, bindings().key(action, 1) != 0},
                                {u"fixed"_s, QcKeyBindings::isFixed(action)}});
    }
    return rows;
}

QVariantMap QcKeyControls::labels() const
{
    QVariantMap labels;
    for (const QcKeyBindings::Action action : QcKeyBindings::actions()) {
        const QList<int> keys = bindings().keys(action);
        const int first = keys.isEmpty() ? 0 : keys.first();
        // A Mac needs fn for the function keys (F11 for fullscreen is a secondary key there).
        const bool fn = bindings().platform() == QcKeyBindings::Platform::Mac && QcKeyBindings::isFunctionKey(first);
        labels.insert(QcKeyBindings::id(action), fn ? u"fn+"_s + QcKeyLabels::key(first) : QcKeyLabels::key(first));
    }
    return labels;
}

bool QcKeyControls::matches(const QString &action, int key) const
{
    const std::optional<QcKeyBindings::Action> found = QcKeyBindings::actionFromId(action);
    return found && bindings().matches(*found, key);
}

QString QcKeyControls::bind(const QString &action, int slot, int key)
{
    const std::optional<QcKeyBindings::Action> found = QcKeyBindings::actionFromId(action);
    if (!found || QcKeyBindings::isFixed(*found)) {
        return {};
    }

    const bool mac = bindings().platform() == QcKeyBindings::Platform::Mac;
    if (key == QcKeyBindings::clearKey) {
        key = 0;
    } else if (mac && key == Qt::Key_Control) {
        return tr("Cmd cannot be used on a Mac: macOS keeps Cmd+Q, Cmd+H, Cmd+Space and other Cmd shortcuts for itself.");
    } else if (!QcKeyBindings::isBindable(key, bindings().platform())) {
        return tr("This key cannot be used.");
    }

    QcKeyBindings next = bindings();
    const std::optional<QcKeyBindings::Displaced> displaced = next.bind(*found, slot, key);
    if (next == bindings()) {
        return {};
    }

    setBindings(next);
    emit edited();

    QStringList notes;
    if (displaced) {
        const QString other = QcKeyLabels::action(displaced->action);
        notes.append(displaced->key == 0 ? tr("%1 was used for “%2”, which now has no key there.").arg(QcKeyLabels::key(key), other)
                                         : tr("%1 was used for “%2”, which now uses %3.")
                                               .arg(QcKeyLabels::key(key), other, QcKeyLabels::key(displaced->key)));
    }
    if (mac && key == Qt::Key_Meta) {
        notes.append(tr("Careful: on a Mac, Control with the arrow keys or Space switches desktops or the input source."));
    }
    return notes.join(u' ');
}

void QcKeyControls::reset()
{
    setMessage(tr("All keys are back to their defaults."));
    if (bindings() != QcKeyBindings::defaults()) {
        setBindings(QcKeyBindings::defaults());
        emit edited();
    }
}

void QcKeyControls::startCapture(const QString &action, int slot)
{
    const std::optional<QcKeyBindings::Action> found = QcKeyBindings::actionFromId(action);
    if (!found || QcKeyBindings::isFixed(*found) || slot < 0 || slot >= QcKeyBindings::slotCount) {
        qCWarning(lcSettings) << "Key capture: ignoring action" << action << "slot" << slot << "(unknown, fixed or no such slot)";
        return;
    }

    setMessage({});
    setCapture(action, slot);
}

void QcKeyControls::cancelCapture()
{
    setCapture({}, -1);
}

void QcKeyControls::clearCapture()
{
    cancelCapture();
    setMessage({});
}

QcKeyControls::Capture QcKeyControls::keyPressed(int key, bool autoRepeat)
{
    if (!capturing()) {
        return Capture::Ignored;
    }
    if (autoRepeat) {
        return Capture::Waiting;
    }
    if (key == Qt::Key_Escape) {
        cancelCapture();
        return Capture::Cancelled;
    }
    if (isModifier(key)) {
        setPendingModifier(key);
        return Capture::Waiting;
    }
    return bindCaptured(key);
}

QcKeyControls::Capture QcKeyControls::keyReleased(int key, bool autoRepeat)
{
    if (!capturing()) {
        return Capture::Ignored;
    }
    if (autoRepeat || key != pendingModifier()) {
        return Capture::Waiting;
    }
    return bindCaptured(key);
}

QcKeyControls::Capture QcKeyControls::bindCaptured(int key)
{
    const QString action = captureAction();
    const int slot = captureSlot();
    // Ended first: the rows rebuilt by the binding show no waiting slot.
    cancelCapture();
    setMessage(bind(action, slot, key));
    return Capture::Bound;
}

void QcKeyControls::setCapture(const QString &action, int slot)
{
    setPendingModifier(0);
    if (m_captureAction == action && m_captureSlot == slot) {
        return;
    }

    m_captureAction = action;
    m_captureSlot = slot;
    emit captureChanged();
}

void QcKeyControls::setMessage(const QString &message)
{
    if (m_message != message) {
        m_message = message;
        emit messageChanged();
    }
}

QcKeyControls::ScreenKey QcKeyControls::screenKey(int key, bool autoRepeat, ClosingKey closing) const
{
    std::optional<QcKeyBindings::Action> own;
    if (const auto index = static_cast<std::size_t>(closing); index < closingActions.size()) {
        own = closingActions.at(index);
    } else {
        qCWarning(lcSettings) << "screenKey: unknown closing key" << static_cast<int>(closing) << "taken as Esc only";
    }

    if (key == Qt::Key_Escape || (own && !autoRepeat && bindings().matches(*own, key))) {
        return ScreenKey::Close;
    }
    // The handbook does not open over itself.
    if (own != QcKeyBindings::Action::Handbook && bindings().matches(QcKeyBindings::Action::Handbook, key)) {
        return ScreenKey::Handbook;
    }
    return ScreenKey::Unused;
}
