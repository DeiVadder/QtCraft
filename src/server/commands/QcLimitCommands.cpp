#include "server/commands/QcLimitCommands.h"

#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace {

constexpr const char *usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /limits, /limits set <key> <value> or /limits reset <key>");

// The settings file of a dedicated server with the limit changed; the file's other keys stay.
[[nodiscard]] QcStatus writeServerLimit(const QString &path, const QString &key, std::optional<qint64> value)
{
    QJsonObject root;
    if (QFile file(path); file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            return qcError(u"Cannot read %1"_s.arg(path));
        }
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return qcError(u"%1 is no valid JSON object"_s.arg(path));
        }
        root = document.object();
    }

    QJsonObject limits = root.value(u"limits"_s).toObject();
    if (value) {
        limits.insert(key, double(*value));
    } else {
        limits.remove(key);
    }
    root.insert(u"limits"_s, limits);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(root).toJson()) < 0 || !out.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, out.errorString()));
    }
    return {};
}

} // namespace

QcLimitCommands::QcLimitCommands(QcServerState &state)
    : m_state(state)
{
}

void QcLimitCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"limits"_s,
                         .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages",
                                                          "Shows the simulation limits of the server (server.json or world.json, \"limits\"), "
                                                          "or sets one or resets it to its default."),
                         .run = [this](const QcCommandContext &context) { limits(context); }});
}

void QcLimitCommands::limits(const QcCommandContext &context)
{
    if (context.args.isEmpty()) {
        list(context);
    } else {
        change(context);
    }
}

void QcLimitCommands::list(const QcCommandContext &context) const
{
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Limits from %1:"),
                  {m_state.config().limitsFromServer ? u"server.json"_s : u"world.json"_s});
    const QStringList lines = m_state.config().limits.lines();
    for (const QString &line : lines) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "  %1"), {line});
    }
}

void QcLimitCommands::change(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    const bool reset = args[0] == u"reset"_s;
    if ((!reset && args[0] != u"set"_s) || args.size() != (reset ? 2 : 3)) {
        context.reply(usage);
        return;
    }
    const QcServerLimits::Field *field = QcServerLimits::field(args[1]);
    if (!field) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown limit: %1"), {args[1]});
        return;
    }
    bool number = true;
    const qint64 value = reset ? QcServerLimits{}.*(field->value) : args[2].toLongLong(&number);
    if (!number || value < field->low || value > field->high) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 must be a whole number from %2 to %3"),
                      {args[1], QString::number(field->low), QString::number(field->high)});
        return;
    }

    m_state.config().limits.*(field->value) = value;
    m_state.applyLimits();
    const QString key = QString::fromLatin1(field->key);
    qCInfo(lcCommands).noquote() << u"%1 set the limit %2 to %3"_s.arg(context.source.name(), key).arg(value);
    if (const QcStatus stored = store(key, reset ? std::nullopt : std::optional(value)); !stored) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 is now %2 until the server stops: %3"), {key, QString::number(value), stored.error()});
    } else if (m_state.config().limitsFromServer) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 is now %2 (written to server.json)"), {key, QString::number(value)});
    } else {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 is now %2 (kept in world.json with the next save)"), {key, QString::number(value)});
    }
}

QcStatus QcLimitCommands::store(const QString &key, std::optional<qint64> value)
{
    if (!m_state.config().limitsFromServer) {
        QJsonObject &own = m_state.info().limits;
        if (value) {
            own.insert(key, double(*value));
        } else {
            own.remove(key);
        }
        return {};
    }
    const QString &path = m_state.config().settingsFile;
    if (path.isEmpty()) {
        return qcError(u"the server has no settings file"_s);
    }
    return writeServerLimit(path, key, value);
}
