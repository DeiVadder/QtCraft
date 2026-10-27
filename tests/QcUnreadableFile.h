#pragma once

#include <QDir>
#include <QFile>
#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Makes a file unreadable while it lives and readable again at the end. POSIX takes the read
// right; Windows has no such right in Qt (only the read-only attribute), so the file is held open
// without sharing there, which makes every other open fail, writing included (decision E-901).
class QcUnreadableFile
{
public:
    explicit QcUnreadableFile(const QString &path)
        : m_path(path)
#ifdef Q_OS_WIN
        , m_handle(openUnshared(path))
        , m_valid(m_handle != INVALID_HANDLE_VALUE)
#else
        , m_valid(QFile::setPermissions(path, QFileDevice::WriteOwner))
#endif
    {
    }

    ~QcUnreadableFile()
    {
#ifdef Q_OS_WIN
        if (m_valid) {
            CloseHandle(m_handle);
        }
#else
        QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
    }

    QcUnreadableFile(const QcUnreadableFile &) = delete;
    QcUnreadableFile &operator=(const QcUnreadableFile &) = delete;

    [[nodiscard]] bool isValid() const { return m_valid; }

private:
#ifdef Q_OS_WIN
    static HANDLE openUnshared(const QString &path)
    {
        const QString native = QDir::toNativeSeparators(path);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): QString stores UTF-16, which is wchar_t on Windows
        return CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    }
#endif

    QString m_path;
#ifdef Q_OS_WIN
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#endif
    bool m_valid = false;
};
