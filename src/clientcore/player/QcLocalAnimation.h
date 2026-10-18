#pragma once

#include "clientcore/meshing/QcEntityPose.h"
#include "clientcore/player/QcHandTimers.h"
#include "clientcore/session/QcClientEntities.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcEntityMotion.h"

class QcPrediction;

// Animation of the local player between server updates: the timers of the arm swing, using,
// switching the held item and landing (in ticks, negative while not running) and the distance
// walked for the stride. The poses for the renderer come from these timers and the predicted
// motion; poseOf() builds the pose of a mirrored entity the same way.
class QcLocalAnimation
{
public:
    // Animation lengths in ticks; those of the hand are QcHandTimers'.
    static constexpr int swingTicks = QcClientEntities::swingTicks;
    static constexpr int deathTicks = QcClientEntities::deathTicks;

    // Advances the timers by one tick. Holding another item (qcNoItem: empty hand) starts the
    // switch; `mining` keeps the arm swinging.
    void tick(QcItemId held, bool mining);
    // After a predicted step on foot: the distance walked and a landing.
    void stepped(const QcEntityMotion &previous, const QcEntityMotion &current);

    // Starts the arm swing or the use animation.
    void swing() { m_swingTicks = 0; }
    void use() { m_useTicks = 0; }

    // Pose of the local player from the prediction; `alpha` interpolates within the current tick,
    // `drawProgress` is the drawn launcher's (QcClientLauncher::progress).
    QcEntityPose localPose(double alpha, const QcPrediction &prediction, double drawProgress) const;
    QcHandTimers handTimers(double alpha) const;
    // Pose of a mirrored entity from its server state and client timers.
    static QcEntityPose poseOf(const QcClientEntity &entity, double alpha);

private:
    int m_swingTicks = -1;
    int m_useTicks = -1;
    int m_switchTicks = -1;
    int m_landingTicks = -1;
    double m_walkDistance = 0.0;
    double m_lastStep = 0.0;
    QcItemId m_lastHeld = qcNoItem;
};
