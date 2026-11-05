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
// itself), switch hard, or with a transition (87.3, shown but not offered yet); while switching,
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
    // Whether the switch with a transition can be chosen (87.3: not yet).
    Q_PROPERTY(bool blendAvailable READ blendAvailable CONSTANT FINAL)
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
    QString stageText() const;
    double progress() const { return m_progress; }
    bool cancelling() const { return m_cancel.load(); }
    const QString &error() const { return m_error; }
    static constexpr bool blendAvailable() { return false; }
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
    // The world `folder` is to open now (kept or switched).
    void playRequested(const QString &folder);

private:
    void setState(State state);
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
    std::atomic<bool> m_cancel{false};
    std::unique_ptr<QThread> m_thread;
};
