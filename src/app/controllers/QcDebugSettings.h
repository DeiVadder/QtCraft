#pragma once

#include "clientcore/settings/QcDebugOptions.h"

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

// The debug display's level and visualisations (GameController.settings.debug, decision E-225):
// the debug key cycles the level, the debug options switch the visualisations; both are stored
// with the other settings.
class QcDebugSettings : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // 0 off, 1 compact, 2 full.
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged FINAL)
    Q_PROPERTY(bool chunkBorders READ chunkBorders WRITE setChunkBorders NOTIFY visualsChanged FINAL)
    Q_PROPERTY(bool hitboxes READ hitboxes WRITE setHitboxes NOTIFY visualsChanged FINAL)
    Q_PROPERTY(bool lightValues READ lightValues WRITE setLightValues NOTIFY visualsChanged FINAL)
    Q_PROPERTY(bool mobGoals READ mobGoals WRITE setMobGoals NOTIFY visualsChanged FINAL)
    Q_PROPERTY(bool remeshFlash READ remeshFlash WRITE setRemeshFlash NOTIFY visualsChanged FINAL)

public:
    using Visual = QcDebugOptions::Visual;

    explicit QcDebugSettings(QObject *parent = nullptr);

    const QcDebugOptions &options() const { return m_options; }
    void load(QcDebugOptions options);

    int level() const { return int(m_options.level); }
    void setLevel(int level);
    QcDebugOptions::Level levelValue() const { return m_options.level; }
    // Off, compact, full, off ...
    void cycleLevel();
    bool shows(Visual visual) const { return m_options.shows(visual); }
    void setShown(Visual visual, bool on);

    bool chunkBorders() const { return shows(Visual::ChunkBorders); }
    void setChunkBorders(bool on) { setShown(Visual::ChunkBorders, on); }
    bool hitboxes() const { return shows(Visual::Hitboxes); }
    void setHitboxes(bool on) { setShown(Visual::Hitboxes, on); }
    bool lightValues() const { return shows(Visual::LightValues); }
    void setLightValues(bool on) { setShown(Visual::LightValues, on); }
    bool mobGoals() const { return shows(Visual::MobGoals); }
    void setMobGoals(bool on) { setShown(Visual::MobGoals, on); }
    bool remeshFlash() const { return shows(Visual::RemeshFlash); }
    void setRemeshFlash(bool on) { setShown(Visual::RemeshFlash, on); }

signals:
    void levelChanged();
    void visualsChanged();

private:
    QcDebugOptions m_options;
};
