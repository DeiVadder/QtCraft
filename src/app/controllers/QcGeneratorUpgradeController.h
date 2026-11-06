// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcConversionControl.h"
#include "worlds/QcGeneratorUpgrade.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <atomic>
#include <functional>
#include <memory>

class QcContentRegistry;
struct QcWorldInfo;

// The question before a saved world made with an older world generator opens (decisions E-1013,
// E-1014; GameController.library.upgrade): keep its version (the default, never switched by
// itself), switch hard, or with a transition of the chosen width; while switching,
// the backup's and the switch's progress with a way to cancel, then the world opens.
class QcGeneratorUpgradeController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GeneratorUpgrade)
    QML_UNCREATABLE("Provided by GameController.library.upgrade")
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString worldName READ worldName NOTIFY stateChanged FINAL)
    // The switches offered: name (the dimension in the content's language), from and to.
    Q_PROPERTY(QVariantList changes READ changes NOTIFY stateChanged FINAL)
    // The same as one text: "Overworld: version 1 → 2", several separated by commas.
    Q_PROPERTY(QString changesText READ changesText NOTIFY stateChanged FINAL)
    Q_PROPERTY(Stage stage READ stage NOTIFY progressChanged FINAL)
    // What the running stage does, for the player.
    Q_PROPERTY(QString stageText READ stageText NOTIFY progressChanged FINAL)
    // The share of the running stage done, 0 to 1; -1 while it cannot count.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    // Cancel was asked for; the switch stops at its next step.
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY progressChanged FINAL)
    // Why the switch failed (state Failed).
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    // The width of the ring of a switch with a transition, in chunks (QcBlendWidth).
    Q_PROPERTY(int blendChunks READ blendChunks WRITE setBlendChunks NOTIFY blendChunksChanged FINAL)
    Q_PROPERTY(int minBlendChunks READ minBlendChunks CONSTANT FINAL)
    Q_PROPERTY(int maxBlendChunks READ maxBlendChunks CONSTANT FINAL)
    Q_PROPERTY(int standardBlendChunks READ standardBlendChunks CONSTANT FINAL)
    // A dimension other than the overworld is offered too: it gets a hard border.
    Q_PROPERTY(bool blendsOverworldOnly READ blendsOverworldOnly NOTIFY stateChanged FINAL)
    // How many worlds askAgain() made ask again; -1 before it ran.
    Q_PROPERTY(int askedAgain READ askedAgain NOTIFY askedAgainChanged FINAL)

public:
    // What the question shows.
    enum class State {
        Idle,
        Asking,
        Converting,
        Failed
    };
    Q_ENUM(State)
    // The running part of a switch (QcConversionControl::Stage).
    enum class Stage {
        Backup,
        Survey,
        Blend,
        Write
    };
    Q_ENUM(Stage)

    // `content` gives the world's base game and mods (loaded on demand), `language` the language of
    // the dimensions' names.
    QcGeneratorUpgradeController(std::function<std::shared_ptr<const QcContentRegistry>()> content, QObject *parent = nullptr);
    ~QcGeneratorUpgradeController() override;
    Q_DISABLE_COPY_MOVE(QcGeneratorUpgradeController)

    State state() const { return m_state; }
    const QString &worldName() const { return m_worldName; }
    QVariantList changes() const;
    QString changesText() const;
    Stage stage() const { return m_stage; }
    bool blendsOverworldOnly() const;
    QString stageText() const;
    double progress() const { return m_progress; }
    bool cancelling() const { return m_cancel.load(); }
    const QString &error() const { return m_error; }
    int blendChunks() const { return m_blendChunks; }
    // Takes `chunks` as the ring's width; a width QcBlendWidth does not allow is refused with a warning.
    void setBlendChunks(int chunks);
    static constexpr int minBlendChunks() { return QcBlendWidth::minChunks; }
    static constexpr int maxBlendChunks() { return QcBlendWidth::maxChunks; }
    static constexpr int standardBlendChunks() { return QcBlendWidth::standardChunks; }
    int askedAgain() const { return m_askedAgain; }
    // The language of the dimensions' names (the player's content language).
    void setLanguage(std::function<QString()> language) { m_language = std::move(language); }

    // Asks about the world in `directory` (folder `folder`, world.json `info`) before it opens when
    // its generator is older than this game's; false when there is nothing to ask (or it was
    // answered with "keep" in this run of the game, or for good with "don't ask again" until a newer
    // version comes): the caller opens it. True without asking while a switch runs: no world opens
    // then (the switch holds the lock of its world).
    [[nodiscard]] bool offer(const QString &folder, const QString &directory, const QcWorldInfo &info);
    // Keeps the world's version and opens it; with `forGood` ("don't ask again") world.json keeps
    // the answer until a newer generator version comes (QcGeneratorUpgrade::keepForGood).
    Q_INVOKABLE void keep(bool forGood);
    // Settings: forgets "don't ask again" in every world of the library (askedAgain says in how many).
    Q_INVOKABLE void askAgain();
    // Switches with hard borders (QcGeneratorUpgrade::Mode::Hard), then opens the world.
    Q_INVOKABLE void switchHard();
    // Switches with a ring of blendChunks chunks (QcGeneratorUpgrade::Mode::Blend), then opens the world.
    Q_INVOKABLE void switchBlend();
    // Stops a running switch; the world stays as it was.
    Q_INVOKABLE void cancel();
    // Closes the message of a failed switch without opening the world.
    Q_INVOKABLE void dismiss();
    // Test hook (--overlay "@<ms> upgrade:<stage>,<share>"): the progress of a switch of the world
    // played last as it would look, for screenshots; nothing runs.
    void preview(Stage stage, double progress);

signals:
    void stateChanged();
    void progressChanged();
    void askedAgainChanged();
    void blendChunksChanged();
    // A real switch began (not a preview).
    void switchStarted();
    // The world `folder` is to open now (kept or switched).
    void playRequested(const QString &folder);

private:
    void setState(State state);
    // Runs the switch with `mode` on its thread.
    void startSwitch(QcGeneratorUpgrade::Mode mode);
    // The progress of `step`, from the switching thread.
    void report(const QcConversionControl::Step &step);
    void setProgress(Stage stage, double progress);
    // Emits nothing: the state change that goes with it does.
    void setError(const QString &error);
    void finish(const QcResult<QcGeneratorUpgrade::Converted> &result);
    // Waits for a running switch to end.
    void stopThread();

    std::function<std::shared_ptr<const QcContentRegistry>()> m_content;
    std::function<QString()> m_language;
    State m_state = State::Idle;
    QString m_folder;
    QString m_directory;
    QString m_worldName;
    QList<QcGeneratorUpgrade::Change> m_changes;
    // Folders whose question was answered with "keep" in this run.
    QSet<QString> m_kept;
    Stage m_stage = Stage::Backup;
    double m_progress = 0.0;
    QString m_error;
    int m_askedAgain = -1;
    int m_blendChunks = QcBlendWidth::standardChunks;
    std::atomic<bool> m_cancel{false};
    std::unique_ptr<QThread> m_thread;
};
