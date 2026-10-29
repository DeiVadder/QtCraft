// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QFile>
#include <QString>

// One log file with a size cap (decision E-834): lines are appended unbuffered, so a crash loses
// none of them; when the file would grow beyond the cap it becomes the rotated part 1, older parts
// move up by one, the oldest beyond `rotations` is deleted and a new file starts. Not thread-safe:
// QcMessageLog writes under its mutex.
class QcLogWriter
{
public:
    struct Limits
    {
        qint64 maxBytes = qint64{20} * 1024 * 1024;
        // Rotated parts kept besides the current file.
        int rotations = 2;
    };

    QcLogWriter(QString path, Limits limits);

    // Opens the file for appending (making its folder); the error says why it cannot.
    [[nodiscard]] QcStatus open();
    void write(const QByteArray &line);
    // Whether writing `bytes` more starts a new file (closing the current one and its handle).
    bool rotatesFor(qsizetype bytes) const { return m_file.isOpen() && m_size > 0 && m_size + bytes > m_limits.maxBytes; }
    QString path() const { return m_path; }
    // The operating system's handle of the open file (for the crash handler); -1 when closed.
    int handle() const { return m_file.isOpen() ? m_file.handle() : -1; }
    // Rotations so far (tests).
    int rotationCount() const { return m_rotationCount; }

private:
    void rotate();

    QString m_path;
    Limits m_limits;
    QFile m_file;
    qint64 m_size = 0;
    int m_rotationCount = 0;
};
