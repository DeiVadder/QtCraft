#pragma once

#include "common/protocol/QcMessages.h"

#include <QObject>
#include <QStringList>

#include <functional>

class QcContentRegistry;

// The stonecutter screen the server opened for the local player (decision E-785): which stone
// blocks of the inventory the stonecutter cuts, what each becomes (the content's stonecutting
// recipes, decision E-784), and the cuts the player asks for. Part of QcClientSession; GUI-free.
class QcClientStonecutter : public QObject
{
    Q_OBJECT

public:
    using Sender = std::function<void(const QcMessage &)>;

    void setSender(Sender sender) { m_send = std::move(sender); }
    // Takes over OpenStonecutter and CloseStonecutter; false for other messages.
    bool handle(const QcMessage &message);

    bool isOpen() const { return m_open; }
    QcBlockPos pos() const { return m_pos; }
    // Asks the server to cut by the recipe `recipe` once, or as often as possible (`repeat`); false
    // when no screen is open.
    bool cut(const QString &recipe, bool repeat);
    // The player closed the screen.
    void close();

    // The items of `inventory` the stonecutter cuts, each once, in the order of the slots.
    [[nodiscard]] static QStringList inputs(const QcContentRegistry &content, const QList<QcItemStack> &inventory);

signals:
    void opened();
    void closed();

private:
    Sender m_send;
    bool m_open = false;
    QcBlockPos m_pos;
};
