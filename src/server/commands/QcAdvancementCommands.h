#pragma once

#include "server/commands/QcCommandRegistry.h"

struct QcServerClient;
class QcServerState;

// /advancement (decision E-923), for operators: grants or revokes an advancement of a player (every
// criterion of it), or all of them with "everything", and shows what a player has reached. The client
// hears of the change in the next tick (QcServerAdvancements::tick); a grant shows the toast.
class QcAdvancementCommands
{
public:
    explicit QcAdvancementCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);

private:
    void advancement(const QcCommandContext &context);
    // Grants or revokes `target` ("everything" or a name) for `player`; the number changed, or none
    // for an unknown name.
    [[nodiscard]] std::optional<int> change(QcServerClient &player, const QString &target, bool grant) const;
    void query(const QcCommandContext &context, const QcServerClient &player, const QString &target) const;

    QcServerState &m_state;
};
