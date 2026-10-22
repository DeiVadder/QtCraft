#include "app/controllers/QcCharacterSettings.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/session/QcSessionParts.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "worlds/QcLocalServerHost.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The character screens edit a draft (decision E-852): changes, random looks and presets change the
// draft and the look the previews show, not the saved look; saving applies it, discarding goes back,
// and closing the draft leaves the saved look as it was.
class TestCharacterDraft : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    // Without a draft a change applies at once, as before.
    void changeWithoutDraftAppliesAtOnce()
    {
        Fixture fixture;
        QcCharacterSettings &character = *fixture.settings.character();
        character.setLook({{u"skin"_s, u"#123456"_s}});
        QCOMPARE(character.appearance().skinColor, 0x123456u);
        QVERIFY(!character.drafting());
        QVERIFY(!character.dirty());
    }

    void draftChangesOnlyTheShownLook()
    {
        Fixture fixture;
        QcCharacterSettings &character = *fixture.settings.character();
        const QcAppearance saved = character.appearance();
        QSignalSpy draft(&character, &QcCharacterSettings::draftChanged);

        character.beginDraft();
        QVERIFY(character.drafting());
        QVERIFY(!character.dirty());
        character.setLook({{u"shirt"_s, u"#abcdef"_s}});
        QVERIFY(character.dirty());
        QCOMPARE(character.appearance(), saved);
        QCOMPARE(character.shownAppearance().shirtColor, 0xabcdefu);
        QCOMPARE(character.look().value(u"shirt"_s).toString(), u"#abcdef"_s);
        QVERIFY(draft.count() >= 2);

        // Presets and the default look change the draft only.
        character.applyPreset(0);
        character.reset();
        QCOMPARE(character.shownAppearance(), QcAppearance());
        QCOMPARE(character.appearance(), saved);
    }

    void saveAppliesTheDraft()
    {
        Fixture fixture;
        QcCharacterSettings &character = *fixture.settings.character();
        character.beginDraft();
        character.setLook({{u"pants"_s, u"#204060"_s}});
        character.saveDraft();
        QCOMPARE(character.appearance().pantsColor, 0x204060u);
        QVERIFY(character.drafting());
        QVERIFY(!character.dirty());
        character.endDraft();
        QCOMPARE(character.appearance().pantsColor, 0x204060u);
    }

    void discardAndCloseKeepTheSavedLook()
    {
        Fixture fixture;
        QcCharacterSettings &character = *fixture.settings.character();
        const QcAppearance saved = character.appearance();
        character.beginDraft();
        character.setLook({{u"hairColor"_s, u"#010203"_s}});
        character.discardDraft();
        QVERIFY(!character.dirty());
        QCOMPARE(character.shownAppearance(), saved);

        character.setLook({{u"hairColor"_s, u"#010203"_s}});
        character.endDraft();
        QVERIFY(!character.drafting());
        QVERIFY(!character.dirty());
        QCOMPARE(character.appearance(), saved);
        QCOMPARE(character.shownAppearance(), saved);
    }

    // Settings loaded while a draft is open change the stored look the draft is compared with: the
    // draft becomes unsaved (or saved), and the editor hears of it.
    void loadDuringADraftUpdatesDirty()
    {
        Fixture fixture;
        QcCharacterSettings &character = *fixture.settings.character();
        character.beginDraft();
        QVERIFY(!character.dirty());
        QSignalSpy draft(&character, &QcCharacterSettings::draftChanged);

        QcAppearance other = character.appearance();
        other.shirtColor = 0x135724u;
        character.load(other, {});
        QCOMPARE(draft.count(), 1);
        QVERIFY(character.dirty());
        QCOMPARE(character.appearance().shirtColor, 0x135724u);

        // Loading the draft's look makes it saved again.
        character.load(character.shownAppearance(), {});
        QCOMPARE(draft.count(), 2);
        QVERIFY(!character.dirty());
    }

private:
    struct Fixture
    {
        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        QcSettingsController settings{parts, library, &audio};
    };

    QTemporaryDir m_data;
};

QTEST_MAIN(TestCharacterDraft)
#include "test_character_draft.moc"
