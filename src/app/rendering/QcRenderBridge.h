#pragma once

#include "app/rendering/QcEntityFrameBuilder.h"
#include "clientcore/camera/QcFovEffect.h"
#include "clientcore/camera/QcViewCamera.h"
#include "clientcore/weather/QcDropPool.h"
#include "clientcore/weather/QcPrecipitationField.h"

#include <QString>

#include <array>
#include <memory>
#include <utility>

class QcSettingsController;
class QcTextureAtlas;
struct QcSessionParts;

// Hands the running world to the world view once per frame: the camera, entity and hand meshes,
// the sky colour and the outline of the targeted block; QcEntityFrameBuilder builds the entity and
// hand meshes it hands over. Lives in the GUI thread.
class QcRenderBridge
{
public:
    QcRenderBridge(const QcSessionParts &parts, const QcSettingsController &settings);

    // Texture atlas of the running world's content; without one no entities are drawn.
    void setAtlas(std::shared_ptr<const QcTextureAtlas> atlas) { m_entities.setAtlas(std::move(atlas)); }
    // Test hook: freezes the local player's pose ("walk", "sprint", "swing" or "swing:<progress>", "swim", "jump",
    // "fall", "sneak", "death") for screenshots; the cape then rests on its target for that movement.
    void setForcedPose(const QString &pose) { m_entities.setForcedPose(pose); }

    // The camera at the eye, with the field of view widened while sprinting (decision E-280);
    // `frameSeconds` advances the widening's easing.
    void placeCamera(const QcVec3 &eye, QcCameraMode mode, double frameSeconds);
    // Creatures, dropped items, falling blocks and, seen from outside, the player.
    void updateEntities(double alpha, QcCameraMode mode);
    // The first-person arm with the held item; hidden in the other camera modes.
    void updateHand(double alpha, const QcVec3 &eye, QcCameraMode mode);
    // After placeCamera: the sky of the time of day under the weather and a lightning flash; below
    // the world it darkens with the camera height.
    void updateSky(double alpha);
    // Rain and snow around the eye, advanced by `frameSeconds` (decision E-395).
    void updateWeather(const QcVec3 &eye, double frameSeconds);
    void updateSelection();
    // Photo mode (decision E-692): the outline of the targeted block hides with the HUD, the hand
    // as the settings say.
    void setPhotoMode(bool hideOutline, bool hideHand)
    {
        m_outlineHidden = hideOutline;
        m_handHidden = hideHand;
    }

private:
    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    // Builds the entity and hand meshes this bridge hands to the view.
    QcEntityFrameBuilder m_entities;
    QcFovEffect m_fov;
    QcPrecipitationField m_precipitation{QcPrecipitationField::selectedBudget()};
    QcDropPool m_drops;
    bool m_outlineHidden = false;
    bool m_handHidden = false;
};
