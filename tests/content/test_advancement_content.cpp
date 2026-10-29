// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The advancements of the content (decision E-923): parsing and validation, mods adding,
// replacing and switching off advancements by name, the requirement groups, the base tree, the
// content sync, the saved progress and the protocol message with its bounds.

#include "common/content/loader/QcBaseContent.h"
#include "common/content/loader/QcContentPackLoader.h"
#include "common/content/parser/QcAdvancementParser.h"
#include "common/content/registry/QcAdvancementSerializer.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/gameplay/QcAdvancementProgress.h"
#include "common/protocol/QcProtocol.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QString writePack(const QString &root, const QString &id, const QList<std::pair<QString, QByteArray>> &files)
{
    const QString directory = QDir(root).filePath(id);
    writeFile(QDir(directory).filePath(u"pack.json"_s), u"{\"id\": \"%1\", \"version\": \"1.0.0\"}"_s.arg(id).toUtf8());
    for (const auto &[path, content] : files) {
        writeFile(QDir(directory).filePath(path), content);
    }
    return directory;
}

struct Loaded
{
    std::shared_ptr<QcContentRegistry> content;
    QStringList warnings;
};

// The base pack and then `packs` in that order, finished; only the warnings of the packs and finish().
Loaded load(const QStringList &packs)
{
    Loaded loaded{std::make_shared<QcContentRegistry>(), {}};
    if (!QcBaseContent::loadInto(*loaded.content)) {
        return loaded;
    }
    for (const QString &pack : packs) {
        const QcResult<QcLoadedPack> read = QcContentPackLoader::load(pack, *loaded.content);
        loaded.warnings += read ? read->warnings : QStringList{read.error()};
    }
    loaded.warnings += loaded.content->finish();
    return loaded;
}

bool anyContains(const QStringList &warnings, const QString &text)
{
    return std::ranges::any_of(warnings, [&text](const QString &warning) { return warning.contains(text); });
}

QcResult<QcAdvancementDefinition> parse(const QByteArray &json)
{
    return QcAdvancementParser::parseAdvancement(QJsonDocument::fromJson(json).object(), u"mymod"_s);
}

QcResult<QcMessage> roundTrip(const QcMessage &message)
{
    return QcProtocol::decode(QcProtocol::encode(message), QcProtocol::Peer::Server);
}

} // namespace

class TestAdvancementContent : public QObject
{
    Q_OBJECT

private slots:
    void parsesEveryField()
    {
        // "\u0023" is '#' in JSON: moc stops reading the file at a '#' inside a raw string.
        const QcResult<QcAdvancementDefinition> parsed = parse(R"({"name": "gem", "parent": "qtcraft:iron", "icon": "gem",
            "frame": "challenge", "hidden": true, "handbook": "item:gem",
            "criteria": {"a": {"trigger": "item_obtained", "conditions": {"item": "\u0023gems", "count": 3}},
                         "b": {"trigger": "location", "conditions": {"biome": "qtcraft:desert", "minY": 10, "maxY": 40}},
                         "c": {"trigger": "impossible"}},
            "requirements": [["a", "b"], ["c"]]})");
        QVERIFY2(parsed.has_value(), qPrintable(parsed ? QString() : parsed.error()));
        QCOMPARE(parsed->name, u"mymod:gem"_s);
        QCOMPARE(parsed->parent, u"qtcraft:iron"_s);
        QCOMPARE(parsed->icon, u"mymod:gem"_s);
        QCOMPARE(parsed->frame, QcAdvancementFrame::Challenge);
        QVERIFY(parsed->hidden);
        QCOMPARE(parsed->handbook, u"item:mymod:gem"_s);
        QCOMPARE(parsed->titleKey, u"advancement.mymod.gem"_s);
        QCOMPARE(parsed->descriptionKey, u"advancement.mymod.gem.description"_s);
        QCOMPARE(parsed->criteria.size(), 3);
        const QcAdvancementCriterion *a = parsed->criterion(u"a"_s);
        QVERIFY(a);
        QCOMPARE(a->trigger, QcAdvancementTrigger::ItemObtained);
        QCOMPARE(a->conditions.item, u"#mymod:gems"_s);
        QCOMPARE(a->conditions.count, 3);
        const QcAdvancementCriterion *b = parsed->criterion(u"b"_s);
        QCOMPARE(b->conditions.biome, u"qtcraft:desert"_s);
        QCOMPARE(b->conditions.minY, std::optional<int>(10));
        QCOMPARE(b->conditions.maxY, std::optional<int>(40));
        QCOMPARE(parsed->requirements, (QList<QStringList>{{u"a"_s, u"b"_s}, {u"c"_s}}));
    }

    void refusesBrokenFiles_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("reason");
        const QByteArray criteria = R"("criteria": {"a": {"trigger": "impossible"}})";
        QTest::newRow("no icon") << QByteArray(R"({"name": "x", )" + criteria + "}") << u"'icon' is required"_s;
        QTest::newRow("no criteria") << QByteArray(R"({"name": "x", "icon": "stick"})") << u"'criteria'"_s;
        QTest::newRow("unknown trigger") << QByteArray(R"({"name": "x", "icon": "stick", "criteria": {"a": {"trigger": "fly"}}})")
                                         << u"unknown trigger 'fly'"_s;
        QTest::newRow("foreign condition") << QByteArray(R"({"name": "x", "icon": "stick", "criteria": {"a": {"trigger": "block_placed",
                                                         "conditions": {"entity": "cow"}}}})")
                                           << u"has no condition 'entity'"_s;
        QTest::newRow("unknown frame") << QByteArray(R"({"name": "x", "icon": "stick", "frame": "epic", )" + criteria + "}") << u"unknown 'frame'"_s;
        QTest::newRow("bad handbook") << QByteArray(R"({"name": "x", "icon": "stick", "handbook": "page:x", )" + criteria + "}") << u"'handbook'"_s;
        QTest::newRow("requirement of nothing") << QByteArray(R"({"name": "x", "icon": "stick", "requirements": [["b"]], )" + criteria + "}")
                                                << u"'requirements'"_s;
        QTest::newRow("bad vehicle") << QByteArray(R"({"name": "x", "icon": "stick", "criteria": {"a": {"trigger": "ride_vehicle",
                                                   "conditions": {"vehicle": "horse"}}}})")
                                     << u"'vehicle'"_s;
        QTest::newRow("bad structure") << QByteArray(R"({"name": "x", "icon": "stick", "criteria": {"a": {"trigger": "location",
                                                     "conditions": {"structure": "temple"}}}})")
                                       << u"'structure'"_s;
    }

    void refusesBrokenFiles()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, reason);
        const QcResult<QcAdvancementDefinition> parsed = parse(json);
        QVERIFY(!parsed);
        QVERIFY2(parsed.error().contains(reason), qPrintable(parsed.error()));
    }

    void requirementsGroupAllAndAny()
    {
        QcResult<QcAdvancementDefinition> all = parse(R"({"name": "x", "icon": "stick", "criteria": {"a": {"trigger": "impossible"},
            "b": {"trigger": "impossible"}}})");
        QVERIFY(all);
        QVERIFY(!all->isDone({u"a"_s}));
        QVERIFY(all->isDone({u"a"_s, u"b"_s}));

        QcResult<QcAdvancementDefinition> any = parse(R"({"name": "x", "icon": "stick", "requirements": "any",
            "criteria": {"a": {"trigger": "impossible"}, "b": {"trigger": "impossible"}}})");
        QVERIFY(any);
        QVERIFY(any->isDone({u"b"_s}));
        QVERIFY(!any->isDone({}));

        QcResult<QcAdvancementDefinition> groups = parse(R"({"name": "x", "icon": "stick", "requirements": [["a", "b"], ["c"]],
            "criteria": {"a": {"trigger": "impossible"}, "b": {"trigger": "impossible"}, "c": {"trigger": "impossible"}}})");
        QVERIFY(groups);
        QVERIFY(!groups->isDone({u"a"_s, u"b"_s}));
        QVERIFY(groups->isDone({u"b"_s, u"c"_s}));
        QVERIFY(!groups->isDone({u"c"_s}));
        QVERIFY(!groups->isDone({u"unknown"_s, u"c"_s}));
    }

    void baseTreeLoadsWithoutWarnings()
    {
        const QcResult<QcBaseContent::Loaded> base = QcBaseContent::load();
        QVERIFY(base);
        QVERIFY2(base->warnings.isEmpty(), qPrintable(base->warnings.join(u'\n')));
        const QcAdvancementCatalog &advancements = base->registry->advancements();
        QVERIFY(advancements.items().size() >= 20 && advancements.items().size() <= 30);

        // One root; every advancement leads into the handbook and is translated in both languages.
        int roots = 0;
        for (const QcAdvancementDefinition &advancement : advancements.items()) {
            roots += advancement.parent.isEmpty() ? 1 : 0;
            QVERIFY2(!advancement.handbook.isEmpty(), qPrintable(advancement.name));
            QVERIFY(base->registry->translations().has(advancement.titleKey, u"de"_s));
            QVERIFY(base->registry->translations().has(advancement.descriptionKey, u"de"_s));
        }
        QCOMPARE(roots, 1);
        QCOMPARE(advancements.find(u"qtcraft:diamond_armour"_s)->frame, QcAdvancementFrame::Challenge);
    }

    void modsAddReplaceAndSwitchOff()
    {
        QTemporaryDir dir;
        const QString mod = writePack(dir.path(), u"guide"_s,
                                      {{u"advancements/furnace.json"_s, R"({"name": "qtcraft:furnace", "disabled": true})"},
                                       {u"advancements/crafting_table.json"_s, R"({"name": "qtcraft:crafting_table", "parent": "qtcraft:adventure",
                                           "icon": "qtcraft:crafting_table", "frame": "goal",
                                           "criteria": {"placed": {"trigger": "block_placed", "conditions": {"block": "qtcraft:crafting_table"}}}})"},
                                       {u"advancements/ruby.json"_s, R"({"name": "ruby", "parent": "qtcraft:iron", "icon": "qtcraft:stick",
                                           "handbook": "chapter:nothing", "criteria": {"x": {"trigger": "entity_killed", "conditions": {"entity": "dragon"}}}})"}});
        const Loaded loaded = load({mod});
        const QcAdvancementCatalog &advancements = loaded.content->advancements();

        QVERIFY(!advancements.find(u"qtcraft:furnace"_s));
        // The furnace's children hang on its parent now.
        QCOMPARE(advancements.find(u"qtcraft:iron"_s)->parent, u"qtcraft:stone_age"_s);
        QCOMPARE(advancements.find(u"qtcraft:cooked_food"_s)->parent, u"qtcraft:stone_age"_s);

        const QcAdvancementDefinition *table = advancements.find(u"qtcraft:crafting_table"_s);
        QCOMPARE(table->frame, QcAdvancementFrame::Goal);
        QCOMPARE(table->criteria.constFirst().trigger, QcAdvancementTrigger::BlockPlaced);

        QVERIFY(advancements.find(u"guide:ruby"_s));
        QVERIFY2(anyContains(loaded.warnings, u"the handbook has no entry 'chapter:guide:nothing'"_s), qPrintable(loaded.warnings.join(u'\n')));
        QVERIFY(anyContains(loaded.warnings, u"criterion 'x' names 'guide:dragon'"_s));
        QVERIFY(anyContains(loaded.warnings, u"no English text for 'advancement.guide.ruby'"_s));
    }

    void unknownParentsAndLoopsBecomeRoots()
    {
        QTemporaryDir dir;
        const QByteArray criteria = R"("criteria": {"a": {"trigger": "impossible"}})";
        const QString mod = writePack(dir.path(), u"loops"_s,
                                      {{u"advancements/a.json"_s, R"({"name": "a", "parent": "b", "icon": "qtcraft:stick", )" + criteria + "}"},
                                       {u"advancements/b.json"_s, R"({"name": "b", "parent": "a", "icon": "qtcraft:stick", )" + criteria + "}"},
                                       {u"advancements/c.json"_s, R"({"name": "c", "parent": "missing", "icon": "qtcraft:stick", )" + criteria + "}"}});
        const Loaded loaded = load({mod});
        const QcAdvancementCatalog &advancements = loaded.content->advancements();
        QVERIFY(anyContains(loaded.warnings, u"advancement 'loops:c': no parent 'loops:missing'"_s));
        QVERIFY(anyContains(loaded.warnings, u"its parents form a loop"_s));
        QVERIFY(advancements.find(u"loops:c"_s)->parent.isEmpty());
        // Exactly one of the loop becomes a root, the other hangs on it.
        QCOMPARE(advancements.find(u"loops:a"_s)->parent.isEmpty() + advancements.find(u"loops:b"_s)->parent.isEmpty(), 1);
    }

    void contentSyncCarriesTheTreeWithoutTriggers()
    {
        const QcResult<QcBaseContent::Loaded> base = QcBaseContent::load();
        QVERIFY(base);
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        base->registry->writeTo(out);
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> received = QcContentRegistry::readFrom(in);
        QVERIFY2(received, qPrintable(received ? QString() : received.error()));

        const QList<QcAdvancementDefinition> &sent = base->registry->advancements().items();
        const QList<QcAdvancementDefinition> &got = received->advancements().items();
        QCOMPARE(got.size(), sent.size());
        for (qsizetype i = 0; i < sent.size(); ++i) {
            QCOMPARE(got[i].name, sent[i].name);
            QCOMPARE(got[i].parent, sent[i].parent);
            QCOMPARE(got[i].icon, sent[i].icon);
            QCOMPARE(got[i].handbook, sent[i].handbook);
            QCOMPARE(got[i].frame, sent[i].frame);
            QCOMPARE(got[i].requirements, sent[i].requirements);
            QCOMPARE(got[i].criteria.size(), sent[i].criteria.size());
            QCOMPARE(got[i].criteria.constFirst().trigger, QcAdvancementTrigger::Impossible);
        }
    }

    void progressBytesKeepUnknownAdvancements()
    {
        QcAdvancementProgress progress;
        QVERIFY(progress.grant(u"qtcraft:iron"_s, u"iron"_s));
        QVERIFY(progress.grant(u"othermod:gone"_s, u"x"_s));
        QVERIFY(!progress.grant(u"othermod:gone"_s, u"x"_s));
        const std::optional<QcAdvancementProgress> read = QcAdvancementProgress::fromBytes(progress.bytes());
        QVERIFY(read);
        QCOMPARE(*read, progress);
        QCOMPARE(read->advancements(), (QStringList{u"othermod:gone"_s, u"qtcraft:iron"_s}));

        // A later format may append fields; they are left out.
        QByteArray later = progress.bytes();
        later.append("trailing");
        QCOMPARE(QcAdvancementProgress::fromBytes(later), progress);
        QVERIFY(!QcAdvancementProgress::fromBytes(QByteArray("\x00\x01\xff\xff\xff\xff", 6)));
    }

    void progressBoundsHold()
    {
        QcAdvancementProgress progress;
        for (int i = 0; i < QcAdvancementDefinition::maxCriteria; ++i) {
            QVERIFY(progress.grant(u"a"_s, QString::number(i)));
        }
        QVERIFY(!progress.grant(u"a"_s, u"one too many"_s));
        QVERIFY(!progress.grant(QString(QcAdvancementProgress::maxNameLength + 1, u'a'), u"x"_s));
    }

    void protocolMessageRoundTrips()
    {
        const QcMsg::AdvancementProgress message{true, {{u"qtcraft:iron"_s, {u"iron"_s}, true, true}, {u"qtcraft:map"_s, {}, false, false}}};
        const QcResult<QcMessage> decoded = roundTrip(message);
        QVERIFY(decoded);
        const auto *got = std::get_if<QcMsg::AdvancementProgress>(&*decoded);
        QVERIFY(got);
        QCOMPARE(got->reset, true);
        QCOMPARE(got->entries, message.entries);
        // Only the server sends it.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(message), QcProtocol::Peer::Client));
    }

    void protocolMessageBoundsHold()
    {
        QcMsg::AdvancementProgress tooMany;
        tooMany.entries = QList<QcMsg::AdvancementProgress::Entry>(QcMsg::AdvancementProgress::maxEntries + 1, {u"a"_s, {}, false, false});
        QVERIFY(!roundTrip(tooMany));

        QStringList criteria;
        for (int i = 0; i <= QcMsg::AdvancementProgress::maxCriteria; ++i) {
            criteria.append(QString::number(i));
        }
        QVERIFY(!roundTrip(QcMsg::AdvancementProgress{false, {{u"a"_s, criteria, false, false}}}));
        QVERIFY(!roundTrip(QcMsg::AdvancementProgress{false, {{QString(QcMsg::AdvancementProgress::maxNameLength + 1, u'a'), {}, false, false}}}));
        QVERIFY(!roundTrip(QcMsg::AdvancementProgress{false, {{QString(), {}, false, false}}}));
    }

    // A forged count of strings fails the read instead of reserving room for four billion of them
    // (QDataStream's own QStringList operator would abort the program).
    void forgedCountsFailTheRead()
    {
        constexpr quint32 forged = 0xfffffff0u;

        QByteArray frame;
        QDataStream message(&frame, QIODevice::WriteOnly);
        message.setVersion(QDataStream::Qt_6_8);
        message << static_cast<quint16>(QcMessage(QcMsg::AdvancementProgress{}).index()) << false << quint32{1} << u"qtcraft:iron"_s << forged;
        QVERIFY(!QcProtocol::decode(frame, QcProtocol::Peer::Server));

        QByteArray progress;
        QDataStream saved(&progress, QIODevice::WriteOnly);
        saved.setVersion(QDataStream::Qt_6_8);
        saved << quint16{1} << quint32{1} << u"qtcraft:iron"_s << forged;
        QVERIFY(!QcAdvancementProgress::fromBytes(progress));

        // The content sync: the criteria of an advancement, then a requirement group.
        for (const bool inGroup : {false, true}) {
            QByteArray content;
            QDataStream sync(&content, QIODevice::WriteOnly);
            sync.setVersion(QDataStream::Qt_6_8);
            sync << quint32{1} << u"qtcraft:iron"_s << QString() << u"qtcraft:iron_ingot"_s << u"t"_s << u"d"_s << QString() << quint8{0} << false;
            if (inGroup) {
                sync << QStringList{u"iron"_s} << quint32{1} << forged;
            } else {
                sync << forged;
            }
            QDataStream in(content);
            in.setVersion(QDataStream::Qt_6_8);
            QcContentRegistry registry;
            QVERIFY2(!QcAdvancementSerializer::read(in, registry), inGroup ? "requirement group" : "criteria");
        }
    }

    void minYAboveMaxYIsRefused()
    {
        const QcResult<QcAdvancementDefinition> parsed = parse(R"({"name": "x", "icon": "stick", "criteria": {"a": {"trigger": "location",
            "conditions": {"minY": 50, "maxY": 10}}}})");
        QVERIFY(!parsed);
        QVERIFY2(parsed.error().contains(u"'minY' is above 'maxY'"_s), qPrintable(parsed.error()));
    }
};

QTEST_GUILESS_MAIN(TestAdvancementContent)
#include "test_advancement_content.moc"
