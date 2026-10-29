// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/auth/QcOwnerOnlyAccess.h"

#ifdef Q_OS_WIN
#include <QByteArray>
#include <QDir>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>

#include <string>

namespace {

// The security identifier of the user running the program; empty when it cannot be read.
QByteArray currentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(static_cast<qsizetype>(size), '\0');
    const bool read = size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    CloseHandle(token);
    if (!read) {
        return {};
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the buffer holds a TOKEN_USER
    const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.constData());
    return {static_cast<const char *>(user->User.Sid), static_cast<qsizetype>(GetLengthSid(user->User.Sid))};
}

bool isBroadGroup(PSID sid)
{
    for (const WELL_KNOWN_SID_TYPE group : {WinWorldSid, WinBuiltinUsersSid, WinAuthenticatedUserSid}) {
        if (IsWellKnownSid(sid, group)) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace QcOwnerOnlyAccess {

bool restrictToOwner(const QString &path)
{
    QByteArray sid = currentUserSid();
    if (sid.isEmpty()) {
        return false;
    }

    EXPLICIT_ACCESS_W access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): with TRUSTEE_IS_SID the name field holds the SID
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid.data());
    PACL list = nullptr;
    if (SetEntriesInAclW(1, &access, nullptr, &list) != ERROR_SUCCESS) {
        return false;
    }

    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD result = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                               nullptr, nullptr, list, nullptr);
    LocalFree(list);
    return result == ERROR_SUCCESS;
}

bool othersMayRead(const QString &path)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    PACL list = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(native.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &list, nullptr, &descriptor)
        != ERROR_SUCCESS) {
        return false;
    }

    // No access list at all lets everybody in.
    bool open = list == nullptr;
    constexpr ACCESS_MASK reading = FILE_READ_DATA | GENERIC_READ | GENERIC_ALL;
    for (DWORD index = 0; !open && index < list->AceCount; ++index) {
        void *entry = nullptr;
        if (!GetAce(list, index, &entry) || static_cast<const ACE_HEADER *>(entry)->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            continue;
        }
        auto *allowed = static_cast<ACCESS_ALLOWED_ACE *>(entry);
        open = (allowed->Mask & reading) != 0 && isBroadGroup(&allowed->SidStart);
    }
    LocalFree(descriptor);
    return open;
}

} // namespace QcOwnerOnlyAccess

#else

namespace QcOwnerOnlyAccess {

bool restrictToOwner(const QString &path)
{
    Q_UNUSED(path);
    return true;
}

bool othersMayRead(const QString &path)
{
    Q_UNUSED(path);
    return false;
}

} // namespace QcOwnerOnlyAccess

#endif
