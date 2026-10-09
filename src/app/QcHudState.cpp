#include "QcHudState.h"

#include "QcClientSession.h"
#include "QcDayCycle.h"
#include "QcMeshScheduler.h"
#include "QcPhysics.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"

using namespace Qt::StringLiterals;

QcHudState::QcHudState(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
{
}

void QcHudState::updateTarget()
{
    const QcClientSession &session = *m_parts.session;
    const QcContentRegistry &content = *session.content();
    const std::optional<QcRaycastHit> &target = session.target();
    const std::optional<quint32> targetEntity = session.targetEntity();
    QString targetLabel;
    m_targetName.clear();
    if (targetEntity) {
        const QcClientEntity entity = session.entities().value(*targetEntity);
        const auto &mobs = content.mobs();
        const bool mob = entity.mobType >= 0 && entity.mobType < mobs.size();
        targetLabel = mob ? content.translate(mobs[entity.mobType].translationKey, m_settings.contentLanguage()) : entity.name;
        if (mob) {
            m_targetName = u"mob:"_s + mobs[entity.mobType].name;
        }
        targetLabel += u" (%1)"_s.arg(qRound(entity.health));
    } else if (target) {
        if (const std::optional<QcBlockId> block = session.world()->blockAt(target->block)) {
            const QcBlockDefinition &definition = content.block(*block);
            targetLabel = content.translate(definition.translationKey, m_settings.contentLanguage());
            m_targetName = definition.name;
        }
    }

    if (targetLabel != m_targetLabel || session.miningProgress() != m_miningProgress) {
        m_targetLabel = targetLabel;
        m_miningProgress = session.miningProgress();
        emit frameStateChanged();
    }
}

void QcHudState::updateTimeOfDay(double alpha)
{
    const double time = m_parts.session->worldTime() + alpha * QcPhysics::tickDuration;
    const QString label = QcDayCycle::isNight(time) ? tr("Night") : tr("Day");
    if (label != m_timeOfDay) {
        m_timeOfDay = label;
        emit frameStateChanged();
    }
}

void QcHudState::updateDebugText(const QcVec3 &feet, double fps)
{
    // The overlay is laid out anew for every change of its text; a few updates a second suffice.
    if (!m_settings.showDebug() || (m_debugClock.isValid() && m_debugClock.elapsed() < debugTextIntervalMs)) {
        return;
    }

    m_debugClock.start();
    const QcMeshScheduler &meshes = *m_parts.meshes;
    const QString debugText = u"%1 FPS | XYZ %2 / %3 / %4 | chunks %5 | meshes %6 (pending %7)"_s.arg(qRound(fps))
                                  .arg(feet.x, 0, 'f', 1)
                                  .arg(feet.y, 0, 'f', 1)
                                  .arg(feet.z, 0, 'f', 1)
                                  .arg(m_parts.session->world()->chunkCount())
                                  .arg(m_parts.view->meshCount())
                                  .arg(meshes.dirtyCount() + meshes.jobsInFlight());
    if (debugText != m_debugText) {
        m_debugText = debugText;
        emit debugTextChanged();
    }
}

void QcHudState::addChatLine(const QString &text)
{
    m_chatLines.append(text);
    // Only the last few lines stay on screen.
    constexpr qsizetype maxChatLines = 8;
    while (m_chatLines.size() > maxChatLines) {
        m_chatLines.removeFirst();
    }
    emit chatLinesChanged();
}
