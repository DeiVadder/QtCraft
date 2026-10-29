// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcAdvancementProgress.h"

#include "common/protocol/QcStreamLimits.h"

#include <QDataStream>
#include <QIODevice>

#include <algorithm>

namespace {

constexpr quint16 formatVersion = 1;

bool fitsName(const QString &name)
{
    return !name.isEmpty() && name.size() <= QcAdvancementProgress::maxNameLength;
}

} // namespace

bool QcAdvancementProgress::grant(const QString &advancement, const QString &criterion)
{
    const auto it = m_done.constFind(advancement);
    const bool room = it != m_done.constEnd() ? it->size() < QcAdvancementDefinition::maxCriteria : m_done.size() < maxAdvancements;
    if (!room || !fitsName(advancement) || !fitsName(criterion) || has(advancement, criterion)) {
        return false;
    }
    m_done[advancement].insert(criterion);
    return true;
}

bool QcAdvancementProgress::revoke(const QString &advancement, const QString &criterion)
{
    const auto it = m_done.find(advancement);
    if (it == m_done.end() || !it->remove(criterion)) {
        return false;
    }
    if (it->isEmpty()) {
        m_done.erase(it);
    }
    return true;
}

bool QcAdvancementProgress::setCriteria(const QString &advancement, const QSet<QString> &criteria)
{
    if (criteria.isEmpty()) {
        m_done.remove(advancement);
        return true;
    }
    const bool fits = fitsName(advancement) && criteria.size() <= QcAdvancementDefinition::maxCriteria && std::ranges::all_of(criteria, fitsName);
    if (!fits || (!m_done.contains(advancement) && m_done.size() >= maxAdvancements)) {
        return false;
    }
    m_done.insert(advancement, criteria);
    return true;
}

bool QcAdvancementProgress::has(const QString &advancement, const QString &criterion) const
{
    const auto it = m_done.constFind(advancement);
    return it != m_done.constEnd() && it->contains(criterion);
}

QStringList QcAdvancementProgress::advancements() const
{
    QStringList names = m_done.keys();
    names.sort();
    return names;
}

QByteArray QcAdvancementProgress::bytes() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_8);
    stream << formatVersion << static_cast<quint32>(m_done.size());
    const QStringList names = advancements();
    for (const QString &name : names) {
        QStringList criteria = m_done.value(name).values();
        criteria.sort();
        stream << name << criteria;
    }
    return bytes;
}

std::optional<QcAdvancementProgress> QcAdvancementProgress::fromBytes(const QByteArray &bytes)
{
    QDataStream stream(bytes);
    stream.setVersion(QDataStream::Qt_6_8);
    quint16 version = 0;
    stream >> version;
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, maxAdvancements);
    if (!count || version == 0) {
        return std::nullopt;
    }

    QcAdvancementProgress progress;
    for (quint32 i = 0; i < *count; ++i) {
        QString name;
        stream >> name;
        const std::optional<QStringList> criteria = QcStreamLimits::readBoundedStrings(stream, QcAdvancementDefinition::maxCriteria, maxNameLength);
        if (stream.status() != QDataStream::Ok || !criteria || !fitsName(name) || !std::ranges::all_of(*criteria, fitsName)) {
            return std::nullopt;
        }
        progress.setCriteria(name, QSet<QString>(criteria->cbegin(), criteria->cend()));
    }
    return progress;
}
