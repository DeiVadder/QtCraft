// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcScreenState;
struct QcSignDefinition;
struct QcSessionParts;

// The editor of a sign for QML (GameController.sign, decision E-766): the sign's lines (the draft:
// what the server opened it with, then as typed), how many lines it has and how long they may be,
// edit(), which keeps a line as it is typed (the text holds at once, docs/UI-Leitfaden.md 5
// pattern 1: closing the editor in any way sends it), and write(), which sends the lines and
// closes the screen. The server cleans the lines again; what it keeps comes back to every player
// as the sign's text.
class QcSignController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QStringList lines READ lines NOTIFY changed FINAL)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY changed FINAL)
    Q_PROPERTY(int lineLength READ lineLength NOTIFY changed FINAL)

public:
    QcSignController(const QcSessionParts &parts, QcScreenState &screens, QObject *parent = nullptr);

    // The draft of the open sign's lines.
    QStringList lines() const;
    int lineCount() const;
    int lineLength() const;

    // Sends `lines` (at most lineCount, cut to lineLength characters) for the open sign and closes
    // the editor.
    Q_INVOKABLE void write(const QStringList &lines);
    // Keeps `text` (cut to lineLength characters) as line `line` of the open sign's draft without
    // closing the editor. The lines property reads the draft but does not notify the change, so the
    // fields are not filled again while they are typed into.
    Q_INVOKABLE void edit(int line, const QString &text);

signals:
    void changed();

private:
    // The sign data of the block being edited; null when it is no sign (any more).
    const QcSignDefinition *definition() const;

    const QcSessionParts &m_parts;
    QcScreenState &m_screens;
};
