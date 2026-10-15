#pragma once

#include <QString>

#include <array>
#include <optional>

// How an armour stand stands (decision E-238): a rotation per body part and what it shows. Kept by
// the server, sent to every client in EntitySpawn and saved with the world; the commands
// /armorstand and /summon change it (later also a stand screen).
struct QcStandPose
{
    enum class Part : quint8 { Head, Body, LeftArm, RightArm, LeftLeg, RightLeg };
    static constexpr std::size_t partCount = 6;
    // Body of a full-size stand (server hits, client targeting); a small one is half as large.
    static constexpr double bodyWidth = 0.5;
    static constexpr double bodyHeight = 1.975;

    // Degrees around the part's pivot like QcAnimator::BoneRotation: x (pitch) swings arms and legs
    // forward and tilts the head up, y (yaw) turns it towards the stand's right, z (roll) lifts it
    // outwards to the right.
    struct Rotation
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        bool operator==(const Rotation &) const = default;
    };

    std::array<Rotation, partCount> parts = defaultParts();
    bool arms = true;
    bool basePlate = true;
    bool small = false;
    // The stand's name floats above it (only when it has one).
    bool showName = false;

    const Rotation &at(Part part) const { return parts[std::size_t(part)]; }
    Rotation &at(Part part) { return parts[std::size_t(part)]; }
    bool operator==(const QcStandPose &) const = default;

    // Arms hanging slightly apart, legs a little spread.
    static std::array<Rotation, partCount> defaultParts()
    {
        return {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {-10.0f, 0.0f, -10.0f}, {-15.0f, 0.0f, 10.0f}, {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}}};
    }

    // Every angle finite and within (-180, 180]; non-finite angles become 0.
    QcStandPose normalized() const;
    // Flag bits as sent and saved.
    quint8 flags() const;
    static QcStandPose withFlags(quint8 flags);

    // Part names in commands: "head", "body", "left_arm", "right_arm", "left_leg", "right_leg".
    static QString partName(Part part);
    [[nodiscard]] static std::optional<Part> partFromName(const QString &name);
};

// Normalises one angle in degrees into (-180, 180]; non-finite values become 0.
float qcNormalizedAngle(float degrees);
