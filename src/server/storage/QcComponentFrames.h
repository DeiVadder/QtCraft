#pragma once

#include "server/entities/QcEntityComponent.h"

#include <QByteArray>
#include <QDataStream>
#include <QList>
#include <QString>

#include <concepts>
#include <initializer_list>
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

// The fields of a known component of `version` read from a stream (`read(QDataStream &, quint16
// version)`, false for damaged data): a reference to a callable that outlives the read, without the
// allocation of a std::function.
class FieldReader
{
public:
    template<typename Read>
        requires(!std::same_as<std::remove_cvref_t<Read>, FieldReader>)
    FieldReader(const Read &read) // NOLINT(google-explicit-constructor): taken from lambdas in lists of known components
        : m_read(&read)
        , m_call([](const void *object, QDataStream &stream, quint16 version) { return (*static_cast<const Read *>(object))(stream, version); })
    {
    }

    bool operator()(QDataStream &stream, quint16 version) const { return m_call(m_read, stream, version); }

private:
    const void *m_read;
    bool (*m_call)(const void *, QDataStream &, quint16);
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
            m_stream.writeRawData(tail->payload.constData(), int(tail->payload.size()));
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
    // Overwrites the placeholder at byte `at` of the stream with `value`.
    template<typename Value>
    void patch(qint64 at, Value value);

    QDataStream &m_stream;
    const QcEntityComponents &m_kept;
    qint64 m_countAt = 0;
    quint16 m_count = 0;
};

// Reads the components of one entry from `stream`: those of an id in `known` with its reader, read in
// place, the others kept in `kept` as they are (beyond maxKept dropped and counted in `dropped`). Of
// a newer version of a known component the bytes after the known fields go to `kept` as its tail.
// False for damaged data: more components or longer ones than allowed, a length beyond the data, an
// id twice, version 0, a reader that fails or reads beyond its component, an older or the current
// version that holds more than its fields.
[[nodiscard]] bool read(QDataStream &stream, std::initializer_list<Known> known, QcEntityComponents &kept, int &dropped);

// Reads a QString written by QDataStream (byte length, then UTF-16) after checking its length: a
// damaged length of up to 4 GB is never allocated. False for a text longer than `maxChars` (the
// stream turns corrupt).
[[nodiscard]] bool readText(QDataStream &stream, QString &text, qsizetype maxChars);

} // namespace QcComponentFrames
