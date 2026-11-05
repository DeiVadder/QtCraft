// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcKeyBindings.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

/*! \brief The player's key table for QML: the rows of the controls page, the key texts of hints,
    key tests for screens and rebinding.

    The controls page captures a key for a slot here: startCapture() waits, keyPressed() and
    keyReleased() take the keys (Esc cancels; a modifier is bound when it is released without
    another key, so Shift+7 binds the character it types), and \c message says what the last
    change did. The game controller loads and stores it with the settings; QML knows it as
    KeyControls (QcKeyControlsForeign). */
class QcKeyControls : public QObject
{
    Q_OBJECT

    /*! [{action, label, primary, secondary, primaryBound, secondaryBound, fixed}] with the keys as
        display texts and whether each slot has a key. */
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed FINAL)
    /*! Action id -> text of its primary key, e.g. {"cameraView": "V"}. */
    Q_PROPERTY(QVariantMap labels READ labels NOTIFY changed FINAL)
    /*! A slot waits for a key (startCapture()). */
    Q_PROPERTY(bool capturing READ capturing NOTIFY captureChanged FINAL)
    /*! The id of the action whose slot waits for a key; empty without a capture. */
    Q_PROPERTY(QString captureAction READ captureAction NOTIFY captureChanged FINAL)
    /*! The slot that waits for a key: 0 primary, 1 secondary, -1 without a capture. */
    Q_PROPERTY(int captureSlot READ captureSlot NOTIFY captureChanged FINAL)
    /*! What the last change did: a refused key, a resolved conflict, a Mac caveat or the reset;
        empty when it needs no words. */
    Q_PROPERTY(QString message READ message NOTIFY messageChanged FINAL)

public:
    /*! What a key did to a capture. */
    enum class Capture {
        Ignored,   // no slot waits for a key
        Waiting,   // the slot still waits (a modifier held, a repeated key, another key released)
        Cancelled, // Esc ended the capture without a key
        Bound      // the key was bound (or refused, see message); the capture ended
    };
    Q_ENUM(Capture)
    /*! What a key pressed on a screen does (screenKey()). */
    enum class ScreenKey {
        Unused,  // nothing of the screens' common rule; the screen may use the key itself
        Close,   // the screen closes
        Handbook // the handbook opens
    };
    Q_ENUM(ScreenKey)
    /*! The key that closes a screen besides Esc, as the key that opened it from the world. */
    enum class ClosingKey {
        EscOnly,         // only Esc (the pause menu, the settings, the main menu)
        InventoryKey,    // the inventory key (the screens with item slots)
        AdvancementsKey, // the advancements key (the advancements)
        HandbookKey      // the handbook key (the handbook)
    };
    Q_ENUM(ClosingKey)

    explicit QcKeyControls(QObject *parent = nullptr);

    /*! Returns the key table. */
    const QcKeyBindings &bindings() const { return m_bindings; }
    /*! Takes the keys \a bindings loaded from the settings; does not ask for storing. */
    void setBindings(const QcKeyBindings &bindings);
    QVariantList rows() const;
    /*! Returns the first key of every action by its id, as the HUD names it; a function key on a
        Mac with "fn+". */
    QVariantMap labels() const;

    /*! Returns whether \a key (a Qt::Key) triggers the action with the id \a action ("handbook",
        "inventory", ...). */
    Q_INVOKABLE bool matches(const QString &action, int key) const;
    /*! Binds \a key (a Qt::Key) to \a slot (0 primary, 1 secondary) of the action with the id
        \a action; Backspace clears the slot. Returns a text describing a refused key, a resolved
        conflict or a Mac caveat, else "". \c {edited} follows once for every change. */
    Q_INVOKABLE QString bind(const QString &action, int slot, int key);
    /*! Gives every action its default keys again; \c {edited} follows when that changed anything.
     */
    Q_INVOKABLE void reset();
    /*! The texts follow the UI language. */
    void retranslate() { emit changed(); }

    /*! Returns whether a slot waits for a key. */
    bool capturing() const { return m_captureSlot >= 0; }
    /*! Returns the id of the action whose slot waits for a key. */
    QString captureAction() const { return m_captureAction; }
    /*! Returns the slot that waits for a key, -1 without a capture. */
    int captureSlot() const { return m_captureSlot; }
    /*! Returns what the last change did. */
    QString message() const { return m_message; }
    /*! Waits for a key for \a slot (0 primary, 1 secondary) of the action with the id \a action
        and clears \c message. An unknown or fixed action or another slot is ignored with a
        warning. */
    Q_INVOKABLE void startCapture(const QString &action, int slot);
    /*! Ends a capture without a key. */
    Q_INVOKABLE void cancelCapture();
    /*! Ends a capture and clears \c message (the settings open). */
    Q_INVOKABLE void clearCapture();
    /*! Takes \a key (a Qt::Key) pressed during a capture, \a autoRepeat when the system repeats
        it. Returns what it did. */
    Q_INVOKABLE QcKeyControls::Capture keyPressed(int key, bool autoRepeat);
    /*! Takes \a key (a Qt::Key) released during a capture, \a autoRepeat when the system repeats
        it: binds a modifier pressed alone. Returns what it did. */
    Q_INVOKABLE QcKeyControls::Capture keyReleased(int key, bool autoRepeat);
    /*! Returns what \a key (a Qt::Key) does on a screen whose own key is \a closing, the common
        rule of the screens as the world's keys have theirs in the game input: Esc closes the
        screen, and so does the closing key unless \a autoRepeat (it is still held from opening
        the screen); the handbook key opens the handbook on any other screen. An unknown
        \a closing is taken as EscOnly with a warning. */
    Q_INVOKABLE QcKeyControls::ScreenKey screenKey(int key, bool autoRepeat, QcKeyControls::ClosingKey closing) const;

signals:
    void changed();
    /*! The player changed a key; the settings should be stored. */
    void edited();
    /*! A capture started or ended. */
    void captureChanged();
    /*! \c message changed. */
    void messageChanged();

private:
    // A capture of `slot` of `action` starts (-1: none) and forgets a pending modifier.
    void setCapture(const QString &action, int slot);
    void setMessage(const QString &message);
    int pendingModifier() const { return m_pendingModifier; }
    void setPendingModifier(int key) { m_pendingModifier = key; }
    // Binds the captured slot to `key` and ends the capture.
    Capture bindCaptured(int key);

    QcKeyBindings m_bindings = QcKeyBindings::defaults();
    QString m_captureAction;
    // NOLINT(qc-member-access): written only by setCapture(), together with m_captureAction.
    int m_captureSlot = -1;
    // A modifier pressed during the capture (0: none); bound when released alone.
    int m_pendingModifier = 0;
    QString m_message;
};
