#pragma once

#include <QObject>
#include <QStringList>

#include <memory>

class QcClientSession;
class QcControlInterface;
class QcLocalServerHost;
class QFile;

// Server and control-interface client in one process without any window: the base of the
// scenario tests and of external automation. Usage: see `qtcraft-headless --help`.
class QcHeadlessGame : public QObject
{
    Q_OBJECT

public:
    explicit QcHeadlessGame(QObject *parent = nullptr);
    ~QcHeadlessGame() override;

    // Parses the command line and starts; returns false (after printing the reason) on bad options.
    bool start(const QStringList &arguments);

private:
    void finishScript();

    std::unique_ptr<QcLocalServerHost> m_server;
    QcClientSession *m_session = nullptr;
    QcControlInterface *m_control = nullptr;
    QFile *m_output = nullptr;
    bool m_scriptMode = false;
};
