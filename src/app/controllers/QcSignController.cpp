// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcSignController.h"

#include "app/controllers/QcScreenState.h"
#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"

#include <algorithm>

QcSignController::QcSignController(const QcSessionParts &parts, QcScreenState &screens, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_screens(screens)
{
    // The editor opens with the overlay; its lines are read then.
    connect(&screens, &QcScreenState::overlayChanged, this, &QcSignController::changed);
}

QStringList QcSignController::lines() const
{
    if (!m_parts.session) {
        return {};
    }
    const QcSignText::Lines &lines = m_parts.session->signEditor().lines();
    return QStringList(lines.begin(), lines.begin() + lineCount());
}

int QcSignController::lineCount() const
{
    const QcSignDefinition *sign = definition();
    return sign ? sign->lines : QcSignText::maxLines;
}

int QcSignController::lineLength() const
{
    const QcSignDefinition *sign = definition();
    return sign ? sign->lineLength : QcSignText::maxLineLength;
}

const QcSignDefinition *QcSignController::definition() const
{
    const QcClientSession *session = m_parts.session.get();
    const QcContentRegistry *content = session ? session->content().get() : nullptr;
    const std::optional<QcBlockId> block = content && session->world() ? session->world()->blockAt(session->signEditor().pos()) : std::nullopt;
    const bool sign = block && content->isValidBlock(*block) && content->block(*block).sign.isSign();
    return sign ? &content->block(*block).sign : nullptr;
}

void QcSignController::write(const QStringList &lines)
{
    if (!m_parts.session || !m_parts.session->signEditor().isOpen()) {
        qCWarning(lcGame) << "Ignoring sign text: no sign editor is open";
        return;
    }

    QcSignText::Lines written;
    const int count = std::min<int>(static_cast<int>(lines.size()), lineCount());
    for (int line = 0; line < count; ++line) {
        written[static_cast<std::size_t>(line)] = lines[line];
    }
    m_parts.session->signEditor().write(QcSignText::cleaned(written, lineCount(), lineLength()));
    m_screens.setOverlay(QcScreenState::Overlay::None);
}

void QcSignController::edit(int line, const QString &text)
{
    if (!m_parts.session || !m_parts.session->signEditor().isOpen()) {
        qCWarning(lcGame) << "Ignoring sign line" << line << ": no sign editor is open";
        return;
    }
    if (line < 0 || line >= lineCount()) {
        qCWarning(lcGame) << "Ignoring sign line" << line << ": the sign has" << lineCount() << "lines";
        return;
    }

    QcSignText::Lines lines = m_parts.session->signEditor().lines();
    lines[static_cast<std::size_t>(line)] = text.left(lineLength());
    m_parts.session->signEditor().setLines(lines);
}
