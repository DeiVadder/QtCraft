// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/models/QcAdvancementTreeModel.h"
#include "app/session/QcSessionParts.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "worlds/QcLocalServerHost.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

// The tree view's model for QML (decision E-924) across changes of the progress: equal data emits
// nothing (the screen keeps its delegates, focus and scroll), the chosen tab stays on its root when
// a root earlier in the content shows up, the chosen advancement is kept by name, and each node
// carries its parent's place.
class TestAdvancementTreeModel : public QObject
{
    Q_OBJECT

private:
    QcSessionParts m_parts;
    QcAudioEngine m_audio;
    QcWorldLibraryController m_library;
    std::unique_ptr<QcSettingsController> m_settings;
    std::unique_ptr<QcAdvancementTreeModel> m_model;

    static QcAdvancementDefinition advancement(const QString &name, const QString &parent, bool hidden = false)
    {
        QcAdvancementDefinition definition;
        definition.name = name;
        definition.parent = parent;
        definition.icon = u"qtcraft:stick"_s;
        definition.hidden = hidden;
        definition.criteria.append({u"c0"_s, QcAdvancementTrigger::Impossible, {}});
        definition.requirements.append({u"c0"_s});
        return definition;
    }

    // A hidden root first, then an open root with two children.
    static QcContentRegistry content()
    {
        QcContentRegistry registry;
        registry.advancements().add(advancement(u"t:secret"_s, {}, true));
        registry.advancements().add(advancement(u"t:open"_s, {}));
        registry.advancements().add(advancement(u"t:left"_s, u"t:open"_s));
        registry.advancements().add(advancement(u"t:right"_s, u"t:open"_s));
        return registry;
    }

    QString tabName() const { return m_model->tabs().value(m_model->tab()).toMap().value(u"name"_s).toString(); }

private slots:
    void init()
    {
        m_settings = std::make_unique<QcSettingsController>(m_parts, m_library, &m_audio);
        m_model = std::make_unique<QcAdvancementTreeModel>(m_parts, *m_settings);
    }

    void cleanup()
    {
        m_model.reset();
        m_settings.reset();
    }

    void nodesCarryTheirParentsPlace()
    {
        const QcContentRegistry registry = content();
        m_model->show(&registry, {});
        const QVariantList nodes = m_model->nodes();
        QCOMPARE(nodes.size(), qsizetype{3});
        const QVariantMap root = nodes[0].toMap();
        for (qsizetype index = 1; index < nodes.size(); ++index) {
            const QVariantMap node = nodes[index].toMap();
            QCOMPARE(node.value(u"parent"_s).toInt(), 0);
            QCOMPARE(node.value(u"parentColumn"_s).toInt(), root.value(u"column"_s).toInt());
            QCOMPARE(node.value(u"parentRow"_s).toDouble(), root.value(u"row"_s).toDouble());
        }
    }

    void equalDataChangesNothing()
    {
        const QcContentRegistry registry = content();
        QSignalSpy changed(m_model.get(), &QcAdvancementTreeModel::changed);
        m_model->show(&registry, {});
        QCOMPARE(changed.size(), 1);
        m_model->show(&registry, {});
        QCOMPARE(changed.size(), 1);

        // Progress on an advancement that stays the same on screen: still nothing.
        QcAdvancementProgress progress;
        progress.grant(u"t:elsewhere"_s, u"c0"_s);
        m_model->show(&registry, progress);
        QCOMPARE(changed.size(), 1);

        // A reached one changes the screen: one signal, not two.
        progress.grant(u"t:left"_s, u"c0"_s);
        m_model->show(&registry, progress);
        QCOMPARE(changed.size(), 2);
    }

    void selectionAndTabSurviveAnUpdate()
    {
        const QcContentRegistry registry = content();
        m_model->show(&registry, {});
        QCOMPARE(m_model->tabs().size(), qsizetype{1});
        QCOMPARE(tabName(), u"t:open"_s);
        m_model->setSelected(u"t:right"_s);
        QCOMPARE(m_model->selectedIndex(), 2);

        // The hidden root earlier in the content shows up: the tab stays on its root (now the second).
        QSignalSpy changed(m_model.get(), &QcAdvancementTreeModel::changed);
        QcAdvancementProgress progress;
        progress.grant(u"t:secret"_s, u"c0"_s);
        progress.grant(u"t:left"_s, u"c0"_s);
        m_model->show(&registry, progress);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(m_model->tabs().size(), qsizetype{2});
        QCOMPARE(m_model->tab(), 1);
        QCOMPARE(tabName(), u"t:open"_s);
        QCOMPARE(m_model->selected(), u"t:right"_s);
        QCOMPARE(m_model->selectedIndex(), 2);

        // On another tab the chosen one is not shown; back on its tab it is again.
        m_model->setTab(0);
        QCOMPARE(tabName(), u"t:secret"_s);
        QCOMPARE(m_model->selectedIndex(), -1);
        m_model->show(&registry, progress);
        QCOMPARE(tabName(), u"t:secret"_s);
        m_model->setTab(1);
        QCOMPARE(m_model->selectedIndex(), 2);

        // Its root goes away again: the first tab.
        m_model->setTab(0);
        progress = {};
        m_model->show(&registry, progress);
        QCOMPARE(m_model->tab(), 0);
        QCOMPARE(tabName(), u"t:open"_s);
    }
};

QTEST_MAIN(TestAdvancementTreeModel)
#include "test_advancement_tree_model.moc"
