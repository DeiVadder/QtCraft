// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/inventory/QcSmithingController.h"

#include "app/debug/QcAppLogging.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"

QcSmithingController::QcSmithingController(const QcSessionParts &parts, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
{
}

bool QcSmithingController::isOpen() const
{
    return m_parts.session && m_parts.session->smithing().isOpen();
}

void QcSmithingController::forge() const
{
    if (!isOpen() || !hasOutput()) {
        qCWarning(lcGame) << "Smithing: nothing to take (open" << isOpen() << "output" << hasOutput() << ")";
        return;
    }
    m_parts.session->smithing().forge();
}

void QcSmithingController::update()
{
    const bool open = isOpen();
    setHasOutput(open && !m_parts.session->smithing().state().output.isEmpty());
    const auto content = open ? m_parts.session->content() : nullptr;
    m_templateItem = content && !content->smithingRecipes().isEmpty() ? content->smithingRecipes().first().templateItem : QString();
    emit changed();
}
