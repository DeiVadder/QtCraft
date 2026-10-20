#include "clientcore/session/QcClientSignEditor.h"

bool QcClientSignEditor::handle(const QcMessage &message)
{
    const auto *open = std::get_if<QcMsg::OpenSignEditor>(&message);
    if (!open) {
        return false;
    }

    m_open = true;
    m_pos = open->pos;
    m_lines = open->lines;
    emit opened();
    return true;
}

bool QcClientSignEditor::write(const QcSignText::Lines &lines)
{
    if (!m_open) {
        return false;
    }

    m_open = false;
    if (m_send) {
        m_send(QcMsg::EditSign{m_pos, lines});
    }
    emit closed();
    return true;
}
