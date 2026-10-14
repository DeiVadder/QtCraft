#pragma once

#include "QcContentRegistry.h"
#include "QcItemStack.h"

#include <QDataStream>
#include <QSet>
#include <QString>
#include <QStringList>

// Reads item stacks of one save file by name (written by QcSaveFile::writeStack); an unknown item is
// dropped with one warning per name, counts and damage are clamped to what the item allows.
class QcStackReader
{
public:
    QcStackReader(const QcContentRegistry &content, QString path, QStringList &warnings);

    // The stack of one slot; empty for an empty slot or an unknown item.
    QcItemStack read(QDataStream &stream);

private:
    const QcContentRegistry &m_content;
    QString m_path;
    QStringList &m_warnings;
    QSet<QString> m_unknown;
};
