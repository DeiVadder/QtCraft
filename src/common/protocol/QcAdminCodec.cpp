// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payloads of the admin channel (QcAdminMessages.h, decision E-848). Every count and text is
// bounded (QcMsg::AdminLimits); a decoder that finds more fails the message.

#include "common/protocol/QcProtocolCodec.h"

#include "common/core/QcCommandCategory.h"
#include "common/protocol/QcStreamLimits.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
namespace Limits = QcMsg::AdminLimits;

namespace {

void writeText(QDataStream &stream, const QString &text, qsizetype limit = Limits::maxText)
{
    stream << text.left(limit);
}

// A text of at most `limit` characters; a longer one turns the stream corrupt.
QString readText(QDataStream &stream, qsizetype limit = Limits::maxText)
{
    QString text;
    stream >> text;
    if (text.size() > limit) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return {};
    }
    return text;
}

void writeTexts(QDataStream &stream, const QStringList &texts)
{
    stream << static_cast<quint32>(texts.size());
    for (const QString &text : texts) {
        writeText(stream, text);
    }
}

QStringList readTexts(QDataStream &stream, quint32 limit)
{
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, limit);
    QStringList texts;
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        texts.append(readText(stream));
    }
    return texts;
}

// Times and rates are finite and not negative; the rest of a sample are counts.
bool validNumber(float value)
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0e12f;
}

void writeSample(QDataStream &stream, const QcMsg::AdminSample &s)
{
    stream << s.timeMs << s.tickMeanMs << s.tickP99Ms << s.tickMaxMs << s.ticksPerSecond << s.players << s.loadedChunks << s.entities << s.memoryMb
           << s.sentKbPerSecond << s.receivedKbPerSecond;
}

QcMsg::AdminSample readSample(QDataStream &stream)
{
    QcMsg::AdminSample s;
    stream >> s.timeMs >> s.tickMeanMs >> s.tickP99Ms >> s.tickMaxMs >> s.ticksPerSecond >> s.players >> s.loadedChunks >> s.entities >> s.memoryMb
        >> s.sentKbPerSecond >> s.receivedKbPerSecond;
    for (const float value : {s.tickMeanMs, s.tickP99Ms, s.tickMaxMs, s.ticksPerSecond, s.memoryMb, s.sentKbPerSecond, s.receivedKbPerSecond}) {
        if (!validNumber(value)) {
            stream.setStatus(QDataStream::ReadCorruptData);
        }
    }
    return s;
}

void writeSamples(QDataStream &stream, const QList<QcMsg::AdminSample> &samples)
{
    stream << static_cast<quint32>(samples.size());
    for (const QcMsg::AdminSample &sample : samples) {
        writeSample(stream, sample);
    }
}

QList<QcMsg::AdminSample> readSamples(QDataStream &stream)
{
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Limits::maxHistory);
    QList<QcMsg::AdminSample> samples;
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        samples.append(readSample(stream));
    }
    return samples;
}

void writeChat(QDataStream &stream, const QcMsg::ChatMessage &line)
{
    writeText(stream, line.text, Limits::maxLongText);
    writeText(stream, line.key, Limits::maxLongText);
    const QStringList arguments = line.arguments.mid(0, QcMsg::ChatMessage::maxArguments);
    stream << static_cast<quint32>(arguments.size());
    for (const QString &argument : arguments) {
        writeText(stream, argument, Limits::maxLongText);
    }
}

QcMsg::ChatMessage readChat(QDataStream &stream)
{
    QcMsg::ChatMessage line;
    line.text = readText(stream, Limits::maxLongText);
    line.key = readText(stream, Limits::maxLongText);
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, QcMsg::ChatMessage::maxArguments);
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        line.arguments.append(readText(stream, Limits::maxLongText));
    }
    return line;
}

[[nodiscard]] QcResult<QcMessage> checked(QDataStream &stream, QcMessage message, const char *what)
{
    if (stream.status() != QDataStream::Ok) {
        return qcError(u"Invalid %1"_s.arg(QLatin1StringView(what)));
    }
    return message;
}

void writeSetting(QDataStream &stream, const QcMsg::AdminSettings::Setting &s)
{
    writeText(stream, s.key);
    writeText(stream, s.group);
    writeText(stream, s.description, Limits::maxLongText);
    stream << static_cast<quint8>(s.type) << s.low << s.high << s.defaultValue << s.value;
}

QList<QcMsg::AdminSettings::Setting> readSettings(QDataStream &stream)
{
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Limits::maxSettings);
    QList<QcMsg::AdminSettings::Setting> settings;
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        QcMsg::AdminSettings::Setting s;
        s.key = readText(stream);
        s.group = readText(stream);
        s.description = readText(stream, Limits::maxLongText);
        quint8 type = 0;
        stream >> type >> s.low >> s.high >> s.defaultValue >> s.value;
        if (type > static_cast<quint8>(QcMsg::AdminSettings::Type::Integer) || s.low > s.high) {
            stream.setStatus(QDataStream::ReadCorruptData);
        }
        s.type = static_cast<QcMsg::AdminSettings::Type>(type);
        settings.append(s);
    }
    return settings;
}

void writePlayer(QDataStream &stream, const QcMsg::AdminPlayers::Entry &p)
{
    using QcProtocolCodec::operator<<;
    stream << p.entityId;
    writeText(stream, p.name);
    stream << p.pingMs << p.position << static_cast<quint8>(p.gameMode) << p.operatorRights << p.local << p.connectedMs;
    writeText(stream, p.address);
}

QcMsg::AdminPlayers::Entry readPlayer(QDataStream &stream)
{
    using QcProtocolCodec::operator>>;
    QcMsg::AdminPlayers::Entry p;
    stream >> p.entityId;
    p.name = readText(stream);
    quint8 mode = 0;
    stream >> p.pingMs >> p.position >> mode >> p.operatorRights >> p.local >> p.connectedMs;
    p.address = readText(stream);
    const std::optional<QcGameMode> gameMode = QcGameModes::fromValue(mode);
    if (!gameMode || !QcProtocolCodec::isFinite(p.position) || p.pingMs < -1 || p.connectedMs < 0) {
        stream.setStatus(QDataStream::ReadCorruptData);
    }
    p.gameMode = gameMode.value_or(QcGameMode::Survival);
    return p;
}

} // namespace

namespace QcProtocolCodec {

void encode(QDataStream &stream, QcMsg::AdminSubscribe m)
{
    stream << m.topics << m.logLevel;
}

void encode(QDataStream &stream, const QcMsg::AdminRequest &m)
{
    stream << m.id << static_cast<quint8>(m.kind);
    writeTexts(stream, m.arguments);
}

void encode(QDataStream &stream, const QcMsg::AdminResult &m)
{
    stream << m.id << m.ok << static_cast<quint8>(m.refusal) << static_cast<quint32>(m.lines.size());
    for (const QcMsg::ChatMessage &line : m.lines) {
        writeChat(stream, line);
    }
}

void encode(QDataStream &stream, const QcMsg::AdminStatus &m)
{
    writeSample(stream, m.sample);
    for (const float ms : m.phaseMeanMs) {
        stream << ms;
    }
    for (const quint32 count : m.entities) {
        stream << count;
    }
    stream << m.peakMemoryMb << m.uptimeMs << m.tick << m.skippedTicks;
    writeText(stream, m.worldName);
    stream << m.seed;
    writeText(stream, m.generator);
    writeText(stream, m.version);
    stream << m.protocol;
}

void encode(QDataStream &stream, const QcMsg::AdminHistory &m)
{
    writeSamples(stream, m.seconds);
    writeSamples(stream, m.minutes);
}

void encode(QDataStream &stream, const QcMsg::AdminPlayers &m)
{
    stream << static_cast<quint32>(m.players.size());
    for (const QcMsg::AdminPlayers::Entry &player : m.players) {
        writePlayer(stream, player);
    }
}

void encode(QDataStream &stream, const QcMsg::AdminChat &m)
{
    stream << m.history << static_cast<quint32>(m.lines.size());
    for (const QcMsg::AdminChatLine &line : m.lines) {
        stream << line.timeMs;
        writeText(stream, line.sender);
        writeChat(stream, line.message);
    }
}

void encode(QDataStream &stream, const QcMsg::AdminCatalogue &m)
{
    stream << static_cast<quint32>(m.commands.size());
    for (const QcMsg::AdminCatalogue::Command &command : m.commands) {
        writeText(stream, command.name);
        writeTexts(stream, command.aliases);
        stream << command.permission << command.needsPlayer << command.macro;
        writeText(stream, command.usage, Limits::maxLongText);
        writeText(stream, command.description, Limits::maxLongText);
        stream << command.category;
        writeTexts(stream, command.names);
    }
}

void encode(QDataStream &stream, const QcMsg::AdminLists &m)
{
    stream << m.whitelistEnabled;
    writeTexts(stream, m.operators);
    writeTexts(stream, m.whitelist);
    stream << static_cast<quint32>(m.bans.size());
    for (const QcMsg::AdminLists::Ban &ban : m.bans) {
        writeText(stream, ban.name);
        writeText(stream, ban.reason);
        writeText(stream, ban.by);
        stream << ban.sinceMs;
    }
}

void encode(QDataStream &stream, const QcMsg::AdminSettings &m)
{
    for (const QList<QcMsg::AdminSettings::Setting> *list : {&m.rules, &m.limits}) {
        stream << static_cast<quint32>(list->size());
        for (const QcMsg::AdminSettings::Setting &setting : *list) {
            writeSetting(stream, setting);
        }
    }
    stream << m.limitsFromServer;
}

void encode(QDataStream &stream, const QcMsg::AdminLog &m)
{
    stream << m.tail << static_cast<quint32>(m.lines.size());
    for (const QcMsg::AdminLogLine &line : m.lines) {
        stream << line.timeMs << static_cast<quint8>(line.level);
        writeText(stream, line.category);
        writeText(stream, line.text, Limits::maxLongText);
    }
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminSubscribe>, QDataStream &stream)
{
    QcMsg::AdminSubscribe m;
    stream >> m.topics >> m.logLevel;
    if ((m.topics & ~QcMsg::AdminSubscribe::all) != 0 || m.logLevel >= QcMsg::AdminLogLine::levelCount) {
        return qcError(u"Unknown admin topics"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminRequest>, QDataStream &stream)
{
    QcMsg::AdminRequest m;
    quint8 kind = 0;
    stream >> m.id >> kind;
    if (kind >= QcMsg::AdminRequest::kindCount) {
        return qcError(u"Unknown admin request"_s);
    }
    m.kind = static_cast<QcMsg::AdminRequest::Kind>(kind);
    m.arguments = readTexts(stream, Limits::maxArguments);
    return checked(stream, m, "admin request");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminResult>, QDataStream &stream)
{
    QcMsg::AdminResult m;
    quint8 refusal = 0;
    stream >> m.id >> m.ok >> refusal;
    if (refusal >= QcMsg::AdminResult::refusalCount) {
        return qcError(u"Unknown admin refusal"_s);
    }
    m.refusal = static_cast<QcMsg::AdminResult::Refusal>(refusal);
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Limits::maxLines);
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        m.lines.append(readChat(stream));
    }
    return checked(stream, m, "admin result");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminStatus>, QDataStream &stream)
{
    QcMsg::AdminStatus m;
    m.sample = readSample(stream);
    for (float &ms : m.phaseMeanMs) {
        stream >> ms;
        if (!validNumber(ms)) {
            stream.setStatus(QDataStream::ReadCorruptData);
        }
    }
    for (quint32 &count : m.entities) {
        stream >> count;
    }
    stream >> m.peakMemoryMb >> m.uptimeMs >> m.tick >> m.skippedTicks;
    m.worldName = readText(stream);
    stream >> m.seed;
    m.generator = readText(stream);
    m.version = readText(stream);
    stream >> m.protocol;
    if (!validNumber(m.peakMemoryMb)) {
        stream.setStatus(QDataStream::ReadCorruptData);
    }
    return checked(stream, m, "admin status");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminHistory>, QDataStream &stream)
{
    QcMsg::AdminHistory m;
    m.seconds = readSamples(stream);
    m.minutes = readSamples(stream);
    return checked(stream, m, "admin history");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminPlayers>, QDataStream &stream)
{
    QcMsg::AdminPlayers m;
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Limits::maxPlayers);
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        m.players.append(readPlayer(stream));
    }
    return checked(stream, m, "admin players");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminChat>, QDataStream &stream)
{
    QcMsg::AdminChat m;
    stream >> m.history;
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Limits::maxLines);
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        QcMsg::AdminChatLine line;
        stream >> line.timeMs;
        line.sender = readText(stream);
        line.message = readChat(stream);
        m.lines.append(line);
    }
    return checked(stream, m, "admin chat");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminCatalogue>, QDataStream &stream)
{
    QcMsg::AdminCatalogue m;
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Limits::maxCommands);
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        QcMsg::AdminCatalogue::Command command;
        command.name = readText(stream);
        command.aliases = readTexts(stream, Limits::maxAliases);
        stream >> command.permission >> command.needsPlayer >> command.macro;
        command.usage = readText(stream, Limits::maxLongText);
        command.description = readText(stream, Limits::maxLongText);
        stream >> command.category;
        command.names = readTexts(stream, Limits::maxNames);
        if (command.permission > 2 || !QcCommandCategories::fromWire(command.category)) {
            stream.setStatus(QDataStream::ReadCorruptData);
        }
        m.commands.append(command);
    }
    return checked(stream, m, "admin catalogue");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminLists>, QDataStream &stream)
{
    QcMsg::AdminLists m;
    stream >> m.whitelistEnabled;
    m.operators = readTexts(stream, Limits::maxEntries);
    m.whitelist = readTexts(stream, Limits::maxEntries);
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Limits::maxEntries);
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        QcMsg::AdminLists::Ban ban;
        ban.name = readText(stream);
        ban.reason = readText(stream);
        ban.by = readText(stream);
        stream >> ban.sinceMs;
        m.bans.append(ban);
    }
    return checked(stream, m, "admin lists");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminSettings>, QDataStream &stream)
{
    QcMsg::AdminSettings m;
    m.rules = readSettings(stream);
    m.limits = readSettings(stream);
    stream >> m.limitsFromServer;
    return checked(stream, m, "admin settings");
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminLog>, QDataStream &stream)
{
    QcMsg::AdminLog m;
    stream >> m.tail;
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, Limits::maxLogLines);
    for (quint32 i = 0; count && i < *count && stream.status() == QDataStream::Ok; ++i) {
        QcMsg::AdminLogLine line;
        quint8 level = 0;
        stream >> line.timeMs >> level;
        line.category = readText(stream);
        line.text = readText(stream, Limits::maxLongText);
        if (level >= QcMsg::AdminLogLine::levelCount) {
            stream.setStatus(QDataStream::ReadCorruptData);
        }
        line.level = static_cast<QcMsg::AdminLogLine::Level>(level);
        m.lines.append(line);
    }
    return checked(stream, m, "admin log");
}

} // namespace QcProtocolCodec
