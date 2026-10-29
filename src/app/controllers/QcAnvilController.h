// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

struct QcSessionParts;

// The anvil's screen for QML (GameController.anvil, decision E-866): the name in the field as the
// server knows it, a count that tells the screen to take the left stack's name into its field (the
// field belongs to the screen while the player types; renames go one way), the output's cost and whether the player can take it, rename() and forge(). The
// left, right and output slots themselves are rows of the inventory model
// (GameController.player.inventory); the server decides and sends everything back.
class QcAnvilController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool open READ isOpen NOTIFY changed FINAL)
    // The text of the name field as the server last confirmed it.
    Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
    // The custom name of the stack in the left slot; empty for none.
    Q_PROPERTY(QString leftName READ leftName NOTIFY changed FINAL)
    // Rises when the screen opens and whenever another stack lies in the left slot: the name field
    // then shows leftName. Server echoes of the typed text never change it.
    Q_PROPERTY(int nameResets READ nameResets NOTIFY nameResetsChanged FINAL)
    // Whether there is an output to take.
    Q_PROPERTY(bool hasOutput READ hasOutput NOTIFY changed FINAL)
    // Levels the output costs (0 without one).
    Q_PROPERTY(int levelCost READ levelCost NOTIFY changed FINAL)
    // Whether the output costs too much (QcAnvil::tooExpensive) outside creative mode.
    Q_PROPERTY(bool tooExpensive READ isTooExpensive NOTIFY changed FINAL)
    // Whether the player can take the output now.
    Q_PROPERTY(bool affordable READ isAffordable NOTIFY changed FINAL)
    // The longest custom name (characters).
    Q_PROPERTY(int maxNameLength READ maxNameLength CONSTANT FINAL)

public:
    explicit QcAnvilController(const QcSessionParts &parts, QObject *parent = nullptr);

    bool isOpen() const;
    QString name() const { return m_name; }
    QString leftName() const { return m_leftName; }
    int nameResets() const { return m_nameResets; }
    bool hasOutput() const { return m_hasOutput; }
    int levelCost() const { return m_levelCost; }
    bool isTooExpensive() const { return m_tooExpensive; }
    bool isAffordable() const { return m_affordable; }
    static int maxNameLength();

    // Sends the text of the name field (the output follows from the server).
    Q_INVOKABLE void rename(const QString &text) const;
    // Takes the output into the left slot.
    Q_INVOKABLE void forge() const;
    // The screen, the player's level or the game mode changed.
    void update();

signals:
    void changed();
    void nameResetsChanged();

private:
    const QcSessionParts &m_parts;
    QString m_name;
    QString m_leftName;
    // The left stack the field last followed, and whether the screen was open then.
    QcItemStack m_left;
    bool m_wasOpen = false;
    int m_nameResets = 0;
    bool m_hasOutput = false;
    int m_levelCost = 0;
    bool m_tooExpensive = false;
    bool m_affordable = false;
};
