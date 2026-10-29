// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QcContentRegistry;

// The stonecutter screen the server opened for the local player (decision E-785): its input slot and
// the stonecutting recipe chosen for it (decision E-784) as the server last sent them, and the
// choices the player makes. Items go in and out through the inventory's slot messages (input and
// output are the machine slots while the screen is open). Part of QcClientSession; GUI-free.
class QcClientStonecutter : public QObject
{
    Q_OBJECT

public:
    using Sender = std::function<void(const QcMessage &)>;

    void setSender(Sender sender) { m_send = std::move(sender); }
    // Takes over OpenStonecutter, StonecutterState and CloseStonecutter; false for other messages.
    bool handle(const QcMessage &message);

    bool isOpen() const { return m_open; }
    QcBlockPos pos() const { return m_pos; }
    QcItemStack input() const { return m_input; }
    // The chosen recipe's name; empty for none.
    const QString &recipe() const { return m_recipe; }
    // Input, an unused middle slot and output, laid out like a machine's slots for the inventory
    // model (the output follows from input and recipe, QcStonecutting::output).
    [[nodiscard]] QList<QcItemStack> slotStacks(const QcContentRegistry &content) const;

    // Asks the server to choose the recipe `recipe` (empty: none); false when no screen is open.
    bool select(const QString &recipe);
    // The player closed the screen; the server gives the input back.
    void close();

signals:
    void opened();
    // The input or the chosen recipe changed.
    void changed();
    void closed();

private:
    Sender m_send;
    bool m_open = false;
    QcBlockPos m_pos;
    QcItemStack m_input;
    QString m_recipe;
};
