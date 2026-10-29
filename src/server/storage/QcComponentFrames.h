// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/entities/QcEntityComponent.h"

#include <QByteArray>
#include <QDataStream>
#include <QList>
#include <QString>

#include <concepts>
#include <initializer_list>
#include <optional>
#include <type_traits>

// The components of a saved entry (decision E-420): after the entry's header a count, then per
// component `quint16 id, quint16 version, quint32 length, payload`. A reader skips what it does not
// know by its length and keeps it (QcEntityComponent), so saving again loses nothing.
//
// Versions of a component only append fields: version n+1 is version n followed by new fields. A
// reader of version n reads an older payload with defaults for the fields it lacks, and of a newer
// one the fields it knows; the rest is kept as the component's tail and written back after them
// under the newer version. A change that cannot be made by appending is a new component id.
//
// The tail goes back unchanged while the fields before it may have changed at runtime (a creature
// lost health). So a newer field must never depend on the contents of older fields (no count of an
// older list, no checksum, no value relative to an older one): such a field is a new component id.
namespace QcComponentFrames {

// Components of one entry at most; more is damaged data. Every version and every mod keeps this,
// together with the two limits below that make it hold for what a version writes back.
inline constexpr quint32 maxComponents = 64;
// Known components one kind writes at most, in any version (QcEntityCodec::Component).
inline constexpr quint32 maxKnown = 16;
// Components of unknown ids one entry keeps at most; beyond that they are dropped with a warning,
// so the kept ones and the known ones of any version always fit into maxComponents.
inline constexpr quint32 maxKept = maxComponents - maxKnown;
// Bytes of one component at most; a valid world never comes close.
inline constexpr quint32 maxLength = 1u << 20;
// The ids of components (QcEntityCodec::Component) and of block-entity tables
// (QcBlockEntityRecord::Table) from here on belong to mods, those below to the game (decision
// E-440). Either kind of unknown id is kept as it is.
inline constexpr quint16 firstModId = 0x8000;

// The fields of a known component of `version` read from a stream (`read(QDataStream &, quint16
// version)`, or `read(QDataStream &, quint16 version, quint32 length)` for a reader that needs the
// component's length; false for damaged data): a reference to a callable that outlives the read,
// without the allocation of a std::function.
class FieldReader
{
public:
    template<typename Read>
        requires(!std::same_as<std::remove_cvref_t<Read>, FieldReader>)
    FieldReader(const Read &read) // NOLINT(google-explicit-constructor): taken from lambdas in lists of known components
        : m_read(&read)
        , m_call([](const void *object, QDataStream &stream, quint16 version, [[maybe_unused]] quint32 length) {
            const Read &reader = *static_cast<const Read *>(object);
            if constexpr (std::invocable<const Read &, QDataStream &, quint16, quint32>) {
                return reader(stream, version, length);
            } else {
                return reader(stream, version);
            }
        })
    {
    }

    bool operator()(QDataStream &stream, quint16 version, quint32 length) const { return m_call(m_read, stream, version, length); }

private:
    const void *m_read;
    bool (*m_call)(const void *, QDataStream &, quint16, quint32);
};

// A component this version reads: its id, the version it writes and its reader.
struct Known
{
    Known(quint16 knownId, quint16 knownVersion, FieldReader reader)
        : id(knownId)
        , version(knownVersion)
        , read(reader)
    {
    }

    quint16 id;
    quint16 version;
    FieldReader read;
};

// Writes the components of one entry straight into the entry's stream: the known ones in the order
// they are added, each followed by its kept tail, then the kept components of unknown ids. The count
// and each length are written as placeholders and filled in when known (the stream's device seeks).
class Writer
{
public:
    Writer(QDataStream &stream, const QcEntityComponents &kept);

    // Adds the known component `id` of `version` whose fields `write(QDataStream &)` writes.
    template<typename Write>
    void add(quint16 id, quint16 version, Write write)
    {
        const QcEntityComponent *tail = tailOf(id, version);
        const qint64 start = begin(id, tail ? tail->version : version);
        write(m_stream);
        if (tail) {
            m_stream.writeRawData(tail->payload.constData(), static_cast<int>(tail->payload.size()));
        }
        end(start);
    }
    // Writes the kept components of unknown ids and the count.
    void finish();

private:
    const QcEntityComponent *tailOf(quint16 id, quint16 version) const;
    // Writes a frame's id, version and a placeholder length; returns where its payload starts.
    qint64 begin(quint16 id, quint16 version);
    // Fills in the length of the frame whose payload started at `start`.
    void end(qint64 start);

    QDataStream &m_stream;
    const QcEntityComponents &m_kept;
    qint64 m_countAt = 0;
    quint16 m_count = 0;
};

// What read() keeps of one entry's components besides the known fields, and how many unknown
// components it dropped beyond maxKept.
struct ReadComponents
{
    QcEntityComponents kept;
    int dropped = 0;
};

// Reads the components of one entry from `stream`: those of an id in `known` with its reader, read in
// place, the others kept as they are (beyond maxKept dropped and counted). Of a newer version of a
// known component the bytes after the known fields are kept as its tail.
// Nullopt for damaged data: more components or longer ones than allowed, a length beyond the data, an
// id twice, version 0, a reader that fails or reads beyond its component, an older or the current
// version that holds more than its fields. `lengthLimit` bounds one component (maxLength for entities;
// the tables of a chunk's block entities allow more, QcBlockEntityCodec).
[[nodiscard]] std::optional<ReadComponents> read(QDataStream &stream, std::initializer_list<Known> known, quint32 lengthLimit = maxLength);

// Overwrites the placeholder length at byte `at` of `stream` (which writes to a seekable device)
// with `length`: in place in the bytes of a QBuffer, else by seeking there and back.
void patchLength(QDataStream &stream, qint64 at, quint32 length);

// Reads the raw bytes of `length` that follow in `stream`, checked against the data left before
// anything is allocated.
[[nodiscard]] std::optional<QByteArray> readRaw(QDataStream &stream, qint64 length);

// Reads a QString written by QDataStream (byte length, then UTF-16) after checking its length: a
// damaged length of up to 4 GB is never allocated. std::nullopt for a text longer than `maxChars`
// (the stream turns corrupt).
[[nodiscard]] std::optional<QString> readText(QDataStream &stream, qsizetype maxChars);

} // namespace QcComponentFrames
