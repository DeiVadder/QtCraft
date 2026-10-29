// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/entities/QcHurtWindow.h"

#include <algorithm>

QcHurtWindow::Take QcHurtWindow::wouldTake(double amount) const
{
    if (!active()) {
        return Take::Fresh;
    }
    return amount > m_lastAmount ? Take::Stronger : Take::Ignored;
}

double QcHurtWindow::take(double amount, int ticks)
{
    switch (wouldTake(amount)) {
    case Take::Ignored:
        return 0.0;
    case Take::Stronger: {
        const double dealt = amount - m_lastAmount;
        m_lastAmount = amount;
        return dealt;
    }
    case Take::Fresh:
        break;
    }

    if (amount <= 0.0) {
        return 0.0;
    }
    open(amount, ticks);
    return amount;
}

double QcHurtWindow::force(double amount, int ticks)
{
    if (amount <= 0.0) {
        return 0.0;
    }
    open(amount, ticks);
    return amount;
}

void QcHurtWindow::tick()
{
    m_sinceHit = std::min(m_sinceHit + 1, knockbackTicks);
    if (m_ticksLeft <= 0) {
        return;
    }

    --m_ticksLeft;
    if (m_ticksLeft == 0) {
        m_lastAmount = 0.0;
    }
}

void QcHurtWindow::clear()
{
    m_ticksLeft = 0;
    m_lastAmount = 0.0;
}

void QcHurtWindow::limit(int ticks)
{
    if (m_ticksLeft <= std::max(0, ticks)) {
        return;
    }

    m_ticksLeft = std::max(0, ticks);
    if (m_ticksLeft == 0) {
        m_lastAmount = 0.0;
    }
}

void QcHurtWindow::open(double amount, int ticks)
{
    m_sinceHit = 0;
    m_ticksLeft = std::max(0, ticks);
    m_lastAmount = m_ticksLeft > 0 ? amount : 0.0;
}
