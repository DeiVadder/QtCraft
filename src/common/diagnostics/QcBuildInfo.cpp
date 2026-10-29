// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcBuildInfo.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSysInfo>
#include <QThread>

#if defined(Q_OS_DARWIN)
#include <sys/sysctl.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#elif defined(Q_OS_WIN)
#include <QSettings>
#endif

#include <array>

using namespace Qt::StringLiterals;

// CMake defines both for this file only (src/common/CMakeLists.txt), so a new commit recompiles
// nothing else.
#ifndef QC_BUILD_VERSION
#define QC_BUILD_VERSION "0.0.0"
#endif
#ifndef QC_BUILD_COMMIT
#define QC_BUILD_COMMIT "unknown"
#endif

namespace {

QString compiler()
{
#if defined(__clang__)
    return u"Clang "_s + QString::fromLatin1(__clang_version__).section(u' ', 0, 0);
#elif defined(_MSC_VER)
    return u"MSVC "_s + QString::number(_MSC_FULL_VER);
#elif defined(__GNUC__)
    return u"GCC "_s + QString::fromLatin1(__VERSION__);
#else
    return u"unknown compiler"_s;
#endif
}

} // namespace

namespace QcBuildInfo {

QString version()
{
    return QStringLiteral(QC_BUILD_VERSION);
}

QString gitCommit()
{
    return QStringLiteral(QC_BUILD_COMMIT);
}

QString describe(const QString &program)
{
    return u"%1 %2 (git %3)"_s.arg(program, version(), gitCommit());
}

QString clientText(const QString &program)
{
    return u"%1 %2 (%3)"_s.arg(describe(program), QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
}

QString cpuName()
{
#if defined(Q_OS_DARWIN)
    std::array<char, 256> name{};
    std::size_t size = name.size();
    if (sysctlbyname("machdep.cpu.brand_string", name.data(), &size, nullptr, 0) != 0) {
        return {};
    }
    return QString::fromLatin1(name.data()).trimmed();
#elif defined(Q_OS_LINUX)
    QFile info(u"/proc/cpuinfo"_s);
    if (!info.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    while (!info.atEnd()) {
        const QByteArray line = info.readLine();
        if (line.startsWith("model name")) {
            return QString::fromLatin1(line.mid(line.indexOf(':') + 1)).trimmed();
        }
    }
    return {};
#elif defined(Q_OS_WIN)
    const QSettings processor(u"HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"_s, QSettings::NativeFormat);
    return processor.value(u"ProcessorNameString"_s).toString().trimmed();
#else
    return {};
#endif
}

QStringList headerLines(const QString &program)
{
    const QString cpu = cpuName();
    return {
        u"%1, built with %2 and Qt %3, running on Qt %4"_s.arg(describe(program), compiler(), QStringLiteral(QT_VERSION_STR),
                                                               QString::fromLatin1(qVersion())),
        u"System: %1 (%2 %3), %4"_s.arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                                         QSysInfo::buildAbi()),
        u"Processor: %1, %2 logical cores"_s.arg(cpu.isEmpty() ? QSysInfo::currentCpuArchitecture() : cpu).arg(QThread::idealThreadCount()),
        u"Process %1 started %2"_s.arg(QCoreApplication::applicationPid()).arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)),
    };
}

} // namespace QcBuildInfo
