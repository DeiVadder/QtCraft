// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/music/QcMusicLibrary.h"
#include "clientcore/music/QcMusicSource.h"

#include <QRandomGenerator>
#include <QString>
#include <QStringList>

#include <memory>

/*! \brief Decides when which music plays (decision E-1137), like the role model: a piece, then a
    random pause of several minutes, then the next one for where the player is. Time passes only
    through advance(), so tests step it by hand.

    A track plays for the most specific place (QcMusicDirector::Situation) that has one; without
    any, or with QcMusicSource::Generated, a composed piece plays (not in the menus, which stay
    silent then). Moving within the same family of places (overworld day, night and caves; the
    biomes of one dimension) lets the piece play on, a new family (another dimension, the menu)
    or another source fades it out and the next starts after a short delay. */
class QcMusicDirector
{
public:
    /*! \brief A random pause, from \c {shortest} to \c {longest} seconds. \qcfields */
    struct Pause
    {
        double shortest = 0.0;
        double longest = 0.0;
    };

    /*! After a track in a world (the role model: 12000 to 24000 ticks). */
    static constexpr Pause trackPause{600.0, 1200.0};
    /*! After a track in the menu (the role model: 20 to 600 ticks). */
    static constexpr Pause menuPause{1.0, 30.0};
    /*! After a composed piece (the pause of the pieces before tracks existed). */
    static constexpr Pause generatedPause{60.0, 180.0};
    /*! Before the first piece once the music is on. */
    static constexpr Pause firstDelay{15.0, 30.0};
    /*! After a fade for a new family of places or another source. */
    static constexpr Pause changeDelay{5.0, 15.0};
    /*! Seconds a piece takes to fade out. */
    static constexpr double fadeSeconds = 4.0;
    /*! Cave level (QcAmbience) from which the overworld's cave tracks play. */
    static constexpr float caveLevel = 0.5f;

    /*! \brief Where the player is, as the music sees it. \qcfields */
    struct Situation
    {
        /*! The places that fit, the most specific first (QcMusicLibrary::slotName()). */
        QStringList places;
        /*! The family of places: "menu" or the dimension's slot; a new family fades the piece. */
        QString family;

        friend bool operator==(const Situation &, const Situation &) = default;
    };

    /*! \brief What the player should do now. \qcfields */
    struct Action
    {
        /*! What to do. */
        enum class Kind : quint8 {
            None,          // Nothing.
            PlayTrack,     // Open and play the track.
            PlayGenerated, // Compose and play a piece.
            FadeOut,       // Fade the playing piece out.
        };

        Kind kind = Kind::None;
        /*! The track to play (QcMusicTrack::id) for PlayTrack. */
        QString track;

        friend bool operator==(const Action &, const Action &) = default;
    };

    /*! What the director is doing. */
    enum class State : quint8 {
        Off,     // Music is off (no world, no menu music asked for).
        Waiting, // A pause before the next piece.
        Asking,  // A piece was asked for; started() or failed() follows.
        Playing, // A piece plays.
        Fading,  // A piece fades out.
    };

    /*! A director drawing its pauses and tracks from \a seed. */
    explicit QcMusicDirector(quint64 seed);

    /*! Returns the menus' situation. */
    [[nodiscard]] static Situation menu();
    /*! Returns the situation in a world: in \a dimension (its name, "qtcraft:overworld"), in the
        biome \a biome (its name; empty when unknown), at \a worldTime (seconds, QcDayCycle), under
        a ceiling with the cave ambience at \a cave (0..1). Day, night and cave count in the
        overworld only. */
    [[nodiscard]] static Situation world(const QString &dimension, const QString &biome, double worldTime, float cave);

    /*! Plays the tracks of \a library from now on (null: none). */
    void setLibrary(std::shared_ptr<const QcMusicLibrary> library) { m_library = std::move(library); }
    /*! Returns the tracks it plays (null: none). */
    [[nodiscard]] const std::shared_ptr<const QcMusicLibrary> &library() const { return m_library; }
    /*! Takes the music from \a source; a piece playing fades out. */
    void setSource(QcMusicSource source);
    /*! Returns where the music comes from. */
    [[nodiscard]] QcMusicSource source() const { return m_source; }
    /*! The player is in \a next now. */
    void setSituation(const Situation &next);
    /*! Returns where the player is. */
    [[nodiscard]] const Situation &situation() const { return m_situation; }
    /*! Switches the music on (\a enabled; the first piece after firstDelay) or off. */
    void setEnabled(bool enabled);

    /*! Lets \a seconds pass and returns what to do now. */
    [[nodiscard]] Action advance(double seconds);
    /*! The piece asked for plays now, for \a seconds. Returns false when it must not play: the
        place's family or the source changed while it was asked for (a short pause follows), or
        \a seconds is not positive (a pause follows as after a failed piece); the player then
        drops it. */
    [[nodiscard]] bool started(double seconds);
    /*! The piece asked for cannot play: a pause follows as after it. */
    void failed();
    /*! The piece stopped from outside (a new output device): the next one follows after
        firstDelay. */
    void interrupted();

    /*! Returns what the director is doing. */
    [[nodiscard]] State state() const { return m_state; }
    /*! Returns the seconds until the pause, piece or fade ends. */
    [[nodiscard]] double remaining() const { return m_remaining; }
    /*! Returns the last track asked for (empty: none). */
    [[nodiscard]] const QString &lastTrack() const { return m_lastTrack; }

private:
    [[nodiscard]] double draw(Pause pause);
    // The pause after the piece that played (or was asked for).
    [[nodiscard]] Pause pauseAfterPiece() const;
    // The next piece for the situation, or a pause in the menu without tracks.
    [[nodiscard]] Action choose();
    // A track of the most specific slot that has one, not the last one when there is another.
    [[nodiscard]] QString pickTrack();
    // A piece plays: it fades with the next advance(); else a waiting pause gets shorter.
    void change();
    // Enters `state` for `seconds` (a pause, piece or fade).
    void setState(State state, double seconds);
    void setRemaining(double seconds) { m_remaining = seconds; }
    [[nodiscard]] bool generated() const { return m_generated; }
    void setGenerated(bool generated) { m_generated = generated; }
    [[nodiscard]] bool fadeRequested() const { return m_fadeRequested; }
    void setFadeRequested(bool requested) { m_fadeRequested = requested; }

    std::shared_ptr<const QcMusicLibrary> m_library;
    QcMusicSource m_source = QcMusicSource::Tracks;
    Situation m_situation = menu();
    State m_state = State::Off;
    double m_remaining = 0.0;
    // The piece playing (or asked for) is composed.
    bool m_generated = false;
    bool m_fadeRequested = false;
    QString m_lastTrack;
    QRandomGenerator m_random;
};
