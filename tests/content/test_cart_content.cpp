#include "QcTestUtil.h"
#include "server/content/QcModLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

void writeFile(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

} // namespace

// Minecarts in mods (decisions E-382, E-386): when the content is linked, a block a cart item carries
// and the items of a structure's minecarts must exist (a typo would silently make a plain cart or
// none), and the loader says so.
class TestCartContent : public QObject
{
    Q_OBJECT

private slots:
    void unknownCartBlocksAndCartItemsAreReported()
    {
        QTemporaryDir dir;
        const QString mod = dir.filePath(u"trains"_s);
        writeFile(mod + u"/pack.json"_s, R"({"id":"trains","version":"1.0"})");
        writeFile(mod + u"/items/barrel_minecart.json"_s, R"({"name":"barrel_minecart","texture":"qtcraft:minecart","placesCart":true,"cartBlock":"barel"})");
        writeFile(mod + u"/items/tank_minecart.json"_s, R"({"name":"tank_minecart","texture":"qtcraft:minecart","placesCart":true,"cartBlock":"qtcraft:chest"})");
        writeFile(mod + u"/structures/depot.json"_s, R"({"name":"depot","palette":{"r":"qtcraft:rail"},"layers":[["rrr"]],)"
                                                     R"("minecarts":[[0,0,0],{"at":[1,0,0],"item":"tank_minecart"},)"
                                                     R"({"at":[2,0,0],"item":"chest_minecrat"},{"at":[2,0,0],"item":"qtcraft:stick"}]})");

        QStringList warnings;
        const QcResult<QcModLoader::Result> loaded = QcModLoader::load(dir.path(), warnings);
        QVERIFY2(loaded.has_value(), qcErrorText(loaded));
        const QString all = warnings.join(u'\n');
        QVERIFY2(all.contains(u"item trains:barrel_minecart: carries unknown block 'trains:barel'"_s), qPrintable(all));
        QVERIFY2(all.contains(u"structure trains:depot: 'trains:chest_minecrat' is no minecart"_s), qPrintable(all));
        QVERIFY2(all.contains(u"structure trains:depot: 'qtcraft:stick' is no minecart"_s), qPrintable(all));
        // Names that exist are not reported.
        QVERIFY2(!all.contains(u"tank_minecart"_s), qPrintable(all));
        QVERIFY2(!all.contains(u"'qtcraft:minecart'"_s), qPrintable(all));
    }
};

QTEST_GUILESS_MAIN(TestCartContent)
#include "test_cart_content.moc"
