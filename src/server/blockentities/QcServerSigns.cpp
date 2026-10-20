#include "server/blockentities/QcServerSigns.h"

#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"

QcServerSigns::QcServerSigns(QcServerState &state)
    : m_state(state)
{
}

bool QcServerSigns::openEditor(QcServerClient &client, QcBlockPos pos)
{
    if (!mayWrite(client, pos)) {
        return false;
    }

    client.screen.sign = pos;
    const QcSignTexts::Lines *lines = m_state.world().signs().at(pos);
    client.connection->send(QcMsg::OpenSignEditor{pos, lines ? *lines : QcSignTexts::Lines{}});
    return true;
}

void QcServerSigns::edit(QcServerClient &client, const QcMsg::EditSign &message)
{
    const bool opened = client.screen.sign == message.pos;
    client.screen.sign.reset();
    if (!opened || !mayWrite(client, message.pos)) {
        qCDebug(lcServer) << "Ignoring sign text from" << client.name << "at" << message.pos.x << message.pos.y << message.pos.z;
        // The client may have shown its own lines already: it gets the true text back.
        client.connection->send(textAt(message.pos));
        return;
    }

    // mayWrite() found the sign there.
    const QcSignDefinition &sign = m_state.content().block(m_state.world().blockAt(message.pos).value_or(qcAirBlockId)).sign;
    m_state.world().signs().set(message.pos, QcSignText::cleaned(message.lines, sign.lines, sign.lineLength));
}

void QcServerSigns::sendChunk(QcServerClient &client, QcChunkPos pos) const
{
    const QList<QcBlockPos> signs = m_state.world().signs().positionsIn(pos);
    for (const QcBlockPos &sign : signs) {
        client.connection->send(textAt(sign));
    }
}

void QcServerSigns::announceChanges()
{
    const QList<QcBlockPos> changes = m_state.world().signs().takeChanges();
    for (const QcBlockPos &pos : changes) {
        const QcMsg::SignText text = textAt(pos);
        for (const auto &client : m_state.clients()) {
            if (client->state == QcServerClient::State::Playing && client->view.sentChunks.contains(pos.chunk())) {
                client->connection->send(text);
            }
        }
    }
}

bool QcServerSigns::mayWrite(const QcServerClient &client, QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    // Adventure mode builds only by the world's allow-lists (decision E-735); writing counts as
    // building.
    return block && m_state.world().signs().isSign(*block) && !client.dead && client.rules().interacts && !client.rules().buildByAllowList
           && QcLineOfSight::isWithinReach(client, pos);
}

QcMsg::SignText QcServerSigns::textAt(QcBlockPos pos) const
{
    const QcSignTexts::Lines *lines = m_state.world().signs().at(pos);
    return {pos, lines ? *lines : QcSignTexts::Lines{}};
}
