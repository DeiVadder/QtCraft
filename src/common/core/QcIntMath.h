// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! Returns \a value divided by \a divisor, rounded towards negative infinity. */
constexpr int qcFloorDiv(int value, int divisor)
{
    const int quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

/*! Returns \a value modulo \a divisor, for a positive divisor always in [0, divisor). */
constexpr int qcFloorMod(int value, int divisor)
{
    return value - qcFloorDiv(value, divisor) * divisor;
}
