#pragma once

#include "server/commands/QcCommandRegistry.h"

struct QcServerClient;
class QcServerState;

// /effect (decision E-450), for operators: gives a player a status effect for some seconds at a
// level, or takes one or all away. The client hears of it with the next tick (QcServerEffects).
class QcEffectCommands
{
public:
    // Seconds and level of /effect give without them.
    static constexpr int defaultSeconds = 30;

    explicit QcEffectCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);

private:
    void effect(const QcCommandContext &context);
    void give(const QcCommandContext &context, const QList<QcServerClient *> &targets);
    void clear(const QcCommandContext &context, const QList<QcServerClient *> &targets) const;

    QcServerState &m_state;
};
