// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/loader/QcBaseContent.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QRandomGenerator>
#include <QTest>
#include <QtLogging>

#include <memory>
#include <source_location>

// QVERIFY2 evaluates its message even when the check passes, so reading error() of a successful
// std::expected there is undefined behaviour. This returns the message only for failures.
template<typename Result>
QByteArray qcErrorText(const Result &result)
{
    if (result.has_value()) {
        return {};
    }
    // A failure with more than a message (QcPlayerStorage::Failure) names it in `error`.
    if constexpr (requires { result.error().error; }) {
        return result.error().error.toUtf8();
    } else {
        return result.error().toUtf8();
    }
}

// Helpers shared by the tests of every topic.
namespace QcTestUtil {

// The base content pack alone (no mods), as the game loads it before mods, with its warnings, or why
// it does not load.
inline QcResult<QcBaseContent::Loaded> baseContent()
{
    return QcBaseContent::load();
}

// The base content for tests that cannot run without it. The content is loaded in member
// initialisers and fixture setup, where a failed check cannot end a single test case, so a pack
// that does not load or loads with warnings stops the test binary.
inline std::shared_ptr<QcContentRegistry> loadContent()
{
    QcResult<QcBaseContent::Loaded> content = baseContent();
    if (!content) {
        qFatal("The base content does not load: %s", qPrintable(content.error()));
    }
    if (!content->warnings.isEmpty()) {
        qFatal("The base content has warnings: %s", qPrintable(content->warnings.join(u'\n')));
    }
    return std::move(content->registry);
}

// The random seed of a world generation test's run besides its fixed ones (decision E-846): the
// value of QC_TEST_SEED if set, else drawn; written to the log either way, so a red run repeats.
inline qint64 randomSeed()
{
    bool ok = false;
    const qint64 fixed = qEnvironmentVariable("QC_TEST_SEED").toLongLong(&ok);
    const qint64 seed = ok ? fixed : static_cast<qint64>(QRandomGenerator::global()->generate() & 0x7fffffff);
    qInfo("random seed %lld (set QC_TEST_SEED to repeat)", seed);
    return seed;
}

// Fails the running test case from a helper that has to hand back a value at once, such as a
// lookup by name inside an expression: the case is marked failed with `message` and goes on with the
// neutral value the helper returns (its later checks may fail as well), and the other cases still run.
inline void failCase(const QString &message, std::source_location where = std::source_location::current())
{
    QTest::qFail(qPrintable(message), where.file_name(), static_cast<int>(where.line()));
}

// Delivers the messages queued between a server and an in-process client.
inline void pump()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

// Runs `count` ticks of a server with an in-process client, delivering the messages after each
// (a template, so that tests without a server need not link one).
template<typename Server>
void ticks(Server &server, int count)
{
    for (int i = 0; i < count; ++i) {
        server.tick();
        pump();
    }
}

} // namespace QcTestUtil
