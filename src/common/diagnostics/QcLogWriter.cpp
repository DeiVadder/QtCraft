// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcLogWriter.h"

#include "common/diagnostics/QcLogFiles.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

QcLogWriter::QcLogWriter(QString path, Limits limits)
    : m_path(std::move(path))
    , m_limits(limits)
    , m_file(m_path)
{
}

QcStatus QcLogWriter::open()
{
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        return qcError(u"Cannot create the folder of %1"_s.arg(m_path));
    }
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered)) {
        return qcError(u"Cannot open %1: %2"_s.arg(m_path, m_file.errorString()));
    }
    m_size = m_file.size();
    return {};
}

void QcLogWriter::write(const QByteArray &line)
{
    if (!m_file.isOpen()) {
        return;
    }
    if (rotatesFor(line.size())) {
        rotate();
    }
    const qint64 written = m_file.write(line);
    m_size += std::max<qint64>(written, 0);
}

void QcLogWriter::rotate()
{
    m_file.close();
    if (m_limits.rotations > 0) {
        QFile::remove(QcLogFiles::rotatedPath(m_path, m_limits.rotations));
        for (int part = m_limits.rotations - 1; part >= 1; --part) {
            QFile::rename(QcLogFiles::rotatedPath(m_path, part), QcLogFiles::rotatedPath(m_path, part + 1));
        }
        if (!QFile::rename(m_path, QcLogFiles::rotatedPath(m_path, 1))) {
            // Held open elsewhere (a viewer on Windows): go on in the same file rather than lose it.
            if (m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered)) {
                m_size = 0;
            }
            return;
        }
    }
    ++m_rotationCount;
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Unbuffered)) {
        return;
    }
    m_size = 0;
    if (m_limits.rotations > 0) {
        m_size = m_file.write("(continued from " + QcLogFiles::rotatedPath(m_path, 1).toUtf8() + ")\n");
    }
}
