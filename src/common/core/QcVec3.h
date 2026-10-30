// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cmath>

/*! \brief Minimal double-precision vector for GUI-free code (QVector3D lives in QtGui). */
struct QcVec3
{
    /*! The components. \qcshared */
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    /*! Returns the sum with \a other. */
    constexpr QcVec3 operator+(const QcVec3 &other) const { return {x + other.x, y + other.y, z + other.z}; }
    /*! Returns the difference to \a other. */
    constexpr QcVec3 operator-(const QcVec3 &other) const { return {x - other.x, y - other.y, z - other.z}; }
    /*! Returns the vector scaled by \a factor. */
    constexpr QcVec3 operator*(double factor) const { return {x * factor, y * factor, z * factor}; }
    /*! Adds \a other and returns this vector. */
    constexpr QcVec3 &operator+=(const QcVec3 &other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    constexpr bool operator==(const QcVec3 &) const = default;

    /*! Returns the dot product with \a other. */
    constexpr double dot(const QcVec3 &other) const { return x * other.x + y * other.y + z * other.z; }
    /*! Returns the length. */
    double length() const { return std::sqrt(dot(*this)); }
    /*! Returns the vector scaled to length 1; the zero vector stays zero. */
    QcVec3 normalized() const
    {
        const double len = length();
        return len > 0.0 ? *this * (1.0 / len) : QcVec3{};
    }
};
