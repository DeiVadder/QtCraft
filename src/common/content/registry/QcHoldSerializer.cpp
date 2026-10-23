#include "common/content/registry/QcHoldSerializer.h"

#include "common/content/parser/QcHoldParser.h"

#include <cmath>

namespace {

// Marks an unset category or grip.
constexpr quint8 unset = 0xff;

// The bounds one value of a vector must keep.
struct Range
{
    double low;
    double high;
};

void writeVector(QDataStream &stream, const std::optional<QcVec3> &vector)
{
    stream << vector.has_value();
    if (vector) {
        stream << vector->x << vector->y << vector->z;
    }
}

// An engaged optional holding the vector (or std::nullopt when it is unset); std::nullopt when
// the data is corrupt or out of `range`.
[[nodiscard]] std::optional<std::optional<QcVec3>> readVector(QDataStream &stream, Range range)
{
    bool present = false;
    stream >> present;
    if (!present) {
        return std::optional<QcVec3>();
    }
    QcVec3 v;
    stream >> v.x >> v.y >> v.z;
    for (const double value : {v.x, v.y, v.z}) {
        if (!(value >= range.low && value <= range.high)) {
            return std::nullopt;
        }
    }
    return std::optional(v);
}

void writeTransform(QDataStream &stream, const QcHoldTransform &transform)
{
    writeVector(stream, transform.translate);
    writeVector(stream, transform.rotate);
    writeVector(stream, transform.scale);
}

[[nodiscard]] std::optional<QcHoldTransform> readTransform(QDataStream &stream)
{
    const double move = QcHoldParser::maxTranslate;
    const auto translate = readVector(stream, {-move, move});
    const auto rotate = readVector(stream, {-QcHoldParser::maxRotate, QcHoldParser::maxRotate});
    const auto scale = readVector(stream, {QcHoldParser::minScale, QcHoldParser::maxScale});
    if (!translate || !rotate || !scale) {
        return std::nullopt;
    }
    return QcHoldTransform{*translate, *rotate, *scale};
}

template<typename Enum>
quint8 code(const std::optional<Enum> &value)
{
    return value ? static_cast<quint8>(*value) : unset;
}

// An engaged optional holding the value (or std::nullopt when unset); std::nullopt when the code
// is not below `count`.
template<typename Enum>
[[nodiscard]] std::optional<std::optional<Enum>> decode(quint8 code, std::size_t count)
{
    if (code == unset) {
        return std::optional<Enum>();
    }
    return code < count ? std::optional(std::optional(Enum(code))) : std::nullopt;
}

} // namespace

namespace QcHoldSerializer {

void write(QDataStream &stream, const QcHoldDisplay &display)
{
    stream << code(display.category) << code(display.grip);
    writeTransform(stream, display.firstPerson);
    writeTransform(stream, display.thirdPerson);
    writeTransform(stream, display.swing);
}

std::optional<QcHoldDisplay> read(QDataStream &stream)
{
    quint8 categoryCode = 0;
    quint8 gripCode = 0;
    stream >> categoryCode >> gripCode;
    const auto category = decode<QcHoldCategory>(categoryCode, qcHoldCategoryCount);
    const auto grip = decode<QcHoldGrip>(gripCode, qcHoldGripCount);
    const std::optional<QcHoldTransform> firstPerson = readTransform(stream);
    const std::optional<QcHoldTransform> thirdPerson = readTransform(stream);
    const std::optional<QcHoldTransform> swing = readTransform(stream);
    if (!category || !grip || !firstPerson || !thirdPerson || !swing || stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }
    return QcHoldDisplay{*category, *grip, *firstPerson, *thirdPerson, *swing};
}

void writeDefaults(QDataStream &stream, const QcHoldDefaults &defaults)
{
    for (const QcHoldDisplay &display : defaults.categories) {
        write(stream, display);
    }
}

std::optional<QcHoldDefaults> readDefaults(QDataStream &stream)
{
    QcHoldDefaults defaults;
    for (QcHoldDisplay &display : defaults.categories) {
        std::optional<QcHoldDisplay> read = QcHoldSerializer::read(stream);
        if (!read) {
            return std::nullopt;
        }
        display = *read;
    }
    return defaults;
}

} // namespace QcHoldSerializer
