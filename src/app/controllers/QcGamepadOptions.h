// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/settings/QcGamepadSettings.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class QcGamepadSource;

// The gamepad page of the settings (GameController.settings.gamepad, decisions E-610 and E-611):
// edits one profile at a time, the one all pads use (`profileKey` empty) or that of a pad model
// (`profileKey` its GUID). A model without a profile of its own uses the standard one; the page
// shows its values read-only until makeOwnProfile() copies them. The buttons of the world actions
// are rebound per profile (bind(): a button used by another action is swapped). `radialOnly` is
// one switch for all pads. Stored with the other settings when the settings screen closes.
class QcGamepadOptions : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GamepadOptions)
    QML_UNCREATABLE("Provided by GameController.settings.gamepad")
    Q_PROPERTY(QString profileKey READ profileKey WRITE setProfileKey NOTIFY changed FINAL)
    // The edited profile is the standard one or the model's own (else the model uses the standard one).
    Q_PROPERTY(bool editable READ editable NOTIFY changed FINAL)
    Q_PROPERTY(bool ownProfile READ ownProfile NOTIFY changed FINAL)
    // The models with a profile of their own: {guid, name}.
    Q_PROPERTY(QVariantList controllers READ controllers NOTIFY controllersChanged FINAL)
    Q_PROPERTY(double lookSpeed READ lookSpeed WRITE setLookSpeed NOTIFY changed FINAL)
    Q_PROPERTY(bool invertLook READ invertLook WRITE setInvertLook NOTIFY changed FINAL)
    Q_PROPERTY(double deadZone READ deadZone WRITE setDeadZone NOTIFY changed FINAL)
    Q_PROPERTY(double curve READ curve WRITE setCurve NOTIFY changed FINAL)
    Q_PROPERTY(double triggerDeadZone READ triggerDeadZone WRITE setTriggerDeadZone NOTIFY changed FINAL)
    Q_PROPERTY(double triggerThreshold READ triggerThreshold WRITE setTriggerThreshold NOTIFY changed FINAL)
    Q_PROPERTY(bool vibration READ vibration WRITE setVibration NOTIFY changed FINAL)
    Q_PROPERTY(bool radialOnly READ radialOnly WRITE setRadialOnly NOTIFY changed FINAL)
    // The rebindable actions of the edited profile: {action, label, button} (button as in
    // GameController.gamepad.actions).
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged FINAL)

public:
    explicit QcGamepadOptions(QObject *parent = nullptr);

    const QcGamepadSettings &settings() const { return m_settings; }
    void load(const QcGamepadSettings &settings);
    // The connected pads (nullptr: none): their models' profiles are never evicted, and a removed
    // profile of a model that is not connected leaves the page on the standard one.
    void setSource(QcGamepadSource *source);

    QString profileKey() const { return m_key; }
    void setProfileKey(const QString &key);
    bool editable() const { return m_key.isEmpty() || ownProfile(); }
    bool ownProfile() const { return m_settings.controller(m_key) != nullptr; }
    QVariantList controllers() const;
    double lookSpeed() const { return edited().lookSpeed; }
    void setLookSpeed(double degreesPerSecond);
    bool invertLook() const { return edited().invertLook; }
    void setInvertLook(bool invert);
    double deadZone() const { return edited().response.deadZone; }
    void setDeadZone(double share);
    double curve() const { return edited().response.exponent; }
    void setCurve(double exponent);
    double triggerDeadZone() const { return edited().response.triggerDeadZone; }
    void setTriggerDeadZone(double share);
    double triggerThreshold() const { return edited().triggerThreshold; }
    void setTriggerThreshold(double share);
    bool vibration() const { return edited().vibration; }
    void setVibration(bool vibrate);
    bool radialOnly() const { return m_settings.radialOnly; }
    void setRadialOnly(bool only);
    QVariantList rows() const;

    // The action's name for the player ("Jump", "Open the inventory"); empty for an unknown id.
    static QString actionLabel(const QString &action);
    // Gives `action` the button (names as in the control interface). Returns what moved: empty, or
    // a message naming the action that now has the action's old button.
    Q_INVOKABLE QString bind(const QString &action, const QString &button);
    // The edited profile's buttons back to the defaults.
    Q_INVOKABLE void resetBindings();
    // The edited profile's values and buttons back to the defaults.
    Q_INVOKABLE void resetProfile();
    // The model `guid` gets a profile of its own, copied from the standard one, and is edited.
    Q_INVOKABLE void makeOwnProfile(const QString &guid, const QString &name);
    // The edited model uses the standard profile again; when the model is not connected, it is no
    // longer offered and the standard profile is edited.
    Q_INVOKABLE void removeOwnProfile();
    // The page opens: the pad in use (`guid`, its model) is edited when its model has a profile of
    // its own, else the standard one.
    Q_INVOKABLE void editProfileOf(const QString &guid);

signals:
    // Anything changed (the game applies the settings).
    void changed();
    // The list of models with a profile of their own changed.
    void controllersChanged();
    // The edited profile's buttons changed (or another profile is edited).
    void rowsChanged();

private:
    const QcGamepadProfile &edited() const;
    // Applies `change` to the edited profile when it is editable, clamps it and tells.
    void edit(const std::function<void(QcGamepadProfile &)> &change);
    // Another profile is edited, or the list of profiles changed.
    void profilesChanged();
    QStringList connectedGuids() const;

    QcGamepadSettings m_settings;
    QString m_key;
    QPointer<QcGamepadSource> m_source;
};
