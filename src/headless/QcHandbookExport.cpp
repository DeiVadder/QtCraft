#include "headless/QcHandbookExport.h"

#include "clientcore/icons/QcItemIconSet.h"
#include "clientcore/icons/QcWebHandbookAssets.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/gameplay/QcKeyBindings.h"
#include "common/handbook/builder/QcHandbookBuilder.h"
#include "common/handbook/QcHandbookMarkdown.h"
#include "common/handbook/QcHandbookWeb.h"
#include "server/gameplay/QcHandbookRules.h"

#include <QSet>

#include <algorithm>
#include <print>

using namespace Qt::StringLiterals;

namespace {

using QcHeadlessOptions::printError;

// The loaded content with --mods-dir, else the base game alone.
std::shared_ptr<const QcContentRegistry> documented(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options,
                                                    const std::shared_ptr<const QcContentRegistry> &content)
{
    if (parser.isSet(options.mods)) {
        return content;
    }
    QStringList baseWarnings;
    QcResult<std::shared_ptr<QcContentRegistry>> base = QcBaseContent::load(baseWarnings);
    if (!base) {
        printError(u"Cannot load the base game: "_s + base.error());
        return nullptr;
    }
    return *base;
}

QcHandbook build(const QcContentRegistry &content, const QString &language)
{
    QcHandbook book = QcHandbookBuilder::build(content, language, QcHandbookRules::values(),
                                               QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
    for (const QString &problem : book.problems()) {
        printError(u"handbook problem (%1): %2"_s.arg(language, problem));
    }
    return book;
}

// The languages of the content, English first.
QStringList languages(const QcContentRegistry &content)
{
    QStringList list = content.languages();
    list.removeAll(u"en"_s);
    std::ranges::sort(list);
    list.prepend(u"en"_s);
    return list;
}

} // namespace

namespace QcHandbookExport {

int markdown(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options, const std::shared_ptr<const QcContentRegistry> &content)
{
    const std::shared_ptr<const QcContentRegistry> exported = documented(parser, options, content);
    if (!exported) {
        return 1;
    }

    const QcHandbook book = build(*exported, parser.value(options.language));
    const QcStatus written = QcHandbookMarkdown::write(book, parser.value(options.handbook));
    if (!written) {
        printError(written.error());
        return 1;
    }
    std::println("{} entries in {} files", book.entries().size(), QcHandbookMarkdown::files(book).size());
    return 0;
}

int web(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options, const std::shared_ptr<const QcContentRegistry> &content)
{
    const std::shared_ptr<const QcContentRegistry> exported = documented(parser, options, content);
    if (!exported) {
        return 1;
    }

    const QStringList ids = languages(*exported);
    QList<QcHandbookWeb::File> files{QcHandbookWeb::languages(ids), QcWebHandbookAssets::theme()};
    QSet<QString> shown;
    qsizetype entries = 0;
    for (const QString &language : ids) {
        const QcHandbook book = build(*exported, language);
        files.append(QcHandbookWeb::data(book));
        const QStringList icons = QcHandbookWeb::icons(book);
        shown.unite(QSet<QString>(icons.begin(), icons.end()));
        entries += book.entries().size();
    }

    const QcTextureAtlas atlas = QcTextureAtlas::build(*exported);
    const QcBlockRenderTable table(*exported, atlas);
    const QHash<QString, QImage> icons = QcItemIconSet::draw(*exported, atlas, &table, QcWebHandbookAssets::iconSize);
    bool complete = true;
    for (const QString &name : std::as_const(shown)) {
        if (!icons.contains(name)) {
            printError(u"handbook problem: no icon for %1"_s.arg(name));
            complete = false;
        }
    }
    files.append(QcWebHandbookAssets::iconSheet(icons));

    const QcStatus written = QcHandbookWeb::write(files, parser.value(options.handbookWeb));
    if (!written) {
        printError(written.error());
        return 1;
    }
    std::println("{} languages, {} entries, {} icons in {} files", ids.size(), entries, icons.size(), files.size());
    return complete ? 0 : 1;
}

} // namespace QcHandbookExport
