#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace QcMsg {

// Server -> client: the receiving player's progress in the adventure guide (decision E-923). After
// the welcome the server sends all of it in messages of at most maxEntries entries, the first with
// `reset` (the client forgets what it knew); afterwards each change as a delta with the
// advancements whose criteria changed. An entry carries all met criteria of its advancement (none:
// revoked) and whether it is reached; `announce` asks for the toast (reached just now).
struct AdvancementProgress
{
    static constexpr qsizetype maxEntries = 256;
    static constexpr qsizetype maxCriteria = 32;
    static constexpr qsizetype maxNameLength = 128;

    struct Entry
    {
        QString advancement;
        QStringList criteria;
        bool done = false;
        bool announce = false;

        bool operator==(const Entry &other) const = default;
    };

    bool reset = false;
    QList<Entry> entries;
};

} // namespace QcMsg
