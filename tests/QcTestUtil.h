#pragma once

#include <QByteArray>

// QVERIFY2 evaluates its message even when the check passes, so reading error() of a successful
// std::expected there is undefined behaviour. This returns the message only for failures.
template<typename Result>
QByteArray qcErrorText(const Result &result)
{
    return result.has_value() ? QByteArray() : result.error().toUtf8();
}
