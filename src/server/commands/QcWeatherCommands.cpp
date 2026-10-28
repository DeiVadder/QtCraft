#include "server/commands/QcWeatherCommands.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"
#include "server/weather/QcServerWeather.h"

#include <QtMath>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// One key per kind, so the client translates the whole sentence (decision E-094).
const char *nowKey(QcWeatherKind kind)
{
    switch (kind) {
    case QcWeatherKind::Clear:
        return QT_TRANSLATE_NOOP("QcServerMessages", "The weather is clear");
    case QcWeatherKind::Rain:
        return QT_TRANSLATE_NOOP("QcServerMessages", "It is raining");
    case QcWeatherKind::Thunder:
        return QT_TRANSLATE_NOOP("QcServerMessages", "A thunderstorm is raging");
    }
    return QT_TRANSLATE_NOOP("QcServerMessages", "The weather is clear");
}

const char *setKey(QcWeatherKind kind)
{
    switch (kind) {
    case QcWeatherKind::Clear:
        return QT_TRANSLATE_NOOP("QcServerMessages", "Set the weather to clear for %1 s");
    case QcWeatherKind::Rain:
        return QT_TRANSLATE_NOOP("QcServerMessages", "Set the weather to rain for %1 s");
    case QcWeatherKind::Thunder:
        return QT_TRANSLATE_NOOP("QcServerMessages", "Set the weather to thunder for %1 s");
    }
    return QT_TRANSLATE_NOOP("QcServerMessages", "Set the weather to clear for %1 s");
}

constexpr const char *usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /weather [clear|rain|thunder] [seconds]");

QString seconds(qint64 ticks)
{
    return QString::number(qRound64(static_cast<double>(ticks) * QcPhysics::tickDuration));
}

} // namespace

QcWeatherCommands::QcWeatherCommands(QcServerState &state, QcServerWeather &weather)
    : m_state(state)
    , m_weather(weather)
{
}

void QcWeatherCommands::addTo(QcCommandRegistry &registry)
{
    // Everyone may ask for the weather; changing it checks for an operator.
    registry.addBuiltIn({.name = u"weather"_s, .category = QcCommandCategory::World, .permission = QcPermission::Player,
                         .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows the weather; operators set it, for a number of seconds or as long as it would last."),
                         .run = [this](const QcCommandContext &context) { weather(context); }});
}

void QcWeatherCommands::weather(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    if (args.isEmpty()) {
        report(context);
        return;
    }

    const std::optional<QcWeatherKind> kind = QcWeather::parse(args[0]);
    bool ok = kind.has_value() && args.size() <= 2;
    std::optional<qint64> ticks;
    if (ok && args.size() == 2) {
        const double wanted = args[1].toDouble(&ok);
        const double maxSeconds = static_cast<double>(QcWeatherCycle::maxSetTicks) * QcPhysics::tickDuration;
        ok = ok && std::isfinite(wanted) && wanted >= 1.0 && wanted <= maxSeconds;
        ticks = ok ? std::optional<qint64>(qRound64(wanted / QcPhysics::tickDuration)) : std::nullopt;
    }
    if (!ok) {
        context.reply(usage);
        return;
    }
    if (!context.source.may(QcPermission::Operator)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Only operators can use /%1"), {u"weather"_s});
        return;
    }

    m_weather.set(*kind, ticks);
    context.reply(setKey(*kind), {seconds(m_weather.state().ticksLeft)});
    if (!m_state.info().weatherCycle) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "weatherCycle is false: the weather stays until it is set again"));
    }
}

void QcWeatherCommands::report(const QcCommandContext &context) const
{
    const QcWeatherState &state = m_weather.state();
    context.reply(nowKey(state.kind));
    if (m_state.info().weatherCycle && state.spell > 0) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The weather changes in %1 s"), {seconds(state.ticksLeft)});
    }
}
