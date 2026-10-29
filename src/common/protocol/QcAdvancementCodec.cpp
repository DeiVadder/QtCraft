// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payload of the advancement message (QcAdvancementMessages.h, decision E-923): at most
// AdvancementProgress::maxEntries entries, each with at most maxCriteria criterion names; every
// name at most maxNameLength UTF-16 units. Anything beyond fails the message.

#include "common/protocol/QcProtocolCodec.h"
#include "common/protocol/QcStreamLimits.h"

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

namespace {

using Progress = QcMsg::AdvancementProgress;

bool fitsName(const QString &name)
{
    return !name.isEmpty() && name.size() <= Progress::maxNameLength;
}

} // namespace

void encode(QDataStream &stream, const QcMsg::AdvancementProgress &m)
{
    stream << m.reset << static_cast<quint32>(m.entries.size());
    for (const Progress::Entry &entry : m.entries) {
        stream << entry.advancement << entry.criteria << entry.done << entry.announce;
    }
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdvancementProgress>, QDataStream &stream)
{
    QcMsg::AdvancementProgress m;
    stream >> m.reset;
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Progress::maxEntries);
    if (!count) {
        return qcError(u"Too many advancement entries"_s);
    }
    for (quint32 i = 0; i < *count; ++i) {
        Progress::Entry entry;
        stream >> entry.advancement;
        const std::optional<QStringList> criteria = QcStreamLimits::readBoundedStrings(stream, Progress::maxCriteria, Progress::maxNameLength);
        stream >> entry.done >> entry.announce;
        if (stream.status() != QDataStream::Ok || !criteria || !fitsName(entry.advancement) || !std::ranges::all_of(*criteria, fitsName)) {
            return qcError(u"Corrupt advancement entry"_s);
        }
        entry.criteria = *criteria;
        m.entries.append(std::move(entry));
    }
    return QcResult<QcMessage>(std::move(m));
}

} // namespace QcProtocolCodec
