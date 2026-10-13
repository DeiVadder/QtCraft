#include "QcServerSettings.h"

#include "QcProfileValidation.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 maxFileBytes = qint64(1024) * 1024;

// Reads an integer key within [low, high]; an absent key keeps `value`.
bool readInt(const QJsonObject &json, const QString &key, int low, int high, int &value)
{
    if (!json.contains(key)) {
        return true;
    }
    const double number = json.value(key).toDouble(-1e300);
    if (!json.value(key).isDouble() || number != std::floor(number) || number < low || number > high) {
        return false;
    }
    value = int(number);
    return true;
}

bool readText(const QJsonObject &json, const QString &key, qsizetype maxLength, QString &value)
{
    if (!json.contains(key)) {
        return true;
    }
    if (!json.value(key).isString() || json.value(key).toString().size() > maxLength) {
        return false;
    }
    value = json.value(key).toString();
    return true;
}

QString invalid(const QString &key)
{
    return u"server.json: invalid value of \"%1\""_s.arg(key);
}

// Operator names must be valid player names as they are (QcProfileValidation), not cleaned into
// another name or into "Player".
QcStatus readOperators(const QJsonObject &json, QStringList &operators)
{
    if (json.contains(u"operators"_s) && !json.value(u"operators"_s).isArray()) {
        return qcError(invalid(u"operators"_s));
    }

    const QJsonArray names = json.value(u"operators"_s).toArray();
    for (const auto &name : names) {
        const std::optional<QString> valid = QcProfileValidation::validPlayerName(name.toString());
        if (!name.isString() || valid != name.toString()) {
            const QString shown = QString::fromUtf8(QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact));
            return qcError(u"server.json: invalid operator name %1 in \"operators\""_s.arg(shown));
        }
        operators.append(*valid);
    }
    return {};
}

} // namespace

QcResult<QcServerSettings> QcServerSettings::read(const QString &path)
{
    QFile file(path);
    if (!file.exists()) {
        return QcServerSettings{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxFileBytes) {
        return qcError(u"Cannot read %1"_s.arg(path));
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return qcError(u"%1 is no valid JSON object: %2"_s.arg(path, error.errorString()));
    }
    return fromJson(document.object());
}

QcResult<QcServerSettings> QcServerSettings::fromJson(const QJsonObject &json)
{
    QcServerSettings settings;
    int port = settings.port;
    struct Range
    {
        QString key;
        int low;
        int high;
        int *value;
    };
    const std::array<Range, 6> ranges{{{u"port"_s, 0, 65535, &port},
                                       {u"maxPlayers"_s, 1, maxPlayersLimit, &settings.maxPlayers},
                                       {u"viewDistance"_s, 1, maxViewDistance, &settings.viewDistance},
                                       {u"loginTimeoutSeconds"_s, 1, 600, &settings.loginTimeoutSeconds},
                                       {u"connectionBurst"_s, 1, 1000, &settings.connectionBurst},
                                       {u"maxConnectionsPerAddress"_s, 1, 1000, &settings.maxConnectionsPerAddress}}};
    for (const Range &range : ranges) {
        if (!readInt(json, range.key, range.low, range.high, *range.value)) {
            return qcError(u"server.json: \"%1\" must be a whole number from %2 to %3"_s.arg(range.key).arg(range.low).arg(range.high));
        }
    }
    settings.port = quint16(port);

    for (const auto &[key, value] : {std::pair{u"serverName"_s, &settings.serverName}, std::pair{u"motd"_s, &settings.motd},
                                     std::pair{u"bindAddress"_s, &settings.bindAddress}, std::pair{u"password"_s, &settings.password},
                                     std::pair{u"world"_s, &settings.world}, std::pair{u"worldType"_s, &settings.worldType}}) {
        if (!readText(json, key, 256, *value)) {
            return qcError(invalid(key));
        }
    }

    // A number, or its text (seeds need all 64 bits, JSON numbers have 53); empty for a random one.
    if (const QString seed = json.value(u"seed"_s).toVariant().toString(); !seed.isEmpty()) {
        bool ok = false;
        settings.seed = seed.toLongLong(&ok);
        if (!ok) {
            return qcError(invalid(u"seed"_s));
        }
    }
    if (json.contains(u"connectionsPerSecond"_s)) {
        settings.connectionsPerSecond = json.value(u"connectionsPerSecond"_s).toDouble(-1.0);
        if (!(settings.connectionsPerSecond > 0.0 && settings.connectionsPerSecond <= 1000.0)) {
            return qcError(invalid(u"connectionsPerSecond"_s));
        }
    }
    if (const QString mode = json.value(u"defaultGameMode"_s).toString(); !mode.isEmpty()) {
        settings.defaultGameMode = QcGameModes::parse(mode);
        if (!settings.defaultGameMode) {
            return qcError(u"server.json: \"defaultGameMode\" must be survival, creative or empty"_s);
        }
    } else if (json.contains(u"defaultGameMode"_s) && !json.value(u"defaultGameMode"_s).isString()) {
        return qcError(invalid(u"defaultGameMode"_s));
    }
    if (json.contains(u"limits"_s) && !json.value(u"limits"_s).isObject()) {
        return qcError(invalid(u"limits"_s));
    }
    settings.limits = QcServerLimits::fromJson(json.value(u"limits"_s).toObject(), u"server.json"_s, &settings.limitWarnings);
    if (const QcStatus operators = readOperators(json, settings.operators); !operators) {
        return qcError(operators.error());
    }

    if (!settings.address()) {
        return qcError(invalid(u"bindAddress"_s));
    }
    return settings;
}

QJsonObject QcServerSettings::toJson() const
{
    return {{u"format"_s, formatVersion},
            {u"serverName"_s, serverName},
            {u"motd"_s, motd},
            {u"port"_s, port},
            {u"bindAddress"_s, bindAddress},
            {u"maxPlayers"_s, maxPlayers},
            {u"password"_s, password},
            {u"world"_s, world},
            {u"seed"_s, seed ? QString::number(*seed) : QString()},
            {u"worldType"_s, worldType},
            {u"viewDistance"_s, viewDistance},
            {u"loginTimeoutSeconds"_s, loginTimeoutSeconds},
            {u"connectionsPerSecond"_s, connectionsPerSecond},
            {u"connectionBurst"_s, connectionBurst},
            {u"maxConnectionsPerAddress"_s, maxConnectionsPerAddress},
            {u"operators"_s, QJsonArray::fromStringList(operators)},
            {u"defaultGameMode"_s, defaultGameMode ? QString(QcGameModes::name(*defaultGameMode)) : QString()},
            {u"limits"_s, limits.toJson()}};
}

QcStatus QcServerSettings::write(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(toJson()).toJson()) < 0) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    // It may hold the server password.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!file.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    return {};
}

QcResult<QHostAddress> QcServerSettings::address() const
{
    if (bindAddress.isEmpty()) {
        return QHostAddress(QHostAddress::Any);
    }
    QHostAddress parsed(bindAddress);
    if (parsed.isNull()) {
        return qcError(u"Invalid bind address '%1'"_s.arg(bindAddress));
    }
    return parsed;
}

void QcServerSettings::applyTo(QcServerConfig &config) const
{
    config.serverName = serverName;
    config.motd = motd;
    config.password = password;
    config.maxPlayers = maxPlayers;
    config.viewDistance = viewDistance;
    config.loginTimeoutMs = loginTimeoutSeconds * 1000;
    config.operators = operators;
    config.defaultGameMode = defaultGameMode;
    config.limits = limits;
    config.limitsFromServer = true;
}

QcNetworkConfig QcServerSettings::networkConfig() const
{
    QcNetworkConfig network;
    network.bindAddress = address().value_or(QHostAddress(QHostAddress::Any));
    network.port = port;
    network.connections.perSecond = connectionsPerSecond;
    network.connections.burst = double(connectionBurst);
    network.connections.perAddress = maxConnectionsPerAddress;
    network.connections.total = std::max(maxPlayers + 16, maxPlayers * 2);
    return network;
}
