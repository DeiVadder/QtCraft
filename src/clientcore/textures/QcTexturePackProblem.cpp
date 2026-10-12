#include "QcTexturePackProblem.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace {

QString filled(QString text, const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        text = text.arg(argument);
    }
    return text;
}

} // namespace

QString QcTexturePackProblem::englishText() const
{
    const QString message = filled(QString::fromUtf8(this->message), arguments);
    return file.isEmpty() ? message : u"%1: %2"_s.arg(file, message);
}

QString QcTexturePackProblem::text() const
{
    const QString message = filled(QCoreApplication::translate("QcTexturePack", this->message), arguments);
    return file.isEmpty() ? message : u"%1: %2"_s.arg(file, message);
}
