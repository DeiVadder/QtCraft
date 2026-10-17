#pragma once

#include "common/gameplay/QcKeyBindings.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// The player's key table for QML: the rows of the controls page, the key texts of hints, key tests
// for screens and rebinding. The game controller loads and stores it with the settings; QML knows
// it as KeyControls (QcKeyControlsForeign).
class QcKeyControls : public QObject
{
    Q_OBJECT

    // [{action, label, primary, secondary, fixed}] with the keys as display texts.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed FINAL)
    // Action id -> text of its primary key, e.g. {"cameraView": "V"}.
    Q_PROPERTY(QVariantMap labels READ labels NOTIFY changed FINAL)

public:
    explicit QcKeyControls(QObject *parent = nullptr);

    const QcKeyBindings &bindings() const { return m_bindings; }
    // Takes keys loaded from the settings; does not ask for storing.
    void setBindings(const QcKeyBindings &bindings);
    QVariantList rows() const;
    QVariantMap labels() const;

    // Whether `key` (a Qt::Key) triggers the action with this id ("handbook", "inventory", ...).
    Q_INVOKABLE bool matches(const QString &action, int key) const;
    // Binds a key to a slot (0 primary, 1 secondary); Backspace clears the slot. Returns a text
    // describing a refused key, a resolved conflict or a Mac caveat, else "". `edited` follows
    // once for every change.
    Q_INVOKABLE QString bind(const QString &action, int slot, int key);
    Q_INVOKABLE void reset();
    // The texts follow the UI language.
    void retranslate() { emit changed(); }

signals:
    void changed();
    // The player changed a key; the settings should be stored.
    void edited();

private:
    QcKeyBindings m_bindings = QcKeyBindings::defaults();
};
