// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QObject>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <utility>

// The toasts of reached advancements (decision E-923, GameController.player.advancementToasts): the
// one on show and those waiting for it. At most maxWaiting wait; further ones are counted and come
// as one last "+N more" toast (granting everything by command reaches dozens at once). The toast in
// QML only slides it in and out and calls next() when it has gone; the game controller clears the
// queue when the player leaves the world (decision E-954).
class QcAdvancementQueue : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AdvancementQueue)
    QML_UNCREATABLE("Provided by GameController.player.advancementToasts")
    // The advancement on show ({name, title, description, icon, heading, special}), empty while none;
    // changes (and notifies) for every toast, also for two equal ones in a row.
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged FINAL)
    Q_PROPERTY(bool showing READ showing NOTIFY currentChanged FINAL)

public:
    // Advancements that wait for their toast at most; the rest are counted.
    static constexpr int maxWaiting = 5;

    explicit QcAdvancementQueue(QObject *parent = nullptr);

    QVariantMap current() const { return m_current; }
    bool showing() const { return !m_current.isEmpty(); }
    // Advancements waiting for a toast of their own, and the number of those beyond them.
    const QList<QVariantMap> &waiting() const { return m_waiting; }
    int folded() const { return m_folded; }

    // Queues `advancement` (QcPlayerController::announceAdvancement); shows it at once when no toast
    // is on show.
    void add(const QVariantMap &advancement);
    // The toast on show has gone: shows the next one, or none when nothing waits.
    Q_INVOKABLE void next();
    // Forgets the toast on show and those waiting.
    void clear();

signals:
    void currentChanged();

private:
    // The toast for the advancements beyond the queue.
    QVariantMap foldedToast() const;
    void setCurrent(const QVariantMap &current);
    void setWaiting(QList<QVariantMap> waiting) { m_waiting = std::move(waiting); }
    void setFolded(int folded) { m_folded = folded; }

    QVariantMap m_current;
    QList<QVariantMap> m_waiting;
    int m_folded = 0;
};
