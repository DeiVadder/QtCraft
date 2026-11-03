// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/controllers/QcHandbookNavigation.h"
#include "app/models/QcHandbookModel.h"
#include "common/gameplay/QcKeyBindings.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class QcItemIcons;
class QcSettingsController;
class QcWorldLibraryController;
struct QcSessionParts;

// The handbook screen (GameController.handbook), which lies over everything else (menu, world,
// inventory). The handbook is built on first use and again after the content, language or keys
// changed.
class QcHandbookController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QcHandbookModel *model READ model CONSTANT FINAL)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY openChanged FINAL)
    // The entry to show when it opens (empty: where it was left) and a search to start with.
    Q_PROPERTY(QString target READ target NOTIFY openChanged FINAL)
    Q_PROPERTY(QString query READ query NOTIFY openChanged FINAL)
    // The open page, the pages before it, the category or the search (decision E-954).
    Q_PROPERTY(QcHandbookNavigation *navigation READ navigation CONSTANT FINAL)
    // The page of the first steps (the main menu's "Handbook: first steps").
    Q_PROPERTY(QString firstSteps READ firstSteps CONSTANT FINAL)

public:
    static QString firstSteps() { return QStringLiteral("chapter:qtcraft:basics"); }
    QcHandbookController(const QcSessionParts &parts, const QcSettingsController &settings, const QcWorldLibraryController &library,
                         QcItemIcons &icons, QObject *parent = nullptr);

    QcHandbookModel *model() const { return m_model; }
    bool isOpen() const { return m_open; }
    QString target() const { return m_target; }
    QString query() const { return m_query; }
    QcHandbookNavigation *navigation() const { return m_navigation; }

    // Opens the handbook at an entry id or content name ("qtcraft:stone"); anything else becomes a
    // search. Without a target it shows the page where it was left (QcHandbookNavigation::startPage).
    Q_INVOKABLE void open(const QString &target = QString());
    Q_INVOKABLE void close();
    // Builds the handbook again when it is open and its language or content changed.
    void refreshIfOpen();

signals:
    void openChanged();
    // The handbook is about to open and takes keyboard and mouse from the world.
    void opening();

private:
    void ensureHandbook();
    // Without signals: open() and close() emit them in their order.
    void setOpen(bool open) { m_open = open; }
    void setTarget(const QString &target) { m_target = target; }
    // Logs how long the first frame with the handbook took (building the page included).
    void logFirstFrame();

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    const QcWorldLibraryController &m_library;
    QcItemIcons &m_icons;
    QcHandbookModel *m_model = nullptr;
    QcHandbookNavigation *m_navigation = nullptr;
    bool m_open = false;
    QString m_target;
    QString m_query;
    // What the current handbook was built from: content generation, language and keys.
    int m_generation = -1;
    QString m_language;
    QcKeyBindings m_keys;
};
