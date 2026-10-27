#pragma once

#include "clientcore/settings/QcRenderOptions.h"

#include <QList>
#include <QtGlobal>

// One step of a frame of the world renderer (decision E-390). A render mode is the list of the steps
// it draws; a later mode adds steps instead of rewriting the frame.
enum class QcRenderPass {
    // Depth of the world seen from the sun into a shadow map (realistic mode, stage 45).
    Shadow,
    // The sky dome behind everything.
    Sky,
    // Opaque and cut-out section faces (and the underside of the world seen from below).
    Opaque,
    // Creatures, players and dropped items.
    Entities,
    // The translucent faces the water may lie in front of (the sections with water, all of them
    // while the eye is under water), drawn into the scene before its copy so the water shows them
    // (realistic mode, decision E-906); the translucent step draws them again over the water.
    TranslucentBehindWater,
    // Copies what the steps before drew (colour and distance) for the water to look through
    // (realistic mode, decision E-906); the steps after it draw on over the same scene.
    Refraction,
    // The water lists of the sections with their own pipeline; without this step the translucent
    // step draws them, sorted together with the other translucent faces.
    Water,
    // Translucent section faces (glass, ice), back to front.
    Translucent,
    // Rain and snow around the camera (decision E-395), over everything the weather falls in front of.
    Weather,
    // Debug lines, the outline of the targeted block and the hand.
    Overlay,
    // Effects over the finished picture (tonemapping, bloom, fog; realistic mode).
    PostProcess,
};

// Where a step draws to. Consecutive steps with the same target share one GPU render pass, which
// clears the target when it begins; a plan therefore draws into each target in one stretch of steps
// and never comes back to it (QcFramePlan::visitsTargetsOnce).
enum class QcRenderTarget {
    // The shadow map of the sun.
    ShadowMap,
    // An offscreen picture that post-processing reads (HDR in the realistic mode).
    Scene,
    // The copy of the scene the water looks through (decision E-906).
    Refraction,
    // The scene again after the copy, keeping what the steps before drew.
    SceneContinued,
    // The item's own render target, shown on screen.
    Output,
};

// The steps of a frame in drawing order and what they read (decision E-390).
struct QcFramePlan
{
    QList<QcRenderPass> passes;
    // The steps read the per-cell light of the sections (QcSectionLightPool); with it on the renderer
    // uploads the light of every section it receives.
    bool sectionLight = false;

    // Today's mode: everything straight into the output, water sorted with the other translucent
    // faces, light only from the vertices.
    static QcFramePlan classic()
    {
        return {{QcRenderPass::Sky, QcRenderPass::Opaque, QcRenderPass::Entities, QcRenderPass::Translucent, QcRenderPass::Weather,
                 QcRenderPass::Overlay},
                false};
    }

    // The realistic mode (stage 45, decision E-899): the sun's shadow map first (decision E-903),
    // then the steps of the classic mode into the HDR scene picture, then post-processing (height
    // fog, bloom, tonemapping) into the output. The water has its own step (decision E-906): after
    // the opaque world, the entities and the translucent faces the water may cover a copy of the
    // scene, which the water looks through, then the water before the other translucent faces.
    static QcFramePlan realistic()
    {
        return {{QcRenderPass::Shadow, QcRenderPass::Sky, QcRenderPass::Opaque, QcRenderPass::Entities,
                 QcRenderPass::TranslucentBehindWater, QcRenderPass::Refraction,
                 QcRenderPass::Water, QcRenderPass::Translucent, QcRenderPass::Weather, QcRenderPass::Overlay,
                 QcRenderPass::PostProcess},
                false};
    }

    // The plan of the render `mode`; QTCRAFT_SECTION_LIGHT=1 also uploads the per-cell light of the
    // sections (a test hook: no shader reads it yet, decision E-390). The renderer draws it, the
    // mesh scheduler follows its sectionLight.
    static QcFramePlan selected(QcRenderOptions::Mode mode = QcRenderOptions::Mode::Classic)
    {
        QcFramePlan plan = mode == QcRenderOptions::Mode::Realistic ? realistic() : classic();
        plan.sectionLight = qEnvironmentVariableIntValue("QTCRAFT_SECTION_LIGHT") == 1;
        return plan;
    }

    bool contains(QcRenderPass pass) const { return passes.contains(pass); }
    // Whether the steps draw into the HDR scene that post-processing finishes (decision E-899).
    bool postProcessed() const { return contains(QcRenderPass::PostProcess); }

    // In the HDR scene of the realistic mode light sources and lava (block.vert, frame uniform
    // animation.y) and the sun (sky.frag, sunDirection.w) shine beyond white by these factors
    // (display values), so that they bloom.
    static constexpr float realisticGlowGain = 1.5f;
    static constexpr float realisticSunGain = 2.0f;
    // The factor of light sources and lava: exactly 1 in a plan without post-processing.
    float glowGain() const { return postProcessed() ? realisticGlowGain : 1.0f; }
    // The sun's extra brightness: exactly 0 in a plan without post-processing.
    float sunGain() const { return postProcessed() ? realisticSunGain : 0.0f; }
    bool operator==(const QcFramePlan &) const = default;
    // Whether the translucent step draws the water lists too.
    bool waterWithTranslucent() const { return !contains(QcRenderPass::Water); }

    // The target of `pass`; the steps after a Refraction step draw on over the scene they copied.
    QcRenderTarget targetOf(QcRenderPass pass) const
    {
        if (pass == QcRenderPass::Shadow) {
            return QcRenderTarget::ShadowMap;
        }
        if (pass == QcRenderPass::Refraction) {
            return QcRenderTarget::Refraction;
        }
        if (pass == QcRenderPass::PostProcess || !contains(QcRenderPass::PostProcess)) {
            return QcRenderTarget::Output;
        }
        const qsizetype copy = passes.indexOf(QcRenderPass::Refraction);
        return copy >= 0 && passes.indexOf(pass) > copy ? QcRenderTarget::SceneContinued : QcRenderTarget::Scene;
    }

    struct Group
    {
        QcRenderTarget target = QcRenderTarget::Output;
        QList<QcRenderPass> passes;
    };

    // Whether every target is drawn by one stretch of consecutive steps. A plan that returns to a
    // target (output, shadow map, output) is invalid: its second render pass would clear what the
    // first one drew.
    bool visitsTargetsOnce() const
    {
        QList<QcRenderTarget> seen;
        for (const QcRenderPass pass : passes) {
            const QcRenderTarget target = targetOf(pass);
            if (!seen.isEmpty() && seen.back() == target) {
                continue;
            }
            if (seen.contains(target)) {
                return false;
            }
            seen.append(target);
        }
        return true;
    }

    // The steps split into GPU render passes: a new one wherever the target changes. Only for plans
    // that visit every target once.
    QList<Group> groups() const
    {
        Q_ASSERT(visitsTargetsOnce());
        QList<Group> result;
        for (const QcRenderPass pass : passes) {
            const QcRenderTarget target = targetOf(pass);
            if (result.isEmpty() || result.back().target != target) {
                result.append({target, {}});
            }
            result.back().passes.append(pass);
        }
        return result;
    }
};
