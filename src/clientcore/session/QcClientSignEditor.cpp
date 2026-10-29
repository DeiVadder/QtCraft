#include "clientcore/session/QcClientSignEditor.h"

#include "clientcore/debug/QcClientLogging.h"

using namespace Qt::StringLiterals;

bool QcClientSignEditor::handle(const QcMessage &message)
{
    const auto *open = std::get_if<QcMsg::OpenSignEditor>(&message);
    if (!open) {
        return false;
    }

    m_pos = open->pos;
    setOpen(true);
    setLines(open->lines);
    emit opened();
    return true;
}

bool QcClientSignEditor::write(const QcSignText::Lines &lines)
{
    if (!isOpen()) {
        return false;
    }

    setOpen(false);
    // One line per edit, for tests and bug reports (the server cleans and may refuse them).
    qCInfo(lcSession).noquote() << u"Sign at %1 %2 %3: sending \"%4\""_s.arg(pos().x).arg(pos().y).arg(pos().z).arg(QStringList(lines.begin(), lines.end()).join(u'|'));
    if (messageSender()) {
        messageSender()(QcMsg::EditSign{pos(), lines});
    }
    emit closed();
    return true;
}

void QcClientSignEditor::setLines(const QcSignText::Lines &lines)
{
    // Only the open editor has lines to edit; a late edit after closing changes nothing.
    if (isOpen()) {
        m_lines = lines;
    }
}
