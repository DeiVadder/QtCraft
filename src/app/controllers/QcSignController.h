#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcScreenState;
struct QcSignDefinition;
struct QcSessionParts;

// The editor of a sign for QML (GameController.sign, decision E-766): the sign's lines when the
// server opened it, how many lines it has and how long they may be, and write(), which sends the
// lines and closes the screen. The server cleans the lines again; what it keeps comes back to every
// player as the sign's text.
class QcSignController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QStringList lines READ lines NOTIFY changed FINAL)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY changed FINAL)
    Q_PROPERTY(int lineLength READ lineLength NOTIFY changed FINAL)

public:
    QcSignController(const QcSessionParts &parts, QcScreenState &screens, QObject *parent = nullptr);

    QStringList lines() const;
    int lineCount() const;
    int lineLength() const;

    // Sends `lines` (at most lineCount, cut to lineLength characters) for the open sign and closes
    // the editor.
    Q_INVOKABLE void write(const QStringList &lines);

signals:
    void changed();

private:
    // The sign data of the block being edited; null when it is no sign (any more).
    const QcSignDefinition *definition() const;

    const QcSessionParts &m_parts;
    QcScreenState &m_screens;
};
