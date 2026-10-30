// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QDataStream>

class QcContentRegistry;

/*! \brief Wire format of the content sync message: everything a client needs to show the world.

    World-generation data stays on the server. Reading treats the stream as untrusted. */
class QcContentSerializer
{
public:
    /*! Writes the client's part of \a content to \a stream. */
    static void write(const QcContentRegistry &content, QDataStream &stream);
    /*! Returns the content read from \a stream, or an error for a corrupt or truncated stream. */
    [[nodiscard]] static QcResult<QcContentRegistry> read(QDataStream &stream);
};
