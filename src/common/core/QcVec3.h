#pragma once

#include <cmath>

// Minimal double-precision vector for GUI-free code (QVector3D lives in QtGui).
struct QcVec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr QcVec3 operator+(const QcVec3 &other) const { return {x + other.x, y + other.y, z + other.z}; }
    constexpr QcVec3 operator-(const QcVec3 &other) const { return {x - other.x, y - other.y, z - other.z}; }
    constexpr QcVec3 operator*(double factor) const { return {x * factor, y * factor, z * factor}; }
    constexpr QcVec3 &operator+=(const QcVec3 &other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
    constexpr bool operator==(const QcVec3 &) const = default;

    constexpr double dot(const QcVec3 &other) const { return x * other.x + y * other.y + z * other.z; }
    double length() const { return std::sqrt(dot(*this)); }
    QcVec3 normalized() const
    {
        const double len = length();
        return len > 0.0 ? *this * (1.0 / len) : QcVec3{};
    }
};
