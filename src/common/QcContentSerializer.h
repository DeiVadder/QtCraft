#pragma once

#include "QcResult.h"

#include <QDataStream>

class QcContentRegistry;

// Wire format of the content sync message: everything a client needs to show the world.
// World-generation data stays on the server. Reading treats the stream as untrusted.
class QcContentSerializer
{
public:
    static void write(const QcContentRegistry &content, QDataStream &stream);
    static QcResult<QcContentRegistry> read(QDataStream &stream);

private:
    static QcStatus readBlocks(QDataStream &stream, QcContentRegistry &registry);
};
