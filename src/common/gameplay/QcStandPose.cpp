#include "common/gameplay/QcStandPose.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

enum Flag : quint8 { Arms = 1, BasePlate = 2, Small = 4, ShowName = 8 };

} // namespace

float qcNormalizedAngle(float degrees)
{
    if (!std::isfinite(degrees)) {
        return 0.0f;
    }

    float angle = std::fmod(degrees, 360.0f);
    if (angle > 180.0f) {
        angle -= 360.0f;
    } else if (angle <= -180.0f) {
        angle += 360.0f;
    }
    return angle;
}

QcStandPose QcStandPose::normalized() const
{
    QcStandPose pose = *this;
    for (Rotation &rotation : pose.parts) {
        rotation = {qcNormalizedAngle(rotation.x), qcNormalizedAngle(rotation.y), qcNormalizedAngle(rotation.z)};
    }
    return pose;
}

quint8 QcStandPose::flags() const
{
    return static_cast<quint8>((arms ? Arms : 0) | (basePlate ? BasePlate : 0) | (small ? Small : 0) | (showName ? ShowName : 0));
}

QcStandPose QcStandPose::withFlags(quint8 flags)
{
    QcStandPose pose;
    pose.arms = (flags & Arms) != 0;
    pose.basePlate = (flags & BasePlate) != 0;
    pose.small = (flags & Small) != 0;
    pose.showName = (flags & ShowName) != 0;
    return pose;
}

QString QcStandPose::partName(Part part)
{
    static const std::array<QString, partCount> names{u"head"_s, u"body"_s, u"left_arm"_s, u"right_arm"_s, u"left_leg"_s, u"right_leg"_s};
    return names[static_cast<std::size_t>(part)];
}

std::optional<QcStandPose::Part> QcStandPose::partFromName(const QString &name)
{
    for (std::size_t i = 0; i < partCount; ++i) {
        if (partName(static_cast<Part>(i)) == name) {
            return static_cast<Part>(i);
        }
    }
    return std::nullopt;
}
