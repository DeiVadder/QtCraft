// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcGameController.h"
#include "app/settings/QcAudioSettings.h"
#include "app/settings/QcSettingsController.h"
#include "app/settings/QcSoundVolumeModel.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

// The item named `name` below `item` in the visual tree (a Repeater's delegates have no QObject
// parent, so findChild() does not see them); null when there is none.
QQuickItem *findItem(QQuickItem *item, const QString &name)
{
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == name) {
            return child;
        }
        if (QQuickItem *found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

// The real audio page (decision E-1150): one slider per sound category from
// GameController.settings.audio.categories, labelled and set as the model says, and a slider the
// player moves sets that category's volume.
class TestAudioPage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    void slidersFollowTheModelAndSetTheVolume()
    {
        QcGameController game;
        QcAudioSettings &audio = *game.settings()->audio();
        audio.setVolume(QcAudioSettings::SoundCategory::Weather, 0.3);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtCraft\nAudioPage {}\n", QUrl(u"qrc:/test_audio_page.qml"_s));
        const std::unique_ptr<QObject> page(component.createWithInitialProperties({{u"game"_s, QVariant::fromValue(&game)}}));
        QVERIFY2(page, qPrintable(component.errorString()));
        auto *root = qobject_cast<QQuickItem *>(page.get());
        QVERIFY(root);
        const auto slider = [root](int category) { return findItem(root, u"categoryVolume%1"_s.arg(category)); };

        const QcSoundVolumeModel &model = *audio.categories();
        for (int row = 0; row < model.rowCount(); ++row) {
            const QQuickItem *rowSlider = slider(row);
            QVERIFY2(rowSlider, qPrintable(u"no slider for category %1"_s.arg(row)));
            const QModelIndex index = model.index(row);
            QCOMPARE(rowSlider->property("label").toString(), index.data(QcSoundVolumeModel::NameRole).toString());
            QCOMPARE(rowSlider->property("value").toDouble(), index.data(QcSoundVolumeModel::VolumeRole).toDouble());
            QCOMPARE(rowSlider->property("defaultValue").toDouble(), index.data(QcSoundVolumeModel::DefaultVolumeRole).toDouble());
        }
        QVERIFY(!slider(model.rowCount()));
        QCOMPARE(slider(1)->property("label").toString(), u"Weather"_s);
        QCOMPARE(slider(1)->property("value").toDouble(), 0.3);

        // The player moves the "Hostile creatures" slider.
        QQuickItem *hostile = slider(static_cast<int>(QcAudioSettings::SoundCategory::Hostile));
        QVERIFY(hostile);
        QVERIFY(hostile->setProperty("value", 0.4));
        QVERIFY(QMetaObject::invokeMethod(hostile, "moved"));
        QCOMPARE(audio.volume(QcAudioSettings::SoundCategory::Hostile), 0.4);
        QCOMPARE(audio.volume(QcAudioSettings::SoundCategory::Friendly), 1.0);
    }

    // A language change reports the new names of all rows.
    void retranslateReportsTheNames()
    {
        QcGameController game;
        QcSoundVolumeModel &model = *game.settings()->audio()->categories();
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        game.settings()->audio()->retranslate();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 0);
        QCOMPARE(changed.at(0).at(1).value<QModelIndex>().row(), model.rowCount() - 1);
        QCOMPARE(changed.at(0).at(2).value<QList<int>>(), QList<int>{QcSoundVolumeModel::NameRole});
    }

private:
    QTemporaryDir m_data;
};

QTEST_MAIN(TestAudioPage)
#include "test_audio_page.moc"
