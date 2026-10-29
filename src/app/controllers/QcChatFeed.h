// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <optional>

// The chat lines of the HUD (GameController.hud.chat, decision E-875): while the chat is closed
// the last lines show for fadeAfterMs and then fade out over fadeMs; while it is open the last
// openLines lines all show again. clear() (the client command /clearchat) empties the view.
class QcChatFeed : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChatFeed)
    QML_UNCREATABLE("Provided by GameController.hud.chat")
    // The lines to draw, oldest first, each as { text, opacity, fadeMs }: a line with fadeMs > 0
    // fades from opacity to 0 over that many milliseconds (the HUD animates it). The list changes
    // only when a line comes, starts to fade or goes, the chat opens or closes, or it is cleared.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged FINAL)
    // Whether the chat line is open (Main.qml binds it).
    Q_PROPERTY(bool open READ isOpen WRITE setOpen NOTIFY openChanged FINAL)

public:
    static constexpr qint64 fadeAfterMs = 10'000;
    static constexpr qint64 fadeMs = 1'000;
    static constexpr qsizetype closedLines = 8;
    static constexpr qsizetype openLines = 20;
    static constexpr qsizetype historyLines = 100;
    // The client command that clears the view; never sent to the server.
    static constexpr QLatin1StringView clearCommand{"/clearchat"};

    struct Entry
    {
        QString text;
        double opacity = 1.0;
        // Milliseconds until the line is gone, while it fades; 0 while it stays.
        qint64 fadeMs = 0;
    };

    explicit QcChatFeed(QObject *parent = nullptr);

    void add(const QString &text);
    void clear();
    bool isOpen() const { return m_open; }
    void setOpen(bool open);
    // The lines to draw now (as entries).
    QList<Entry> shown() const;
    QVariantList entries() const;
    // The clock in milliseconds the lines age by; tests pass their own and call refresh().
    void setClock(std::function<qint64()> clock);
    // Looks at the clock again: lines that faded go, lines whose time ran out start to fade.
    void refresh();
    // Milliseconds until the feed looks at the clock again on its own (its timer); nullopt while
    // nothing is due (open, empty, or every line gone).
    [[nodiscard]] std::optional<qint64> nextRefreshMs() const { return m_nextRefresh; }

signals:
    void entriesChanged();
    void openChanged();

private:
    struct Line
    {
        QString text;
        qint64 at = 0;
    };

    qint64 now() const { return m_clock ? m_clock() : m_elapsed.elapsed(); }
    // When to look again: at the next start or end of a line's fade, or never.
    void schedule();

    QList<Line> m_history;
    bool m_open = false;
    QTimer m_timer;
    std::optional<qint64> m_nextRefresh;
    QElapsedTimer m_elapsed;
    std::function<qint64()> m_clock;
};
