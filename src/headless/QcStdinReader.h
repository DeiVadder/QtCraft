#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>

#include <memory>

// Reads standard input line by line on a background thread and delivers the lines to the owner's
// thread. Blocking reads keep the event loop free and work the same on every platform (a socket
// notifier on stdin does not work on Windows).
class QcStdinReader : public QObject
{
    Q_OBJECT

public:
    explicit QcStdinReader(QObject *parent = nullptr);
    ~QcStdinReader() override;

    void start();

signals:
    void lineReceived(const QByteArray &line);
    void endOfInput();

private:
    struct Shared;

    void deliver();

    std::shared_ptr<Shared> m_shared;
};
