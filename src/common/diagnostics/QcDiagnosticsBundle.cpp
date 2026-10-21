#include "common/diagnostics/QcDiagnosticsBundle.h"

#include "common/diagnostics/QcBuildInfo.h"
#include "common/diagnostics/QcLogFiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace {

bool isSecretKey(const QString &key)
{
    static const QRegularExpression secret(u"password|secret|token|passphrase"_s, QRegularExpression::CaseInsensitiveOption);
    return secret.match(key).hasMatch();
}

QJsonValue cleanValue(const QJsonValue &value)
{
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            it.value() = isSecretKey(it.key()) ? QJsonValue(u"<hidden>"_s) : cleanValue(it.value());
        }
        return object;
    }
    if (value.isArray()) {
        QJsonArray array;
        const QJsonArray elements = value.toArray();
        for (const auto &element : elements) {
            array.append(cleanValue(element));
        }
        return array;
    }
    return value;
}

QcStatus writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    return {};
}

// The names of the folders in `path`, as lines of info.txt.
QStringList folderNames(const QString &title, const QString &path)
{
    const QStringList names = QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    return {u"%1 (%2): %3"_s.arg(title, QDir::toNativeSeparators(path), names.isEmpty() ? u"none"_s : names.join(u", "_s))};
}

// Copies the files of the newest sessions and all crash reports and dumps of <data>/logs; returns
// the lines for info.txt: how many older files stayed behind, and each file that could not be copied
// (pruned meanwhile, locked).
QStringList copyLogs(const QString &dataDirectory, const QDir &target)
{
    const QDir logs(QDir(dataDirectory).filePath(QString::fromLatin1(QcLogFiles::folderName)));
    const QStringList names = QcLogFiles::bundleFiles(logs, QcDiagnosticsBundle::keptSessions);
    QStringList lines;
    if (const qsizetype older = logs.entryList(QDir::Files).size() - names.size(); older > 0) {
        lines.append(u"Older log files left out: %1 (in %2)"_s.arg(older).arg(QDir::toNativeSeparators(logs.path())));
    }
    for (const QString &name : names) {
        if (!QFile::copy(logs.filePath(name), target.filePath(name))) {
            lines.append(u"Not copied: "_s + QDir::toNativeSeparators(logs.filePath(name)));
        }
    }
    return lines;
}

// settings.ini and server.json of the data folder, cleaned, where they exist.
QcStatus copySettings(const QDir &data, const QDir &target)
{
    QFile settings(data.filePath(u"settings.ini"_s));
    if (settings.open(QIODevice::ReadOnly)) {
        if (const QcStatus written = writeFile(target.filePath(u"settings.ini"_s), QcDiagnosticsBundle::cleanSettings(settings.readAll())); !written) {
            return written;
        }
    }
    QFile server(data.filePath(u"server.json"_s));
    if (server.open(QIODevice::ReadOnly)) {
        return writeFile(target.filePath(u"server.json"_s), QcDiagnosticsBundle::cleanServerSettings(server.readAll()));
    }
    return {};
}

} // namespace

namespace QcDiagnosticsBundle {

QcResult<QString> write(const Contents &contents)
{
    const QDir data(contents.dataDirectory);
    const QString name = u"%1-%2"_s.arg(contents.program, QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s));
    const QString base = QDir(data.filePath(QString::fromLatin1(folderName))).filePath(name);
    QString path = base;
    for (int counter = 2; QFileInfo::exists(path); ++counter) {
        path = u"%1-%2"_s.arg(base).arg(counter);
    }
    if (!QDir().mkpath(path)) {
        return qcError(u"Cannot create %1"_s.arg(path));
    }
    const QDir target(path);
    const QStringList notCopied = copyLogs(contents.dataDirectory, target);
    if (const QcStatus copied = copySettings(data, target); !copied) {
        return qcError(copied.error());
    }

    QStringList info = QcBuildInfo::headerLines(contents.program);
    info += folderNames(u"Mods"_s, data.filePath(u"mods"_s));
    info += folderNames(u"Worlds"_s, data.filePath(u"worlds"_s));
    info += contents.info;
    info += notCopied;
    if (const QcStatus written = writeFile(target.filePath(u"info.txt"_s), (info.join(u'\n') + u'\n').toUtf8()); !written) {
        return qcError(written.error());
    }
    return path;
}

QByteArray cleanSettings(const QByteArray &ini)
{
    QByteArrayList lines = ini.split('\n');
    for (QByteArray &line : lines) {
        const qsizetype equals = line.indexOf('=');
        if (equals > 0 && isSecretKey(QString::fromUtf8(line.left(equals)))) {
            line = line.left(equals + 1) + "<hidden>";
        }
    }
    return lines.join('\n');
}

QByteArray cleanServerSettings(const QByteArray &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return "(server.json is not a JSON object and was left out)\n";
    }
    return QJsonDocument(cleanValue(document.object()).toObject()).toJson();
}

} // namespace QcDiagnosticsBundle
