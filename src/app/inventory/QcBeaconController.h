// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// The beacon's screen for QML (GameController.beacon, decision E-1142): the beacon's level, the
// effects it offers by tier with the ones chosen on the screen, whether the payment slot holds an
// item, and confirm(). The payment slot itself is a row of the inventory model
// (GameController.player.inventory); the server checks the choice and sends everything back.
class QcBeaconController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool open READ isOpen NOTIFY changed FINAL)
    // The level of the pyramid under the beacon, 0 while it does not shine.
    Q_PROPERTY(int level READ level NOTIFY changed FINAL)
    // Whether the payment slot holds an item.
    Q_PROPERTY(bool hasPayment READ hasPayment NOTIFY changed FINAL)
    // The highest level of a pyramid.
    Q_PROPERTY(int maxLevel READ maxLevel CONSTANT FINAL)
    // The items that pay, by their names in the content's language ("Diamond, Emerald, Gold ingot, ...").
    Q_PROPERTY(QString paymentItems READ paymentItems NOTIFY changed FINAL)
    // The primary effects by tier: {name, label, icon, colour, tier, available}; they change with
    // the level only.
    Q_PROPERTY(QVariantList primaryOptions READ primaryOptions NOTIFY optionsChanged FINAL)
    // The secondary effects: regeneration and the chosen primary one at level II, with "levelTwo";
    // they change with the level and the chosen primary effect.
    Q_PROPERTY(QVariantList secondaryOptions READ secondaryOptions NOTIFY optionsChanged FINAL)
    // The effects chosen on the screen (empty: none), at first the beacon's own.
    Q_PROPERTY(QString primary READ primary NOTIFY choiceChanged FINAL)
    Q_PROPERTY(QString secondary READ secondary NOTIFY choiceChanged FINAL)
    // Whether confirm() would send: the screen open, a payment, and a primary effect (with the
    // secondary one) the beacon's level allows.
    Q_PROPERTY(bool canConfirm READ canConfirm NOTIFY changed FINAL)
    // The handbook page of the screen's block (the screen's handbook button).
    Q_PROPERTY(QString handbookTarget READ handbookTarget CONSTANT FINAL)

public:
    QcBeaconController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    bool isOpen() const;
    int level() const { return m_level; }
    bool hasPayment() const { return m_hasPayment; }
    QVariantList primaryOptions() const;
    QVariantList secondaryOptions() const;
    QString primary() const { return m_primary; }
    QString secondary() const { return m_secondary; }
    bool canConfirm() const;
    static int maxLevel();
    QString paymentItems() const;
    static QString handbookTarget() { return QStringLiteral("qtcraft:beacon"); }

    // Chooses `effect` as the primary effect; one the beacon's level does not offer is refused.
    Q_INVOKABLE void choosePrimary(const QString &effect);
    // Chooses `effect` as the secondary effect (empty: none); refused below level 4 or when it is
    // neither regeneration nor the primary effect.
    Q_INVOKABLE void chooseSecondary(const QString &effect);
    // Pays for the chosen effects.
    Q_INVOKABLE void confirm() const;
    // A beacon's screen opened: the choice follows the first state the server sends for it.
    void opened();
    // The screen's state changed or it closed.
    void update();

signals:
    // Open, level, payment, payment items or canConfirm changed.
    void changed();
    void optionsChanged();
    void choiceChanged();

private:
    // One option of an effect for QML.
    QVariantMap optionOf(const QString &effect, bool available) const;
    void setChoice(const QString &primary, const QString &secondary);
    // Reads the screen's state; `takeChoice` makes the beacon's own effects the choice.
    void refresh(bool takeChoice);

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    int m_level = 0;
    bool m_hasPayment = false;
    QString m_primary;
    QString m_secondary;
    bool m_open = false;
    // Whether the screen opened and its first state has not come yet.
    bool m_awaitingState = false;
};
