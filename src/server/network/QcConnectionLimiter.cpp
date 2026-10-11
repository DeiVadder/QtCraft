#include "QcConnectionLimiter.h"

#include <algorithm>

QString QcConnectionLimiter::sourceOf(const QHostAddress &address)
{
    bool isIpv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIpv4);
    if (isIpv4) {
        return QHostAddress(ipv4).toString();
    }
    if (address.protocol() != QAbstractSocket::IPv6Protocol) {
        return address.toString();
    }

    // The first 64 bits; the rest is the interface, chosen freely by the host.
    Q_IPV6ADDR bytes = address.toIPv6Address();
    std::fill(std::begin(bytes.c) + 8, std::end(bytes.c), quint8(0));
    return QHostAddress(bytes).toString() + u"/64";
}

QcConnectionLimiter::QcConnectionLimiter(const Limits &limits)
    : m_limits(limits)
{
}

bool QcConnectionLimiter::admit(const QString &address, qint64 nowMs)
{
    if (m_open >= m_limits.total || m_openPerAddress.value(address) >= m_limits.perAddress) {
        return false;
    }

    if (!m_buckets.contains(address) && m_buckets.size() >= maxTracked) {
        forgetFullBuckets(nowMs);
    }
    // A new address starts with a full bucket.
    auto bucket = m_buckets.find(address);
    if (bucket == m_buckets.end()) {
        bucket = m_buckets.insert(address, {m_limits.burst, nowMs});
    }

    const double refill = double(std::max<qint64>(0, nowMs - bucket->refilledAt)) * 1e-3 * m_limits.perSecond;
    bucket->tokens = std::min(m_limits.burst, bucket->tokens + refill);
    bucket->refilledAt = nowMs;
    if (bucket->tokens < 1.0) {
        return false;
    }

    bucket->tokens -= 1.0;
    ++m_openPerAddress[address];
    ++m_open;
    return true;
}

void QcConnectionLimiter::closed(const QString &address)
{
    const auto found = m_openPerAddress.find(address);
    if (found == m_openPerAddress.end()) {
        return;
    }

    --m_open;
    if (--*found <= 0) {
        m_openPerAddress.erase(found);
    }
}

void QcConnectionLimiter::forgetFullBuckets(qint64 nowMs)
{
    m_buckets.removeIf([this, nowMs](QHash<QString, Bucket>::iterator it) {
        const double tokens = it->tokens + double(std::max<qint64>(0, nowMs - it->refilledAt)) * 1e-3 * m_limits.perSecond;
        return tokens >= m_limits.burst && !m_openPerAddress.contains(it.key());
    });

    // Under a flood from ever new addresses nothing is full: the table starts over.
    if (m_buckets.size() >= maxTracked) {
        m_buckets.clear();
    }
}
