// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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
    // The entry the screen showed last: kept here, so the screen shows it again after it was
    // unloaded (LazyScreen, decision E-691).
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged FINAL)

public:
    QcHandbookController(const QcSessionParts &parts, const QcSettingsController &settings, const QcWorldLibraryController &library,
                         QcItemIcons &icons, QObject *parent = nullptr);

    QcHandbookModel *model() const { return m_model; }
    bool isOpen() const { return m_open; }
    QString target() const { return m_target; }
    QString query() const { return m_query; }
    QString page() const { return m_page; }
    void setPage(const QString &page);

    // Opens the handbook at an entry id or content name ("qtcraft:stone"); anything else becomes a
    // search.
    Q_INVOKABLE void open(const QString &target = QString());
    Q_INVOKABLE void close();
    // Builds the handbook again when it is open and its language or content changed.
    void refreshIfOpen();

signals:
    void openChanged();
    void pageChanged();
    // The handbook is about to open and takes keyboard and mouse from the world.
    void opening();

private:
    void ensureHandbook();
    // Logs how long the first frame with the handbook took (building the page included).
    void logFirstFrame();

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    const QcWorldLibraryController &m_library;
    QcItemIcons &m_icons;
    QcHandbookModel *m_model = nullptr;
    bool m_open = false;
    QString m_target;
    QString m_query;
    QString m_page;
    // What the current handbook was built from: content generation, language and keys.
    int m_generation = -1;
    QString m_language;
    QcKeyBindings m_keys;
};
