#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcItemStack.h"
#include "server/storage/QcLoadWarnings.h"

#include <QDataStream>
#include <QString>

// Reads item stacks of one save file by name (written by QcSaveFile::writeStack); an unknown item is
// dropped with one warning per name (QcLoadWarnings::addOnce), counts and damage are clamped to
// what the item allows.
class QcStackReader
{
public:
    QcStackReader(const QcContentRegistry &content, QString path, QcLoadWarnings &warnings);

    // The stack of one slot; empty for an empty slot or an unknown item.
    QcItemStack read(QDataStream &stream);

private:
    const QcContentRegistry &m_content;
    QString m_path;
    QcLoadWarnings &m_warnings;
};
