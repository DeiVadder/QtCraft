#include "app/controllers/QcHudState.h"
#include "clientcore/session/QcClientEntity.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QTest>

using namespace Qt::StringLiterals;

// The crosshair text for a targeted entity (QcHudState::entityTarget, decision E-874): health only
// for creatures and players; paintings, item frames, carts and armour stands by their item's name.
class TestHudTarget : public QObject
{
    Q_OBJECT

private:
    QcContentRegistry m_content;

    [[nodiscard]] QcItemId item(const QString &name) const { return m_content.itemId(name).value_or(qcNoItem); }

    [[nodiscard]] QcClientEntity hanging(QcHangingType type, const QString &ownItem) const
    {
        QcClientEntity entity;
        entity.kind = QcEntityKind::Hanging;
        entity.hanging.type = type;
        entity.held = item(ownItem);
        return entity;
    }

private slots:
    void initTestCase() { QVERIFY(QcBaseContent::loadInto(m_content)); }

    void paintingShowsItsName()
    {
        const QcClientEntity painting = hanging(QcHangingType::Painting, u"qtcraft:painting"_s);
        QCOMPARE(QcHudState::entityTarget(m_content, painting, u"en"_s).label, u"Painting"_s);
        QCOMPARE(QcHudState::entityTarget(m_content, painting, u"de"_s).label, u"Gemälde"_s);
        QVERIFY(QcHudState::entityTarget(m_content, painting, u"en"_s).name.isEmpty());

        // Without its own item (an unknown one) the kind names it.
        QCOMPARE(QcHudState::entityTarget(m_content, hanging(QcHangingType::Painting, {}), u"en"_s).label, u"Painting"_s);
    }

    void itemFrameShowsWhatItHolds()
    {
        QcClientEntity frame = hanging(QcHangingType::ItemFrame, u"qtcraft:item_frame"_s);
        QCOMPARE(QcHudState::entityTarget(m_content, frame, u"en"_s).label, u"Item frame"_s);

        frame.item = QcItemStack{item(u"qtcraft:diamond"_s), 1};
        QCOMPARE(QcHudState::entityTarget(m_content, frame, u"en"_s).label, u"Item frame (Diamond)"_s);

        // A renamed stack (a named map) shows its name.
        frame.item = QcItemStack{item(u"qtcraft:filled_map"_s), 1}.withName(u"Home"_s);
        QCOMPARE(QcHudState::entityTarget(m_content, frame, u"en"_s).label, u"Item frame (Home)"_s);
    }

    void creaturesShowTheirHealth()
    {
        QcClientEntity pig;
        pig.kind = QcEntityKind::Creature;
        pig.creatureType = *m_content.mobIndex(u"qtcraft:pig"_s);
        pig.health = 9.6;
        const QcHudState::EntityTarget target = QcHudState::entityTarget(m_content, pig, u"en"_s);
        QCOMPARE(target.label, u"Pig (10)"_s);
        QCOMPARE(target.name, u"mob:qtcraft:pig"_s);

        pig.name = u"Wilbur"_s;
        QCOMPARE(QcHudState::entityTarget(m_content, pig, u"en"_s).label, u"Wilbur (Pig) (10)"_s);

        QcClientEntity player;
        player.kind = QcEntityKind::Player;
        player.name = u"Alex"_s;
        player.health = 20.0;
        QCOMPARE(QcHudState::entityTarget(m_content, player, u"en"_s).label, u"Alex (20)"_s);
    }

    void otherEntitiesShowNoHealth()
    {
        QcClientEntity stand;
        stand.kind = QcEntityKind::ArmourStand;
        QCOMPARE(QcHudState::entityTarget(m_content, stand, u"en"_s).label, u"Armour stand"_s);
        stand.name = u"Knight"_s;
        QCOMPARE(QcHudState::entityTarget(m_content, stand, u"en"_s).label, u"Knight"_s);

        QcClientEntity cart;
        cart.kind = QcEntityKind::Minecart;
        cart.item = QcItemStack{item(u"qtcraft:minecart"_s), 1};
        QCOMPARE(QcHudState::entityTarget(m_content, cart, u"en"_s).label, u"Minecart"_s);
    }
};

QTEST_GUILESS_MAIN(TestHudTarget)
#include "test_hud_target.moc"
