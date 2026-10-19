#include "app/controllers/QcGamepadOptions.h"

#include "clientcore/input/QcGamepadMapping.h"
#include "clientcore/input/QcGamepadSource.h"

#include <QHash>
#include <QVariantMap>

using namespace Qt::StringLiterals;

QcGamepadOptions::QcGamepadOptions(QObject *parent)
    : QObject(parent)
{
}

void QcGamepadOptions::load(const QcGamepadSettings &settings)
{
    m_settings = settings;
    profilesChanged();
}

void QcGamepadOptions::setSource(QcGamepadSource *source)
{
    m_source = source;
}

QStringList QcGamepadOptions::connectedGuids() const
{
    QStringList guids;
    if (m_source) {
        const QList<QcGamepadInfo> pads = m_source->pads();
        for (const QcGamepadInfo &pad : pads) {
            guids.append(pad.guid);
        }
    }
    return guids;
}

void QcGamepadOptions::profilesChanged()
{
    emit controllersChanged();
    emit rowsChanged();
    emit changed();
}

void QcGamepadOptions::setProfileKey(const QString &key)
{
    if (m_key != key) {
        m_key = key;
        profilesChanged();
    }
}

QVariantList QcGamepadOptions::controllers() const
{
    QVariantList result;
    for (const QcGamepadSettings::Controller &controller : m_settings.controllers) {
        result.append(QVariantMap{{u"guid"_s, controller.guid}, {u"name"_s, controller.name}});
    }
    return result;
}

const QcGamepadProfile &QcGamepadOptions::edited() const
{
    return m_settings.profileFor(m_key);
}

void QcGamepadOptions::edit(const std::function<void(QcGamepadProfile &)> &change)
{
    if (!editable()) {
        return;
    }

    QcGamepadProfile profile = edited();
    change(profile);
    profile = profile.sanitized();
    if (profile == edited()) {
        return;
    }
    const bool rebound = profile.bindings != edited().bindings;

    if (m_key.isEmpty()) {
        m_settings.standard = profile;
    } else {
        m_settings.setProfile(m_key, m_settings.controller(m_key)->name, profile, connectedGuids());
    }
    if (rebound) {
        emit rowsChanged();
    }
    emit changed();
}

void QcGamepadOptions::setLookSpeed(double degreesPerSecond)
{
    edit([degreesPerSecond](QcGamepadProfile &profile) { profile.lookSpeed = degreesPerSecond; });
}

void QcGamepadOptions::setInvertLook(bool invert)
{
    edit([invert](QcGamepadProfile &profile) { profile.invertLook = invert; });
}

void QcGamepadOptions::setDeadZone(double share)
{
    edit([share](QcGamepadProfile &profile) { profile.response.deadZone = float(share); });
}

void QcGamepadOptions::setCurve(double exponent)
{
    edit([exponent](QcGamepadProfile &profile) { profile.response.exponent = float(exponent); });
}

void QcGamepadOptions::setTriggerDeadZone(double share)
{
    edit([share](QcGamepadProfile &profile) { profile.response.triggerDeadZone = float(share); });
}

void QcGamepadOptions::setTriggerThreshold(double share)
{
    edit([share](QcGamepadProfile &profile) { profile.triggerThreshold = float(share); });
}

void QcGamepadOptions::setVibration(bool vibrate)
{
    edit([vibrate](QcGamepadProfile &profile) { profile.vibration = vibrate; });
}

void QcGamepadOptions::setRadialOnly(bool only)
{
    if (m_settings.radialOnly != only) {
        m_settings.radialOnly = only;
        emit changed();
    }
}

QVariantList QcGamepadOptions::rows() const
{
    QVariantList result;
    for (std::size_t index = 0; index < QcGamepadMapping::actionCount; ++index) {
        const auto action = QcGamepadMapping::Action(index);
        const std::optional<QcGamepadButton> button = edited().bindings.button(action);
        if (button) {
            const QString name = QcGamepadMapping::actionName(action);
            result.append(QVariantMap{{u"action"_s, name}, {u"label"_s, actionLabel(name)}, {u"button"_s, QcGamepadNames::button(*button)}});
        }
    }
    return result;
}

QString QcGamepadOptions::actionLabel(const QString &action)
{
    static const QHash<QString, const char *> labels{
        {u"jump"_s, QT_TR_NOOP("Jump (twice: fly)")},
        {u"sprint"_s, QT_TR_NOOP("Sprint")},
        {u"sneak"_s, QT_TR_NOOP("Sneak")},
        {u"hotbarPrevious"_s, QT_TR_NOOP("Previous hotbar slot")},
        {u"hotbarNext"_s, QT_TR_NOOP("Next hotbar slot")},
        {u"pause"_s, QT_TR_NOOP("Pause menu")},
        {u"inventory"_s, QT_TR_NOOP("Inventory")},
        {u"chat"_s, QT_TR_NOOP("Chat")},
        {u"handbook"_s, QT_TR_NOOP("Handbook")},
        {u"cameraView"_s, QT_TR_NOOP("Camera view")},
        {u"playerList"_s, QT_TR_NOOP("Player list (hold)")},
        {u"drop"_s, QT_TR_NOOP("Drop (hold: stack)")},
        {u"pickBlock"_s, QT_TR_NOOP("Pick block")},
        {u"hotbarRadial"_s, QT_TR_NOOP("Radial menu (hold)")},
    };
    const char *label = labels.value(action);
    return label ? tr(label) : QString();
}

QString QcGamepadOptions::bind(const QString &action, const QString &button)
{
    const std::optional<QcGamepadMapping::Action> parsedAction = QcGamepadMapping::actionFromName(action);
    const std::optional<QcGamepadButton> parsedButton = QcGamepadNames::buttonFromName(button);
    if (!parsedAction || !parsedButton || !editable()) {
        return {};
    }

    std::optional<QcGamepadMapping::Action> displaced;
    edit([&](QcGamepadProfile &profile) { displaced = profile.bindings.bind(*parsedAction, *parsedButton); });
    if (!displaced) {
        return {};
    }
    return tr("“%1” now has the button of “%2”.").arg(actionLabel(QcGamepadMapping::actionName(*displaced)), actionLabel(action));
}

void QcGamepadOptions::resetBindings()
{
    edit([](QcGamepadProfile &profile) { profile.bindings = {}; });
}

void QcGamepadOptions::resetProfile()
{
    edit([](QcGamepadProfile &profile) { profile = {}; });
}

void QcGamepadOptions::makeOwnProfile(const QString &guid, const QString &name)
{
    if (guid.isEmpty()) {
        return;
    }
    if (!m_settings.controller(guid)) {
        m_settings.setProfile(guid, name, m_settings.standard, connectedGuids());
    }
    m_key = guid;
    profilesChanged();
}

void QcGamepadOptions::removeOwnProfile()
{
    if (ownProfile()) {
        m_settings.removeProfile(m_key);
        if (!connectedGuids().contains(m_key)) {
            m_key.clear();
        }
        profilesChanged();
    }
}

void QcGamepadOptions::editProfileOf(const QString &guid)
{
    setProfileKey(m_settings.controller(guid) ? guid : QString());
}
