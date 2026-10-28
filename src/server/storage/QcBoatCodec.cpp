#include "server/storage/QcBoatCodec.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorldConstants.h"
#include "common/physics/QcBoatMotion.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcEntityCodec.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

using Entry = QcEntityRecord::Ordered<QcBoat>;
using QcEntityCodec::Component;

// No boat goes faster than this: QcBoatMotion::maxSpeed across while falling at the terminal
// velocity. Saving clamps the velocity to it, so no save of a running world counts as damaged.
const double fastestBoat = std::hypot(QcBoatMotion::maxSpeed, QcPhysics::terminalVelocity);
// A saved boat faster than this (with a margin for rounding) is damaged data.
const double maxSavedSpeed = fastestBoat + 1.0;

// The velocity no faster than fastestBoat.
QcVec3 savedVelocity(const QcVec3 &velocity)
{
    const double speed = velocity.length();
    return speed > fastestBoat ? velocity * (fastestBoat / speed) : velocity;
}

bool isFinite(const QcVec3 &v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Reads the entries of one chunk.
class Reader
{
public:
    Reader(QDataStream &stream, QcChunkPos chunk, const QcContentRegistry &content, QcLoadWarnings &warnings)
        : m_stream(stream)
        , m_chunk(chunk)
        , m_content(content)
        , m_warnings(warnings)
        , m_path(u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z))
    {
    }

    [[nodiscard]] std::optional<QList<Entry>> read()
    {
        QList<Entry> entries;
        if (m_stream.atEnd()) {
            return entries;
        }
        const std::optional<quint32> count = QcStreamLimits::readCount(m_stream, QcEntityCodec::maxPerChunk);
        if (!count) {
            return std::nullopt;
        }
        for (quint32 i = 0; i < *count; ++i) {
            const std::optional<std::optional<Entry>> entry = readEntry();
            if (!entry) {
                return std::nullopt;
            }
            if (*entry) {
                entries.append(**entry);
            }
        }
        return entries;
    }

private:
    // An engaged optional holding the entry, or nothing for an entry dropped with a warning;
    // nothing for damaged data.
    [[nodiscard]] std::optional<std::optional<Entry>> readEntry()
    {
        const std::optional<QString> item = QcComponentFrames::readText(m_stream, QcEntityCodec::maxTypeName);
        quint64 order = 0;
        m_stream >> order;
        QcBoat boat;
        const auto readFields = [&boat](QDataStream &in, quint16) {
            in >> boat.motion.position.x >> boat.motion.position.y >> boat.motion.position.z >> boat.motion.velocity.x >> boat.motion.velocity.y
                >> boat.motion.velocity.z >> boat.yaw;
            return in.status() == QDataStream::Ok;
        };
        std::optional<QcComponentFrames::ReadComponents> components =
            item ? QcComponentFrames::read(m_stream, {{static_cast<quint16>(Component::Boat), QcEntityCodec::kindVersion, readFields}}) : std::nullopt;
        if (!components || !plausible(boat)) {
            return std::nullopt;
        }

        const std::optional<QcItemId> id = m_content.itemId(*item);
        if (!id || !m_content.item(*id).placesBoat()) {
            m_warnings.addOnce(u"boat:"_s + *item, u"%1: unknown boat '%2' dropped (in this world reported only here)"_s.arg(m_path, *item));
            return std::optional<Entry>();
        }
        boat.item = *item;
        boat.yaw = qcWrappedYaw(boat.yaw);
        boat.kept.components = std::move(components->kept);
        return std::optional<Entry>(Entry{boat, order});
    }

    // A finite place in this record's chunk inside the world's height, a speed a boat can have.
    [[nodiscard]] bool plausible(const QcBoat &boat) const
    {
        const QcVec3 &feet = boat.motion.position;
        const QcVec3 &velocity = boat.motion.velocity;
        return isFinite(feet) && isFinite(velocity) && std::isfinite(boat.yaw) && std::abs(feet.y) <= 4.0 * QcWorld::worldHeight
               && std::abs(feet.x) <= QcWorld::maxChunkBlock + 1 && std::abs(feet.z) <= QcWorld::maxChunkBlock + 1
               && QcPhysics::blockContaining(feet).chunk() == m_chunk && velocity.length() <= maxSavedSpeed;
    }

    QDataStream &m_stream;
    QcChunkPos m_chunk;
    const QcContentRegistry &m_content;
    QcLoadWarnings &m_warnings;
    QString m_path;
};

} // namespace

namespace QcBoatCodec {

void write(QDataStream &stream, const QList<Entry> &boats)
{
    if (boats.isEmpty()) {
        return;
    }
    stream << static_cast<quint32>(boats.size());
    for (const auto &[boat, order] : boats) {
        stream << boat.item << order;
        QcComponentFrames::Writer components(stream, boat.kept.components);
        components.add(static_cast<quint16>(Component::Boat), QcEntityCodec::kindVersion, [&boat](QDataStream &out) {
            const QcVec3 &feet = boat.motion.position;
            const QcVec3 velocity = savedVelocity(boat.motion.velocity);
            out << feet.x << feet.y << feet.z << velocity.x << velocity.y << velocity.z << boat.yaw;
        });
        components.finish();
    }
}

[[nodiscard]] std::optional<QList<Entry>> read(QDataStream &stream, QcChunkPos chunk, const QcContentRegistry &content, QcLoadWarnings &warnings)
{
    return Reader(stream, chunk, content, warnings).read();
}

} // namespace QcBoatCodec
