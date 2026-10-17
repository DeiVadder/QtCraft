#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"

#include <QDataStream>
#include <QSet>
#include <QString>
#include <QStringList>

// Reads item stacks of one save file by name (written by QcSaveFile::writeStack); an unknown item is
// dropped with one warning per name, counts and damage are clamped to what the item allows.
class QcStackReader
{
public:
    // `reported`: unknown names warned about before (one warning per world); without it, per reader.
    QcStackReader(const QcContentRegistry &content, QString path, QStringList &warnings, QSet<QString> *reported = nullptr);

    // The stack of one slot; empty for an empty slot or an unknown item.
    QcItemStack read(QDataStream &stream);

private:
    const QcContentRegistry &m_content;
    QString m_path;
    QStringList &m_warnings;
    QSet<QString> m_ownUnknown;
    QSet<QString> &m_unknown;
};
