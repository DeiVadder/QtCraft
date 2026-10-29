// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/settings/QcCharacterPresets.h"
#include "clientcore/settings/QcGameSettings.h"
#include "common/core/QcNameText.h"
#include "QcTestUtil.h"

#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSettings>
#include <QTest>
#include <QTranslator>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

// A German translation of one preset name, without a .qm file.
class PresetTranslator : public QTranslator
{
    Q_OBJECT

public:
    QString translate(const char *context, const char *sourceText, const char *, int) const override
    {
        return QByteArrayView(context) == "QcCharacterProfile" && QByteArrayView(sourceText) == "Miner" ? u"Bergmann"_s : QString();
    }
    bool isEmpty() const override { return false; }
};

QStringList labels(const QcCharacterPresets &presets)
{
    QStringList list;
    const QVariantList rows = presets.rows();
    for (const QVariant &row : rows) {
        list.append(row.toMap().value(u"label"_s).toString());
    }
    return list;
}

} // namespace

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void missingFileGivesDefaults()
    {
        QTemporaryDir dir;
        QCOMPARE(QcGameSettings::load(dir.filePath(u"settings.ini"_s)), QcGameSettings{});
        // Explosions shake the camera unless the player switches it off (decision E-879).
        QVERIFY(QcGameSettings{}.cameraShake);
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"nested/settings.ini"_s);
        QcGameSettings settings;
        settings.viewDistance = 12;
        settings.fieldOfView = 90.0;
        settings.fovEffects = 0.35;
        settings.mouseSensitivity = 0.3;
        settings.invertMouse = true;
        settings.debug.level = QcDebugOptions::Level::Full;
        settings.debug.set(QcDebugOptions::Visual::Hitboxes, true);
        settings.debug.set(QcDebugOptions::Visual::RemeshFlash, true);
        settings.showLabels = false;
        settings.cameraShake = false;
        settings.language = u"de"_s;
        settings.cameraMode = QcCameraMode::Front;
        settings.uiScale = QcUiScale::Step::Percent150;
        settings.textureFiltering = 8;
        settings.render = {QcRenderOptions::Mode::Realistic, QcRenderOptions::Quality::High, 1.35};
        settings.appearance.outfit = u"mod:robe"_s;
        settings.appearance.hair.clear();
        settings.appearance.skinColor = 0x010203;
        settings.appearance.hairColor = 0xfffffe;
        settings.appearance.shirtColor = 0x00ff00;
        settings.appearance.pantsColor = 0x000000;
        settings.appearance.cape = u"mod:red_cape"_s;
        settings.audio.master = 0.25;
        settings.audio.effects = 0.0;
        settings.audio.music = 1.0;
        settings.audio.ambient = 0.4;
        settings.audio.muted = true;
        settings.characterPresets = {{u"Cave look"_s, {u"qtcraft:miner"_s, u"qtcraft:cap"_s, 0x111111, 0x222222, 0x333333, 0x444444, u"qtcraft:green_cape"_s}},
                                     {u"Bald"_s, {u"qtcraft:tunic"_s, QString(), 0xabcdef, 0, 0xffffff, 0x010101}}};

        const QcStatus saved = settings.save(path);
        QVERIFY2(saved.has_value(), qcErrorText(saved));
        QCOMPARE(QcGameSettings::load(path), settings);

        // Fewer presets saved later leave none of the old ones behind.
        settings.characterPresets.removeFirst();
        QVERIFY(settings.save(path).has_value());
        QCOMPARE(QcGameSettings::load(path).characterPresets, settings.characterPresets);
    }

    // Active texture packs keep their order; one pack, none, repeats and too many survive the INI file.
    void texturePacksRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QcGameSettings settings;
        for (const QStringList &packs : {QStringList{u"builtin:storybook_32"_s, u"user:My Pack"_s}, QStringList{u"builtin:qtcraft_hd_64"_s},
                                         QStringList{u"user:a, b"_s, u"user:c"_s},
                                         QStringList{}}) {
            settings.texturePacks = packs;
            QVERIFY(settings.save(path).has_value());
            QCOMPARE(QcGameSettings::load(path).texturePacks, packs);
        }

        QStringList many{u"a"_s, u" a "_s, u""_s};
        for (int i = 0; i < 20; ++i) {
            many.append(u"p%1"_s.arg(i));
        }
        const QStringList clean = QcGameSettings::cleanTexturePacks(many);
        QCOMPARE(clean.size(), QcGameSettings::maxTexturePacks);
        QCOMPARE(clean.mid(0, 2), (QStringList{u"a"_s, u"p0"_s}));
    }

    void profilesWithoutCapeLoadWithoutCape()
    {
        // A settings file from before capes (stage 22) has no cape keys: the look and the presets
        // load as before, without a cape (decision E-240).
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(u"character/outfit"_s, u"qtcraft:miner"_s);
            file.setValue(u"character/hair"_s, u"qtcraft:cap"_s);
            file.setValue(u"character/shirt"_s, u"#123456"_s);
            file.setValue(u"characterPresets/size"_s, 1);
            file.setValue(u"characterPresets/1/name"_s, u"Old look"_s);
            file.setValue(u"characterPresets/1/outfit"_s, u"qtcraft:tunic"_s);
        }

        const QcGameSettings loaded = QcGameSettings::load(path);
        QCOMPARE(loaded.appearance.outfit, u"qtcraft:miner"_s);
        QCOMPARE(loaded.appearance.shirtColor, 0x123456u);
        QCOMPARE(loaded.appearance.cape, QString());
        QCOMPARE(loaded.characterPresets.size(), 1);
        QCOMPARE(loaded.characterPresets.first().appearance.outfit, u"qtcraft:tunic"_s);
        QCOMPARE(loaded.characterPresets.first().appearance.cape, QString());

        // An overlong cape name falls back to none, like other names.
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(u"character/cape"_s, QString(QcAppearance::maxNameLength + 1, u'x'));
        }
        QCOMPARE(QcGameSettings::load(path).appearance.cape, QString());
    }

    void characterPresetsAreBounded()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        {
            QSettings file(path, QSettings::IniFormat);
            // More than allowed, a nameless one and a name with control characters and spaces.
            file.setValue(u"characterPresets/size"_s, 50);
            for (int index = 1; index <= 12; ++index) {
                file.setValue(u"characterPresets/%1/name"_s.arg(index), index == 2 ? QString() : u"  Look\t%1 "_s.arg(index));
                file.setValue(u"characterPresets/%1/skin"_s.arg(index), u"#00000%1"_s.arg(index % 10));
            }
            file.setValue(u"characterPresets/3/outfit"_s, QString(QcAppearance::maxNameLength + 1, u'x'));
        }
        const QList<QcCharacterPreset> presets = QcGameSettings::load(path).characterPresets;
        // Eight places read, the nameless one skipped.
        QCOMPARE(presets.size(), QcCharacterProfile::maxPresets - 1);
        QCOMPARE(presets.first().name, u"Look1"_s);
        QCOMPARE(presets.first().appearance.skinColor, 0x000001u);
        // An overlong outfit name falls back to the default outfit.
        QCOMPARE(presets[1].appearance.outfit, QcAppearance().outfit);

        QCOMPARE(QcCharacterProfile::cleanPresetName(QString(40, u'a')).size(), QcCharacterProfile::maxPresetNameLength);
        QCOMPARE(QcCharacterProfile::cleanPresetName(u"\n\t"_s), QString());
    }

    void presetNamesCountCodePoints()
    {
        // 31 letters and an emoji are 32 code points and stay whole; one letter more drops the emoji
        // whole; a lone surrogate never survives (decision E-232).
        const QString emoji = u"\U0001F600"_s;
        QCOMPARE(QcCharacterProfile::cleanPresetName(QString(31, u'a') + emoji), QString(31, u'a') + emoji);
        QCOMPARE(QcCharacterProfile::cleanPresetName(QString(32, u'a') + emoji), QString(32, u'a'));
        QCOMPARE(QcCharacterProfile::cleanPresetName(u"Mi"_s + QChar(0xDC00) + u"ner"_s + QChar(0xD800)), u"Miner"_s);
        QVERIFY(QcCharacterProfile::cleanPresetName(QString(31, u'a') + emoji + emoji).isValidUtf16());

        // The name field cuts while typing, between graphemes and without trimming.
        QCOMPARE(QcNameText::fit(u" ab "_s, 32), u" ab "_s);
        QCOMPARE(QcNameText::fit(QString(31, u'a') + emoji + u"b"_s, 32), QString(31, u'a') + emoji);
        QCOMPARE(QcNameText::fit(QString(31, u'a') + u"e\u0301"_s, 32), QString(31, u'a'));
        QCOMPARE(QcNameText::codePoints(QString(emoji + u"x"_s + QChar(0xD800))), 3);
    }

    void presetsFollowTheLanguage()
    {
        QcCharacterPresets presets;
        QCOMPARE(labels(presets).at(1), u"Miner"_s);
        QSignalSpy changed(&presets, &QcCharacterPresets::changed);

        // Installing a translator (a language switch) updates the built-in names; the player's own
        // names are never translated.
        PresetTranslator german;
        QVERIFY(QCoreApplication::installTranslator(&german));
        QVERIFY(changed.count() >= 1);
        QCOMPARE(labels(presets).at(1), u"Bergmann"_s);

        const int before = static_cast<int>(changed.count());
        QVERIFY(QCoreApplication::removeTranslator(&german));
        QVERIFY(changed.count() > before);
        QCOMPARE(labels(presets).at(1), u"Miner"_s);
    }

    void presetPlacesAreLimited()
    {
        QcCharacterPresets presets;
        const int builtIn = static_cast<int>(QcCharacterProfile::builtInPresets().size());
        QcAppearance look;
        for (int index = 0; index < QcCharacterProfile::maxPresets; ++index) {
            look.skinColor = static_cast<quint32>(index);
            QVERIFY(presets.save(u"Look %1"_s.arg(index), look));
        }
        QVERIFY(!presets.canSave());
        // Full: a new name cannot be saved, an existing one can be replaced.
        QVERIFY(!presets.canSaveAs(u"Another"_s));
        QVERIFY(!presets.save(u"Another"_s, look));
        QVERIFY(presets.canSaveAs(u"  Look 3 "_s));
        look.skinColor = 0xabcdef;
        QVERIFY(presets.save(u"Look 3"_s, look));
        QCOMPARE(presets.appearance(builtIn + 3)->skinColor, 0xabcdefu);
        QVERIFY(!presets.canSaveAs(u" \t"_s));

        presets.remove(builtIn);
        QVERIFY(presets.canSaveAs(u"Another"_s));
        // Built-in presets cannot be removed; rows outside both lists have no look.
        presets.remove(0);
        QCOMPARE(presets.rows().size(), builtIn + QcCharacterProfile::maxPresets - 1);
        QVERIFY(!presets.appearance(-1));
        QVERIFY(!presets.appearance(int(presets.rows().size())));
    }

    void randomLooksAndBuiltInPresets()
    {
        const QStringList outfits{u"a:one"_s, u"a:two"_s};
        const QStringList hair{u"a:short"_s};
        const QStringList capes{u"a:green"_s, u"a:red"_s};
        // The same seed, the same look; every part from the offered choices.
        QCOMPARE(QcCharacterProfile::random(outfits, hair, capes, 7), QcCharacterProfile::random(outfits, hair, capes, 7));
        QSet<QString> seenOutfits;
        QSet<QString> seenHair;
        QSet<QString> seenCapes;
        for (quint32 seed = 0; seed < 200; ++seed) {
            const QcAppearance look = QcCharacterProfile::random(outfits, hair, capes, seed);
            seenOutfits.insert(look.outfit);
            seenHair.insert(look.hair);
            seenCapes.insert(look.cape);
            QVERIFY(QcCharacterProfile::skinColors().contains(look.skinColor));
            QVERIFY(QcCharacterProfile::hairColors().contains(look.hairColor));
            QVERIFY(QcCharacterProfile::clothColors().contains(look.shirtColor));
            QVERIFY(QcCharacterProfile::clothColors().contains(look.pantsColor));
        }
        QCOMPARE(seenOutfits, (QSet<QString>{u"a:one"_s, u"a:two"_s}));
        // Hair or none.
        QCOMPARE(seenHair, (QSet<QString>{u"a:short"_s, QString()}));
        // Capes or none.
        QCOMPARE(seenCapes, (QSet<QString>{u"a:green"_s, u"a:red"_s, QString()}));
        QCOMPARE(QcCharacterProfile::random({}, {}, {}, 3).outfit, QcAppearance().outfit);
        QCOMPARE(QcCharacterProfile::random({}, {}, {}, 3).cape, QString());

        // The first built-in preset is the default look; all have names.
        const QList<QcCharacterPreset> &builtIn = QcCharacterProfile::builtInPresets();
        QVERIFY(builtIn.size() >= 3);
        QCOMPARE(builtIn.first().appearance, QcAppearance());
        // The default look has no cape; one built-in preset shows the green cape.
        QCOMPARE(QcAppearance().cape, QString());
        QVERIFY(std::ranges::any_of(builtIn, [](const QcCharacterPreset &preset) { return preset.appearance.cape == u"qtcraft:green_cape"_s; }));
        QVERIFY(std::ranges::none_of(builtIn, [](const QcCharacterPreset &preset) { return preset.name.isEmpty(); }));
    }

    void audioDefaultsAndBadVolumes()
    {
        const QcGameSettings defaults;
        QCOMPARE(defaults.audio.master, 0.8);
        QCOMPARE(defaults.audio.effects, 1.0);
        QCOMPARE(defaults.audio.music, 0.5);
        QCOMPARE(defaults.audio.ambient, 0.7);
        QVERIFY(!defaults.audio.muted);

        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[audio]\nmaster=1.5\neffects=-2\nmusic=loud\nambient=nan\nmuted=true\n");
        file.close();

        const QcGameSettings settings = QcGameSettings::load(path);
        QCOMPARE(settings.audio.master, 1.0);
        QCOMPARE(settings.audio.effects, 0.0);
        QCOMPARE(settings.audio.music, defaults.audio.music);
        QCOMPARE(settings.audio.ambient, defaults.audio.ambient);
        QVERIFY(settings.audio.muted);
    }

    void badCharacterAndViewValuesFallBack()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[General]\ncameraMode=sideways\nuiScale=133\ntextureFiltering=3\n[render]\nmode=vivid\nquality=ultra\nexposure=7\n[character]\nskin=red\nhairColor=#12345\nshirt=#00ff0g\n"
                   "pants=#abcdef\noutfit=");
        file.write(QByteArray(QcAppearance::maxNameLength + 1, 'x'));
        file.write("\n");
        file.close();

        const QcGameSettings settings = QcGameSettings::load(path);
        const QcAppearance defaults;
        QCOMPARE(settings.cameraMode, QcCameraMode::FirstPerson);
        QCOMPARE(settings.uiScale, QcUiScale::defaultStep);
        QCOMPARE(settings.textureFiltering, QcGameSettings::defaultTextureFiltering);
        // Unknown render mode and quality and an exposure out of range give the defaults (decision E-899).
        QCOMPARE(settings.render, QcRenderOptions{});
        QCOMPARE(settings.appearance.skinColor, defaults.skinColor);
        QCOMPARE(settings.appearance.hairColor, defaults.hairColor);
        QCOMPARE(settings.appearance.shirtColor, defaults.shirtColor);
        QCOMPARE(settings.appearance.pantsColor, 0xabcdefu);
        QCOMPARE(settings.appearance.outfit, defaults.outfit);
    }

    void valuesFromTheFileAreClamped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[General]\nviewDistance=99\nfieldOfView=500\nfovEffects=-2\nmouseSensitivity=nan\ninvertMouse=true\n"
                   "showDebug=false\nlanguage=../../evil\n");
        file.close();

        const QcGameSettings settings = QcGameSettings::load(path);
        QCOMPARE(settings.viewDistance, QcGameSettings::maxViewDistance);
        QCOMPARE(settings.fieldOfView, QcGameSettings::maxFieldOfView);
        QCOMPARE(settings.fovEffects, 0.0);
        QCOMPARE(settings.mouseSensitivity, QcGameSettings::defaultMouseSensitivity);
        QVERIFY(settings.invertMouse);
        // An older file with only showDebug=false has the debug display off.
        QCOMPARE(settings.debug.level, QcDebugOptions::Level::Off);
        QCOMPARE(settings.language, QString());
    }

    void debugOptionsAndOlderFiles()
    {
        // The compact display by default, no visualisations.
        const QcGameSettings defaults;
        QCOMPARE(defaults.debug.level, QcDebugOptions::Level::Compact);
        QVERIFY(!defaults.debug.anyVisual());

        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[General]\nshowDebug=true\n[debug]\nmobGoals=true\nlightValues=nonsense\n");
        file.close();
        QcGameSettings loaded = QcGameSettings::load(path);
        QCOMPARE(loaded.debug.level, QcDebugOptions::Level::Compact);
        QVERIFY(loaded.debug.shows(QcDebugOptions::Visual::MobGoals));
        QVERIFY(!loaded.debug.shows(QcDebugOptions::Visual::LightValues));
        QVERIFY(!loaded.debug.shows(QcDebugOptions::Visual::ChunkBorders));

        // Saving writes the level and drops the old key; the level wins over it from then on.
        loaded.debug.level = QcDebugOptions::Level::Full;
        QVERIFY(loaded.save(path).has_value());
        const QSettings stored(path, QSettings::IniFormat);
        QVERIFY(!stored.contains(u"showDebug"_s));
        QCOMPARE(stored.value(u"debug/level"_s).toString(), u"full"_s);
        QCOMPARE(QcGameSettings::load(path).debug, loaded.debug);

        // An unknown level falls back to the default.
        QSettings broken(path, QSettings::IniFormat);
        broken.setValue(u"debug/level"_s, u"loud"_s);
        broken.sync();
        QCOMPARE(QcGameSettings::load(path).debug.level, QcDebugOptions::Level::Compact);
    }

    void debugLevelsCycleAndNamesRoundTrip()
    {
        using L = QcDebugOptions::Level;
        QCOMPARE(QcDebugOptions::nextLevel(L::Off), L::Compact);
        QCOMPARE(QcDebugOptions::nextLevel(L::Compact), L::Full);
        QCOMPARE(QcDebugOptions::nextLevel(L::Full), L::Off);
        for (const L level : {L::Off, L::Compact, L::Full}) {
            QCOMPARE(QcDebugOptions::levelFromName(QcDebugOptions::levelName(level)), std::optional(level));
        }
        for (int visual = 0; visual < QcDebugOptions::visualCount; ++visual) {
            const auto id = QcDebugOptions::visualId(static_cast<QcDebugOptions::Visual>(visual));
            QCOMPARE(QcDebugOptions::visualFromId(id), std::optional(QcDebugOptions::Visual(visual)));
        }
        QCOMPARE(QcDebugOptions::visualFromId(u"xray"_s), std::nullopt);
    }

    // The bloom of the realistic mode grows with the quality; the classic mode and Low have none
    // (decision E-899). Names round-trip.
    void renderOptionsAndTheirBloom()
    {
        QcRenderOptions options;
        QCOMPARE(options.mode, QcRenderOptions::Mode::Classic);
        QCOMPARE(options.bloomLevels(), 0);
        QCOMPARE(options.occlusionSamples(), 0);
        options.mode = QcRenderOptions::Mode::Realistic;
        QCOMPARE(options.bloomLevels(), QcRenderOptions::mediumBloomLevels);
        QCOMPARE(options.occlusionSamples(), QcRenderOptions::mediumOcclusionSamples);
        options.quality = QcRenderOptions::Quality::Low;
        QCOMPARE(options.bloomLevels(), 0);
        // Low has no ambient occlusion (decision E-902).
        QCOMPARE(options.occlusionSamples(), 0);
        options.quality = QcRenderOptions::Quality::High;
        QCOMPARE(options.bloomLevels(), QcRenderOptions::highBloomLevels);
        QCOMPARE(options.occlusionSamples(), QcRenderOptions::highOcclusionSamples);
        QVERIFY(QcRenderOptions::highBloomLevels > QcRenderOptions::mediumBloomLevels);
        QVERIFY(QcRenderOptions::highOcclusionSamples > QcRenderOptions::mediumOcclusionSamples);

        for (const auto mode : {QcRenderOptions::Mode::Classic, QcRenderOptions::Mode::Realistic}) {
            QCOMPARE(QcRenderOptions::modeFromName(QcRenderOptions::modeName(mode)), mode);
        }
        for (const auto quality : {QcRenderOptions::Quality::Low, QcRenderOptions::Quality::Medium, QcRenderOptions::Quality::High}) {
            QCOMPARE(QcRenderOptions::qualityFromName(QcRenderOptions::qualityName(quality)), quality);
        }
        QVERIFY(!QcRenderOptions::modeFromName(u"Realistic"_s));
        QVERIFY(QcRenderOptions::isExposure(QcRenderOptions::minExposure));
        QVERIFY(QcRenderOptions::isExposure(QcRenderOptions::maxExposure));
        QVERIFY(!QcRenderOptions::isExposure(QcRenderOptions::maxExposure + 0.01));
        QVERIFY(!QcRenderOptions::isExposure(qQNaN()));
    }

    // The sun shadows (decision E-903) grow with the quality: more cascades, a finer map, a wider
    // filter and a longer distance; none in the classic mode.
    void renderOptionsAndTheirShadows()
    {
        QcRenderOptions options;
        QCOMPARE(options.shadows(), QcRenderOptions::Shadows{});
        options.mode = QcRenderOptions::Mode::Realistic;
        QcRenderOptions::Shadows previous;
        for (const auto quality : {QcRenderOptions::Quality::Low, QcRenderOptions::Quality::Medium, QcRenderOptions::Quality::High}) {
            options.quality = quality;
            const QcRenderOptions::Shadows shadows = options.shadows();
            QVERIFY(shadows.cascades >= 1 && shadows.cascades <= QcRenderOptions::maxShadowCascades);
            QVERIFY(shadows.cascades > previous.cascades);
            QVERIFY(shadows.resolution > previous.resolution);
            QVERIFY(shadows.distance > previous.distance);
            QVERIFY(shadows.filterTaps > previous.filterTaps);
            previous = shadows;
        }
        QCOMPARE(previous.cascades, QcRenderOptions::maxShadowCascades);
    }

    void renderOptionsAndTheirWater()
    {
        QcRenderOptions options;
        QVERIFY(!options.water().drawn());
        QCOMPARE(options.water(), QcRenderOptions::Water{});
        options.mode = QcRenderOptions::Mode::Realistic;
        options.quality = QcRenderOptions::Quality::Low;
        const QcRenderOptions::Water low = options.water();
        QVERIFY(low.drawn());
        QCOMPARE(low.refraction, 0.0f);
        QCOMPARE(low.reflectionSteps, 0);
        QCOMPARE(low.caustics, 0.0f);
        options.quality = QcRenderOptions::Quality::Medium;
        const QcRenderOptions::Water medium = options.water();
        QVERIFY(medium.waveOctaves > low.waveOctaves);
        QVERIFY(medium.refraction > 0.0f);
        QVERIFY(medium.caustics > 0.0f);
        QCOMPARE(medium.reflectionSteps, 0);
        options.quality = QcRenderOptions::Quality::High;
        const QcRenderOptions::Water high = options.water();
        QVERIFY(high.waveOctaves > medium.waveOctaves);
        QVERIFY(high.reflectionSteps > 0);
    }

    // The material maps (decision E-908): none in the classic mode and on Low, the maps on Medium,
    // also the moving lights' highlights on High.
    void renderOptionsAndTheirMaterials()
    {
        QcRenderOptions options;
        options.quality = QcRenderOptions::Quality::High;
        QCOMPARE(options.materials(), QcRenderOptions::Materials{});
        options.mode = QcRenderOptions::Mode::Realistic;
        options.quality = QcRenderOptions::Quality::Low;
        QCOMPARE(options.materials(), QcRenderOptions::Materials{});
        options.quality = QcRenderOptions::Quality::Medium;
        QCOMPARE(options.materials(), (QcRenderOptions::Materials{.maps = true, .lightHighlights = false}));
        options.quality = QcRenderOptions::Quality::High;
        QCOMPARE(options.materials(), (QcRenderOptions::Materials{.maps = true, .lightHighlights = true}));
    }

    void clampHelpers()
    {
        QCOMPARE(QcGameSettings::clampViewDistance(0), QcGameSettings::minViewDistance);
        QCOMPARE(QcGameSettings::clampFieldOfView(std::numeric_limits<double>::infinity()), QcGameSettings::defaultFieldOfView);
        QCOMPARE(QcGameSettings::clampMouseSensitivity(-1.0), QcGameSettings::minMouseSensitivity);
        QCOMPARE(QcGameSettings::cleanLanguage(u"de"_s), u"de"_s);
        QCOMPARE(QcGameSettings::cleanLanguage(u"DE"_s), QString());
        QCOMPARE(QcGameSettings::cleanLanguage(QString()), QString());
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "test_settings.moc"
