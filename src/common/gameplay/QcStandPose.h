// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <array>
#include <optional>

/*! \brief How an armour stand stands (decision E-238): a rotation per body part and what it shows.

    Kept by the server, sent to every client in EntitySpawn and saved with the world; the
    commands /armorstand and /summon change it (later also a stand screen). */
struct QcStandPose
{
    /*! The body parts. */
    enum class Part : quint8 {
        /*! The head. */
        Head,
        /*! The body. */
        Body,
        /*! The left arm. */
        LeftArm,
        /*! The right arm. */
        RightArm,
        /*! The left leg. */
        LeftLeg,
        /*! The right leg. */
        RightLeg
    };
    /*! The number of parts. */
    static constexpr std::size_t partCount = 6;
    /*! Body of a full-size stand (server hits, client targeting); a small one is half as large.
        \qcshared */
    static constexpr double bodyWidth = 0.5;
    static constexpr double bodyHeight = 1.975;

    /*! \brief Degrees around the part's pivot like QcAnimator::BoneRotation: x (pitch) swings
        arms and legs forward and tilts the head up, y (yaw) turns it towards the stand's right,
        z (roll) lifts it outwards to the right. */
    struct Rotation
    {
        /*! Pitch. */
        float x = 0.0f;
        /*! Yaw. */
        float y = 0.0f;
        /*! Roll. */
        float z = 0.0f;

        bool operator==(const Rotation &) const = default;
    };

    /*! The rotation of each part, in the order of Part. */
    std::array<Rotation, partCount> parts = defaultParts();
    /*! Whether the stand has arms. */
    bool arms = true;
    /*! Whether it stands on a base plate. */
    bool basePlate = true;
    /*! Whether it is the small stand. */
    bool small = false;
    /*! The stand's name floats above it (only when it has one). */
    bool showName = false;

    /*! Returns the rotation of \a part. */
    const Rotation &at(Part part) const { return parts[static_cast<std::size_t>(part)]; }
    /*! Returns the rotation of \a part to change it. */
    Rotation &at(Part part) { return parts[static_cast<std::size_t>(part)]; }
    bool operator==(const QcStandPose &) const = default;

    /*! Arms hanging slightly apart, legs a little spread. */
    static std::array<Rotation, partCount> defaultParts()
    {
        return {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {-10.0f, 0.0f, -10.0f}, {-15.0f, 0.0f, 10.0f}, {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}}};
    }

    /*! Every angle finite and within (-180, 180]; non-finite angles become 0. */
    QcStandPose normalized() const;
    /*! Returns arms, base plate, size and name display as flag bits, as sent and saved. */
    quint8 flags() const;
    /*! Returns the default pose with the switches of the bits \a flags. */
    static QcStandPose withFlags(quint8 flags);

    /*! Returns the name of \a part in commands: "head", "body", "left_arm", "right_arm",
        "left_leg", "right_leg". */
    static QString partName(Part part);
    /*! Returns the part named \a name; std::nullopt for other names. */
    [[nodiscard]] static std::optional<Part> partFromName(const QString &name);
};

/*! Returns the angle \a degrees normalised into (-180, 180]; non-finite values become 0. */
float qcNormalizedAngle(float degrees);
