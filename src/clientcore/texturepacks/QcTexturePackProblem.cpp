#include "clientcore/texturepacks/QcTexturePackProblem.h"

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
    const QString filledText = filled(QString::fromUtf8(message), arguments);
    return file.isEmpty() ? filledText : u"%1: %2"_s.arg(file, filledText);
}

QString QcTexturePackProblem::text() const
{
    const QString filledText = filled(QCoreApplication::translate("QcTexturePack", message), arguments);
    return file.isEmpty() ? filledText : u"%1: %2"_s.arg(file, filledText);
}
