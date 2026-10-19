#pragma once

#include "common/content/loader/QcBaseContent.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QTest>
#include <QtLogging>

#include <memory>
#include <source_location>

// QVERIFY2 evaluates its message even when the check passes, so reading error() of a successful
// std::expected there is undefined behaviour. This returns the message only for failures.
template<typename Result>
QByteArray qcErrorText(const Result &result)
{
    return result.has_value() ? QByteArray() : result.error().toUtf8();
}

// Helpers shared by the tests of every topic.
namespace QcTestUtil {

// The base content pack alone (no mods), as the game loads it before mods. The content is loaded
// in member initialisers and fixture setup, where a failed check cannot end a single test case, and
// no case can run without it, so a pack that does not load cleanly stops the test binary.
inline std::shared_ptr<QcContentRegistry> loadContent()
{
    QStringList warnings;
    QcResult<std::shared_ptr<QcContentRegistry>> content = QcBaseContent::load(warnings);
    if (!content) {
        qFatal("The base content does not load: %s", qPrintable(content.error()));
    }
    if (!warnings.isEmpty()) {
        qFatal("The base content has warnings: %s", qPrintable(warnings.join(u'\n')));
    }
    return std::move(*content);
}

// Fails the running test case from a helper that has to hand back a value at once, such as a
// lookup by name inside an expression: the case is marked failed with `message` and goes on with the
// neutral value the helper returns (its later checks may fail as well), and the other cases still run.
inline void failCase(const QString &message, std::source_location where = std::source_location::current())
{
    QTest::qFail(qPrintable(message), where.file_name(), int(where.line()));
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
