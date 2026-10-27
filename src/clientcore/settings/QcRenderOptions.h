#pragma once

#include <QString>

#include <optional>

class QSettings;

// How the world is drawn (decision E-899): the classic look or the realistic render mode (stage 45)
// with its quality and exposure. Stored in settings.ini as render/<key>; a value that makes no
// sense gives the default.
struct QcRenderOptions
{
    // Classic: the picture of the game as it always was, straight into the window. Realistic: the
    // world into an HDR picture first, with height fog, bloom and tonemapping on the way to the window.
    enum class Mode { Classic, Realistic };
    // What the realistic mode spends on effects: Low without bloom, Medium with a bloom of
    // mediumBloomLevels, High of highBloomLevels (wider); later stages add their effects by quality.
    enum class Quality { Low, Medium, High };

    static constexpr double defaultExposure = 1.0;
    static constexpr double minExposure = 0.5;
    static constexpr double maxExposure = 2.0;
    static constexpr int mediumBloomLevels = 5;
    static constexpr int highBloomLevels = 7;

    Mode mode = Mode::Classic;
    Quality quality = Quality::Medium;
    // Brightness of the realistic mode before tonemapping (1: the classic brightness of the mid tones).
    double exposure = defaultExposure;

    bool realistic() const { return mode == Mode::Realistic; }
    // The halvings of the bloom chain, 0 for none (the classic mode and quality Low).
    int bloomLevels() const;

    // "classic", "realistic"; "low", "medium", "high".
    static QString modeName(Mode mode);
    [[nodiscard]] static std::optional<Mode> modeFromName(const QString &name);
    static QString qualityName(Quality quality);
    [[nodiscard]] static std::optional<Quality> qualityFromName(const QString &name);
    // Whether `exposure` is a finite value in [minExposure, maxExposure].
    static bool isExposure(double exposure);

    static QcRenderOptions load(const QSettings &file);
    void save(QSettings &file) const;

    friend bool operator==(const QcRenderOptions &, const QcRenderOptions &) = default;
};
