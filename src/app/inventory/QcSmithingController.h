// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

struct QcSessionParts;

// The smithing table's screen for QML (GameController.smithing, decision E-1115): whether there is
// an output to take, the template the screen suggests in its empty first slot, and forge(). The
// template, base, addition and output slots themselves are rows of the inventory model
// (GameController.player.inventory); the server decides and sends everything back.
class QcSmithingController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool open READ isOpen NOTIFY changed FINAL)
    // Whether there is an output to take.
    Q_PROPERTY(bool hasOutput READ hasOutput NOTIFY changed FINAL)
    // The item the empty template slot shows as a pale placeholder: the template of the first
    // smithing recipe; empty without recipes.
    Q_PROPERTY(QString templateItem READ templateItem NOTIFY changed FINAL)
    // The handbook page of the screen's block (the screen's handbook button).
    Q_PROPERTY(QString handbookTarget READ handbookTarget CONSTANT FINAL)

public:
    explicit QcSmithingController(const QcSessionParts &parts, QObject *parent = nullptr);

    bool isOpen() const;
    bool hasOutput() const { return m_hasOutput; }
    QString templateItem() const { return m_templateItem; }
    void setHasOutput(bool hasOutput) { m_hasOutput = hasOutput; }
    static QString handbookTarget() { return QStringLiteral("qtcraft:smithing_table"); }

    // Takes the output into the base slot.
    Q_INVOKABLE void forge() const;
    // The screen changed.
    void update();

signals:
    void changed();

private:
    const QcSessionParts &m_parts;
    bool m_hasOutput = false;
    QString m_templateItem;
};
