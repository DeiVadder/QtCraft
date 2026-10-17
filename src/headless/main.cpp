#include "headless/QcHeadlessGame.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"qtcraft-headless"_s);
    QCoreApplication::setOrganizationName(u"QtCraft"_s);
    QcHeadlessGame game;
    if (!game.start(QCoreApplication::arguments())) {
        return 1;
    }
    return QCoreApplication::exec();
}
