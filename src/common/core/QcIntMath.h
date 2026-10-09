#pragma once

// Integer floor division that rounds towards negative infinity.
constexpr int qcFloorDiv(int value, int divisor)
{
    const int quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

// Modulo whose result always lies in [0, divisor).
constexpr int qcFloorMod(int value, int divisor)
{
    return value - qcFloorDiv(value, divisor) * divisor;
}
