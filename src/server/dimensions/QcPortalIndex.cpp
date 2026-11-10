// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcPortalIndex.h"

#include "common/core/QcWorldConstants.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

using Axis = QcPortalFrame::Axis;
constexpr int fileVersion = 1;

QString axisName(Axis axis)
{
    switch (axis) {
    case Axis::X:
        return u"x"_s;
    case Axis::Z:
        return u"z"_s;
    case Axis::Y:
        return u"y"_s;
    }
    return u"x"_s;
}

std::optional<Axis> axisOf(const QString &name)
{
    if (name == u"x"_s) {
        return Axis::X;
    }
    if (name == u"z"_s) {
        return Axis::Z;
    }
    return name == u"y"_s ? std::optional(Axis::Y) : std::nullopt;
}

QJsonObject toJson(const QcPortalIndex::Entry &entry)
{
    const QcPortalFrame &frame = entry.frame;
    return {{u"portal"_s, entry.portal},  {u"x"_s, frame.origin.x},         {u"y"_s, frame.origin.y},          {u"z"_s, frame.origin.z},
            {u"axis"_s, axisName(frame.axis)}, {u"width"_s, frame.width}, {u"height"_s, frame.height}};
}

// The entry of `json`; none when it is not plausible.
std::optional<QcPortalIndex::Entry> fromJson(const QJsonObject &json)
{
    constexpr int far = 30'000'000;
    const QString portal = json.value(u"portal"_s).toString();
    const std::optional<Axis> axis = axisOf(json.value(u"axis"_s).toString());
    const QcBlockPos origin{json.value(u"x"_s).toInt(far + 1), json.value(u"y"_s).toInt(-1), json.value(u"z"_s).toInt(far + 1)};
    const int width = json.value(u"width"_s).toInt(0);
    const int height = json.value(u"height"_s).toInt(0);
    const bool inside = std::abs(origin.x) <= far && std::abs(origin.z) <= far && origin.y >= 0 && origin.y < QcWorld::worldHeight;
    const bool sized = width >= 1 && height >= 1 && width <= QcPortalDefinition::maxInner && height <= QcPortalDefinition::maxInner;
    if (portal.isEmpty() || portal.size() > 128 || !axis || !inside || !sized) {
        return std::nullopt;
    }
    return QcPortalIndex::Entry{portal, {origin, *axis, width, height}};
}

} // namespace

bool QcPortalIndex::add(const Entry &entry)
{
    if (m_entries.contains(entry)) {
        return false;
    }
    if (m_entries.size() >= maxPortals) {
        m_entries.removeFirst();
    }
    m_entries.append(entry);
    changed();
    return true;
}

bool QcPortalIndex::remove(const Entry &entry)
{
    if (!m_entries.removeOne(entry)) {
        return false;
    }
    changed();
    return true;
}

QList<QcPortalIndex::Entry> QcPortalIndex::near(const QString &portal, const QcVec3 &point, double radius) const
{
    QList<Entry> found;
    for (const Entry &entry : m_entries) {
        const QcVec3 middle = entry.frame.centre();
        if (entry.portal == portal && std::abs(middle.x - point.x) <= radius && std::abs(middle.z - point.z) <= radius) {
            found.append(entry);
        }
    }
    std::ranges::stable_sort(found, {}, [&point](const Entry &entry) { return (entry.frame.centre() - point).length(); });
    return found;
}

std::optional<QcPortalIndex::Entry> QcPortalIndex::holding(QcBlockPos cell) const
{
    const auto it = std::ranges::find_if(m_entries, [cell](const Entry &entry) { return entry.frame.contains(cell); });
    return it != m_entries.end() ? std::optional(*it) : std::nullopt;
}

qsizetype QcPortalIndex::forgetGone(const std::function<bool(const Entry &)> &isGone)
{
    const qsizetype removed = m_entries.removeIf(isGone);
    if (removed > 0) {
        changed();
    }
    return removed;
}

void QcPortalIndex::restore(const QList<Entry> &entries)
{
    m_entries = entries.first(std::min(entries.size(), maxPortals));
}

QByteArray QcPortalIndex::bytes() const
{
    QJsonArray portals;
    for (const Entry &entry : m_entries) {
        portals.append(toJson(entry));
    }
    return QJsonDocument(QJsonObject{{u"version"_s, fileVersion}, {u"portals"_s, portals}}).toJson(QJsonDocument::Compact);
}

QcResult<QList<QcPortalIndex::Entry>> QcPortalIndex::fromBytes(const QByteArray &bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return qcError(u"%1 is damaged: %2"_s.arg(QString::fromLatin1(fileName), error.errorString()));
    }

    QList<Entry> entries;
    const QJsonArray portals = document.object().value(u"portals"_s).toArray();
    for (const auto &value : portals) {
        if (std::optional<Entry> entry = fromJson(value.toObject()); entry && entries.size() < maxPortals) {
            entries.append(std::move(*entry));
        }
    }
    return entries;
}

QcResult<QList<QcPortalIndex::Entry>> QcPortalIndex::read(const QString &directory)
{
    QFile file(QDir(directory).filePath(QString::fromLatin1(fileName)));
    if (!file.exists()) {
        return QList<Entry>();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"%1 cannot be read"_s.arg(file.fileName()));
    }
    return fromBytes(file.readAll());
}
