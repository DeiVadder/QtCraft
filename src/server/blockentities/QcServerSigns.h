#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcSignMessages.h"

struct QcServerClient;
class QcServerState;

// Players writing on signs (decision E-766). Placing a sign or using one (not sneaking, within
// reach, not in adventure mode) opens its editor on the player's client (QcMsg::OpenSignEditor);
// the player's EditSign is taken only for the sign whose editor the server opened for him, within
// reach and the Sign rate limit, and its lines are cleaned (QcSignText::cleaned: no control or
// format characters, the block's number of lines and line length). The text lives in QcSignTexts;
// clients learn it after the chunk (sendChunk) and whenever it changes (announceChanges).
// Holds no state of its own; made where it is needed, like QcBlockDyeing.
class QcServerSigns
{
public:
    explicit QcServerSigns(QcServerState &state);

    // Opens the editor of the sign at `pos` for `client` (the caller checked sneaking); false when
    // there is no sign or the player may not write on it (then other uses of the click may follow).
    bool openEditor(QcServerClient &client, QcBlockPos pos);
    // The lines the player wrote; ignored unless they belong to the sign whose editor is open.
    void edit(QcServerClient &client, const QcMsg::EditSign &message);
    // Sends the texts of the signs in chunk `pos` to `client` (right after the chunk's blocks).
    void sendChunk(QcServerClient &client, QcChunkPos pos) const;
    // Tells the clients that have a chunk about the signs in it whose text changed.
    void announceChanges();

private:
    // Whether `client` may write on the sign block at `pos` now.
    bool mayWrite(const QcServerClient &client, QcBlockPos pos) const;
    QcMsg::SignText textAt(QcBlockPos pos) const;

    QcServerState &m_state;
};
