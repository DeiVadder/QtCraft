#include "QcHeadlessGame.h"

#include "QcBaseContent.h"
#include "QcClientSession.h"
#include "QcControlInterface.h"
#include "QcLocalServerHost.h"
#include "QcMapRenderer.h"
#include "QcStdinReader.h"
#include "QcTerrainGenerator.h"
#include "QcTransport.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>
#include <print>

using namespace Qt::StringLiterals;

namespace {

constexpr int defaultTimeLimitSeconds = 120;

void printError(const QString &message)
{
    std::println(stderr, "{}", message.toStdString());
}

} // namespace

QcHeadlessGame::QcHeadlessGame(QObject *parent)
    : QObject(parent)
{
}

QcHeadlessGame::~QcHeadlessGame()
{
    delete m_control;
    delete m_session;
    if (m_server) {
        m_server->shutdown();
    }
}

bool QcHeadlessGame::start(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"QtCraft headless server with a control-interface client."_s);
    parser.addHelpOption();
    const QCommandLineOption seedOption(u"seed"_s, u"World seed."_s, u"number"_s, u"0"_s);
    const QCommandLineOption worldOption(u"world"_s, u"World name."_s, u"name"_s, u"Headless"_s);
    const QCommandLineOption viewOption(u"view-distance"_s, u"Chunk radius streamed to the client."_s, u"chunks"_s, u"4"_s);
    const QCommandLineOption scriptOption(u"script"_s, u"Run JSON-lines commands from a file, then exit."_s, u"file"_s);
    const QCommandLineOption timeOption(u"time-limit"_s, u"Exit with failure after this many seconds."_s, u"seconds"_s,
                                        QString::number(defaultTimeLimitSeconds));
    const QCommandLineOption mapOption(u"map"_s, u"Write a top-down terrain map PNG and exit."_s, u"file"_s);
    const QCommandLineOption mapRadiusOption(u"map-radius"_s, u"Map radius in blocks."_s, u"blocks"_s, u"1024"_s);
    parser.addOptions({seedOption, worldOption, viewOption, scriptOption, timeOption, mapOption, mapRadiusOption});
    if (!parser.parse(arguments)) {
        printError(parser.errorText());
        return false;
    }
    if (parser.isSet(u"help"_s)) {
        std::print("{}", parser.helpText().toStdString());
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(0); });
        return true;
    }

    bool seedOk = false;
    QcServerConfig config;
    config.seed = parser.value(seedOption).toLongLong(&seedOk);
    config.worldName = parser.value(worldOption);
    bool viewOk = false;
    config.viewDistance = std::clamp(parser.value(viewOption).toInt(&viewOk), 1, 32);
    bool timeOk = false;
    const int timeLimit = parser.value(timeOption).toInt(&timeOk);
    if (!seedOk || !viewOk || !timeOk || timeLimit <= 0) {
        printError(u"Invalid numeric option"_s);
        return false;
    }

    QStringList warnings;
    QcResult<std::shared_ptr<QcContentRegistry>> content = QcBaseContent::load(warnings);
    for (const QString &warning : std::as_const(warnings)) {
        printError(u"content warning: "_s + warning);
    }
    if (!content) {
        printError(u"Cannot load content: "_s + content.error());
        return false;
    }
    const std::shared_ptr<const QcContentRegistry> shared = std::move(*content);
    QStringList generatorWarnings;
    auto generator = std::make_unique<QcTerrainGenerator>(shared, config.seed, &generatorWarnings);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        printError(u"world generation warning: "_s + warning);
    }
    if (parser.isSet(mapOption)) {
        const int radius = std::clamp(parser.value(mapRadiusOption).toInt(), 64, 8192);
        const QImage map = QcMapRenderer::render(*generator, *shared, 0, 0, radius, std::max(1, radius / 512));
        const bool saved = map.save(parser.value(mapOption));
        if (!saved) {
            printError(u"Cannot write map"_s);
        }
        QTimer::singleShot(0, qApp, [saved] { QCoreApplication::exit(saved ? 0 : 1); });
        return true;
    }
    m_server = std::make_unique<QcLocalServerHost>(config, shared, std::move(generator));

    m_session = new QcClientSession(m_server->connectClient(), u"Bot"_s);
    m_output = new QFile(this);
    // Opened by descriptor: a FILE*-based QFile would sit in stdio's buffer when stdout is a pipe.
    if (!m_output->open(fileno(stdout), QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        printError(u"Cannot open standard output"_s);
        return false;
    }

    m_control = new QcControlInterface(m_session, nullptr, m_output);
    m_scriptMode = parser.isSet(scriptOption);
    // A script's exit code reports whether every request succeeded, also when it ends with quit.
    if (m_scriptMode) {
        connect(m_control, &QcControlInterface::quitRequested, this, &QcHeadlessGame::finishScript, Qt::QueuedConnection);
    } else {
        connect(m_control, &QcControlInterface::quitRequested, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    }
    connect(m_session, &QcClientSession::disconnected, this, [](const QString &reason) {
        printError(u"disconnected: "_s + reason);
    });

    if (m_scriptMode) {
        QFile script(parser.value(scriptOption));
        if (!script.open(QIODevice::ReadOnly | QIODevice::Text)) {
            printError(u"Cannot open script %1"_s.arg(script.fileName()));
            return false;
        }
        connect(m_control, &QcControlInterface::idle, this, &QcHeadlessGame::finishScript, Qt::QueuedConnection);
        m_control->feed(script.readAll() + '\n');
    } else {
        auto *reader = new QcStdinReader(this);
        connect(reader, &QcStdinReader::lineReceived, m_control, &QcControlInterface::feed);
        connect(reader, &QcStdinReader::endOfInput, this, [this] {
            // End of input: finish the queued requests, then exit.
            if (m_control->isIdle()) {
                QCoreApplication::quit();
            } else {
                connect(m_control, &QcControlInterface::idle, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
            }
        });
        reader->start();
    }

    QTimer::singleShot(timeLimit * 1000, this, [] {
        printError(u"time limit reached"_s);
        QCoreApplication::exit(2);
    });
    m_session->start();
    return true;
}

void QcHeadlessGame::finishScript()
{
    if (!m_scriptMode) {
        return;
    }
    const int failed = m_control->failedRequests();
    QTimer::singleShot(0, qApp, [failed] { QCoreApplication::exit(failed == 0 ? 0 : 1); });
}
