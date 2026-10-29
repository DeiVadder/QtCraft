// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcInventoryModel.h"
#include "clientcore/session/QcClientMachineScreen.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcProtocol.h"
#include "QcTestUtil.h"

#include <QTest>

using namespace Qt::StringLiterals;

// What the client shows of a backpack (decisions E-832, E-833): the held slot of a screen opened
// from the hand on the wire and in the screen state, and the tooltip listing what a backpack holds.
class TestBackpackScreen : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemStack stackOf(const char *name, int count) const { return {*m_content->itemId(QString::fromLatin1(name)), count, 0}; }

private slots:
    void heldSlotTravelsAndIsChecked()
    {
        QcMsg::OpenMachine open;
        open.heldSlot = 4;
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(open), QcProtocol::Peer::Server);
        QVERIFY(decoded);
        QCOMPARE(std::get<QcMsg::OpenMachine>(*decoded).heldSlot, 4);
        QCOMPARE(std::get<QcMsg::OpenMachine>(*QcProtocol::decode(QcProtocol::encode(QcMsg::OpenMachine{}), QcProtocol::Peer::Server)).heldSlot,
                 QcMsg::OpenMachine::noHeldSlot);

        // Outside the hotbar, or with a machine category or a cart, the message is refused.
        for (const QcMsg::OpenMachine &bad : {QcMsg::OpenMachine{{}, {}, 0, 9}, QcMsg::OpenMachine{{}, u"smelting"_s, 0, 1},
                                              QcMsg::OpenMachine{{}, {}, 7, 1}, QcMsg::OpenMachine{{}, {}, 0, -2}}) {
            QVERIFY(!QcProtocol::decode(QcProtocol::encode(bad), QcProtocol::Peer::Server));
        }
    }

    void theScreenKnowsTheHeldSlot()
    {
        QcClientMachineScreen screen;
        QcMsg::OpenMachine open;
        open.heldSlot = 3;
        QCOMPARE(screen.handle(open, m_content.get()), QcClientMachineScreen::Change::Opened);
        QCOMPARE(screen.heldSlot(), 3);
        QVERIFY(screen.isContainer());

        QcMsg::MachineState state;
        state.stacks = QList<QcItemStack>(9);
        state.stacks[0] = stackOf("qtcraft:dirt", 2);
        QCOMPARE(screen.handle(state, m_content.get()), QcClientMachineScreen::Change::Changed);
        QCOMPARE(screen.stacks().size(), 9);

        QCOMPARE(screen.handle(QcMsg::CloseMachine{}, m_content.get()), QcClientMachineScreen::Change::Closed);
        QCOMPARE(screen.heldSlot(), QcMsg::OpenMachine::noHeldSlot);

        // A block's screen has none.
        QCOMPARE(screen.handle(QcMsg::OpenMachine{{1, 2, 3}, {}}, m_content.get()), QcClientMachineScreen::Change::Opened);
        QCOMPARE(screen.heldSlot(), QcMsg::OpenMachine::noHeldSlot);
    }

    void tooltipListsTheContents()
    {
        const QcItemStack plain = stackOf("qtcraft:backpack", 1);
        QVERIFY(QcInventoryModel::contentsLines(*m_content, plain, u"en"_s).isEmpty());

        const QcItemStack few = plain.withContents({stackOf("qtcraft:dirt", 5), {}, stackOf("qtcraft:stick", 1)});
        QCOMPARE(QcInventoryModel::contentsLines(*m_content, few, u"en"_s), (QStringList{u"Dirt × 5"_s, u"Stick"_s}));

        QList<QcItemStack> many;
        for (int i = 0; i < 9; ++i) {
            many.append(stackOf("qtcraft:cobblestone", i + 1));
        }
        const QStringList lines = QcInventoryModel::contentsLines(*m_content, plain.withContents(many), u"en"_s);
        QCOMPARE(lines.size(), QcInventoryModel::maxContentLines + 1);
        QCOMPARE(lines.constFirst(), u"Cobblestone"_s);
        QCOMPARE(lines.at(1), u"Cobblestone × 2"_s);
        QCOMPARE(lines.constLast(), u"and 4 more"_s);
    }
};

QTEST_GUILESS_MAIN(TestBackpackScreen)
#include "test_backpack_screen.moc"
