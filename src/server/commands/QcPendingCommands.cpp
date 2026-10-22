#include "server/commands/QcPendingCommands.h"

void QcPendingCommands::ask(const QcCommandContext &context, Action action, qint64 nowNs)
{
    m_pending.insert(context.source.id(), {std::move(action), context.permission, nowNs + timeoutNs});
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Type /confirm within %1 s to go ahead or /cancel to drop it"),
                  {QString::number(timeoutSeconds)});
}

bool QcPendingCommands::confirm(const QcCommandSource &source, qint64 nowNs)
{
    if (!m_pending.contains(source.id())) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Nothing to confirm"));
        return false;
    }

    // Taken out first: the action may ask again.
    const Pending pending = m_pending.take(source.id());
    if (!source.may(pending.needed)) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "You may no longer run the command waiting for /confirm"));
        return false;
    }
    if (nowNs > pending.expiresNs) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Too late: confirm within %1 s, run the command again"),
                     {QString::number(timeoutSeconds)});
        return false;
    }

    pending.action(source);
    return true;
}

bool QcPendingCommands::cancel(const QcCommandSource &source)
{
    const bool dropped = m_pending.remove(source.id());
    source.reply(dropped ? QT_TRANSLATE_NOOP("QcServerMessages", "Cancelled") : QT_TRANSLATE_NOOP("QcServerMessages", "Nothing to confirm"));
    return dropped;
}
