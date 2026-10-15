#pragma once

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
    // The water lists of the sections with their own pipeline; without this step the translucent
    // step draws them, sorted together with the other translucent faces.
    Water,
    // Translucent section faces (glass, ice), back to front.
    Translucent,
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
        return {{QcRenderPass::Sky, QcRenderPass::Opaque, QcRenderPass::Entities, QcRenderPass::Translucent, QcRenderPass::Overlay}, false};
    }

    // The plan of this run: today's mode; QTCRAFT_SECTION_LIGHT=1 also uploads the per-cell light of
    // the sections (a test hook: no shader reads it yet, decision E-390). The renderer draws it, the
    // mesh scheduler follows its sectionLight.
    static QcFramePlan selected()
    {
        QcFramePlan plan = classic();
        plan.sectionLight = qEnvironmentVariableIntValue("QTCRAFT_SECTION_LIGHT") == 1;
        return plan;
    }

    bool contains(QcRenderPass pass) const { return passes.contains(pass); }
    // Whether the translucent step draws the water lists too.
    bool waterWithTranslucent() const { return !contains(QcRenderPass::Water); }

    QcRenderTarget targetOf(QcRenderPass pass) const
    {
        if (pass == QcRenderPass::Shadow) {
            return QcRenderTarget::ShadowMap;
        }
        if (pass == QcRenderPass::PostProcess || !contains(QcRenderPass::PostProcess)) {
            return QcRenderTarget::Output;
        }
        return QcRenderTarget::Scene;
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
