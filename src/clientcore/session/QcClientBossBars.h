// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QList>
#include <QObject>

/*! \brief The boss bars the server shows the local player (QcMsg::BossBar, decision E-1127), in
    the order they came; a bar that goes leaves the others in their order, and a change of
    dimension (QcMsg::DimensionChange) takes them all away. Part of QcClientSession; GUI-free. */
class QcClientBossBars : public QObject
{
    Q_OBJECT

public:
    /*! The most bars shown at once; more are kept but not shown (the role model shows as many as
        fit on the screen). */
    static constexpr qsizetype maxShown = 4;

    /*! Takes over \a message when it is a BossBar (returns true); forgets every bar on a
        DimensionChange and leaves it to the others (returns false). */
    bool handle(const QcMessage &message);

    /*! Returns the bars shown, at most maxShown. */
    [[nodiscard]] QList<QcMsg::BossBar> bars() const { return m_bars.first(std::min(m_bars.size(), maxShown)); }

signals:
    /*! The bars changed. */
    void changed();

private:
    void apply(const QcMsg::BossBar &bar);

    QList<QcMsg::BossBar> m_bars;
};
