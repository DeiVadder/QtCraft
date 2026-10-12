#include "QcCommandArguments.h"

#include "QcAccessList.h"
#include "QcServerState.h"
#include "QcWorldConstants.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcCommandArguments {

QString qualified(const QString &name)
{
    return name.contains(u':') ? name : u"qtcraft:"_s + name;
}

std::optional<int> blockCoordinate(const QString &word, std::optional<int> origin)
{
    // Read and added in 64 bits, so neither the words nor the sum can overflow before the limit
    // check.
    bool ok = false;
    qint64 value = 0;
    if (!word.startsWith(u'~')) {
        value = word.toLongLong(&ok);
    } else if (origin) {
        const qint64 offset = word.size() > 1 ? QStringView(word).mid(1).toLongLong(&ok) : 0;
        ok = (word.size() == 1 || ok) && offset >= -2 * qint64(worldLimit) && offset <= 2 * qint64(worldLimit);
        value = qint64(*origin) + offset;
    }

    if (!ok || value < -worldLimit || value > worldLimit) {
        return std::nullopt;
    }
    return int(value);
}

std::optional<QcBlockPos> blockPosition(const QStringList &words, qsizetype first, std::optional<QcBlockPos> origin)
{
    if (words.size() < first + 3) {
        return std::nullopt;
    }

    const std::optional<int> x = blockCoordinate(words[first], origin ? std::optional<int>(origin->x) : std::nullopt);
    const std::optional<int> y = blockCoordinate(words[first + 1], origin ? std::optional<int>(origin->y) : std::nullopt);
    const std::optional<int> z = blockCoordinate(words[first + 2], origin ? std::optional<int>(origin->z) : std::nullopt);
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return QcBlockPos{*x, *y, *z};
}

namespace {

std::optional<double> pointCoordinate(const QString &word, std::optional<double> origin)
{
    bool ok = false;
    if (!word.startsWith(u'~')) {
        const double value = word.toDouble(&ok);
        return ok && std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
    }
    if (!origin) {
        return std::nullopt;
    }

    const double offset = word.size() > 1 ? QStringView(word).mid(1).toDouble(&ok) : 0.0;
    if (word.size() > 1 && (!ok || !std::isfinite(offset))) {
        return std::nullopt;
    }
    return *origin + offset;
}

} // namespace

std::optional<QcVec3> point(const QStringList &words, qsizetype first, std::optional<QcVec3> origin)
{
    if (words.size() < first + 3) {
        return std::nullopt;
    }

    const std::optional<double> x = pointCoordinate(words[first], origin ? std::optional<double>(origin->x) : std::nullopt);
    const std::optional<double> y = pointCoordinate(words[first + 1], origin ? std::optional<double>(origin->y) : std::nullopt);
    const std::optional<double> z = pointCoordinate(words[first + 2], origin ? std::optional<double>(origin->z) : std::nullopt);
    if (!x || !y || !z || std::abs(*x) > worldLimit || std::abs(*z) > worldLimit || *y < lowestTeleportY || *y > QcWorld::worldHeight) {
        return std::nullopt;
    }
    return QcVec3{*x, *y, *z};
}

std::optional<int> count(const QStringList &words, qsizetype index, int fallback, int low, int high)
{
    if (words.size() <= index) {
        return fallback;
    }

    bool ok = false;
    const int value = words[index].toInt(&ok);
    return ok && value >= low && value <= high ? std::optional<int>(value) : std::nullopt;
}

std::optional<bool> toggle(const QString &word)
{
    if (word == u"true"_s || word == u"on"_s) {
        return true;
    }
    if (word == u"false"_s || word == u"off"_s) {
        return false;
    }
    return std::nullopt;
}

QList<QcServerClient *> players(const QcServerState &state, const QString &name)
{
    const QString key = QcAccessList::key(name);
    QList<QcServerClient *> found;
    for (const auto &client : state.clients) {
        if (client->joined && client->state != QcServerClient::State::Closed && QcAccessList::key(client->name) == key) {
            found.append(client.get());
        }
    }
    return found;
}

} // namespace QcCommandArguments
