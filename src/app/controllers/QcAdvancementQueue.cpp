// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcAdvancementQueue.h"

using namespace Qt::StringLiterals;

QcAdvancementQueue::QcAdvancementQueue(QObject *parent)
    : QObject(parent)
{
}

void QcAdvancementQueue::add(const QVariantMap &advancement)
{
    if (waiting().size() < maxWaiting) {
        setWaiting(waiting() + QList{advancement});
    } else {
        setFolded(folded() + 1);
    }

    if (!showing()) {
        next();
    }
}

void QcAdvancementQueue::next()
{
    if (!waiting().isEmpty()) {
        QList<QVariantMap> rest = waiting();
        const QVariantMap first = rest.takeFirst();
        setWaiting(std::move(rest));
        setCurrent(first);
    } else if (folded() > 0) {
        setCurrent(foldedToast());
        setFolded(0);
    } else if (showing()) {
        setCurrent({});
    }
}

void QcAdvancementQueue::clear()
{
    setWaiting({});
    setFolded(0);
    if (showing()) {
        setCurrent({});
    }
}

QVariantMap QcAdvancementQueue::foldedToast() const
{
    return {{u"name"_s, u"more"_s},
            {u"title"_s, tr("+%n more", "", folded())},
            {u"description"_s, QString()},
            {u"heading"_s, tr("Advancements made!")},
            {u"icon"_s, QString()},
            {u"special"_s, false}};
}

void QcAdvancementQueue::setCurrent(const QVariantMap &current)
{
    // Always notifies: each toast slides in anew, also one equal to the one before.
    m_current = current;
    emit currentChanged();
}
