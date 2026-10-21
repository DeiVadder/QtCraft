#pragma once

#include "clientcore/settings/QcUiPalette.h"

#include <QColor>
#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

// The UI palette (QcUiPalette) for QML as the singleton `Theme`: every screen takes its colours
// from here, so contrast is checked in one place (test_ui_palette).
class QcUiTheme : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON
    Q_PROPERTY(QColor text MEMBER m_text CONSTANT FINAL)
    Q_PROPERTY(QColor textSecondary MEMBER m_textSecondary CONSTANT FINAL)
    Q_PROPERTY(QColor textMuted MEMBER m_textMuted CONSTANT FINAL)
    Q_PROPERTY(QColor heading MEMBER m_heading CONSTANT FINAL)
    Q_PROPERTY(QColor link MEMBER m_link CONSTANT FINAL)
    Q_PROPERTY(QColor warning MEMBER m_warning CONSTANT FINAL)
    Q_PROPERTY(QColor error MEMBER m_error CONSTANT FINAL)
    Q_PROPERTY(QColor textOutline MEMBER m_textOutline CONSTANT FINAL)
    Q_PROPERTY(QColor hudBackdrop MEMBER m_hudBackdrop CONSTANT FINAL)
    Q_PROPERTY(QColor scrim MEMBER m_scrim CONSTANT FINAL)
    Q_PROPERTY(QColor scrimStrong MEMBER m_scrimStrong CONSTANT FINAL)
    Q_PROPERTY(QColor deathScrim MEMBER m_deathScrim CONSTANT FINAL)
    Q_PROPERTY(QColor panel MEMBER m_panel CONSTANT FINAL)
    Q_PROPERTY(QColor panelRaised MEMBER m_panelRaised CONSTANT FINAL)
    Q_PROPERTY(QColor panelBorder MEMBER m_panelBorder CONSTANT FINAL)
    Q_PROPERTY(QColor menuTop MEMBER m_menuTop CONSTANT FINAL)
    Q_PROPERTY(QColor menuBottom MEMBER m_menuBottom CONSTANT FINAL)
    Q_PROPERTY(QColor slot MEMBER m_slot CONSTANT FINAL)
    Q_PROPERTY(QColor hudSlot MEMBER m_hudSlot CONSTANT FINAL)
    Q_PROPERTY(QColor slotHover MEMBER m_slotHover CONSTANT FINAL)
    Q_PROPERTY(QColor slotBorder MEMBER m_slotBorder CONSTANT FINAL)
    Q_PROPERTY(QColor swatchBorder MEMBER m_swatchBorder CONSTANT FINAL)
    Q_PROPERTY(QColor slotPicked MEMBER m_slotPicked CONSTANT FINAL)
    Q_PROPERTY(QColor iconBackdrop MEMBER m_iconBackdrop CONSTANT FINAL)
    Q_PROPERTY(QColor selection MEMBER m_selection CONSTANT FINAL)
    Q_PROPERTY(QColor focusRing MEMBER m_focusRing CONSTANT FINAL)
    Q_PROPERTY(QColor padGlyph MEMBER m_padGlyph CONSTANT FINAL)
    Q_PROPERTY(QColor padGreen MEMBER m_padGreen CONSTANT FINAL)
    Q_PROPERTY(QColor padRed MEMBER m_padRed CONSTANT FINAL)
    Q_PROPERTY(QColor padBlue MEMBER m_padBlue CONSTANT FINAL)
    Q_PROPERTY(QColor padYellow MEMBER m_padYellow CONSTANT FINAL)
    Q_PROPERTY(QColor padPink MEMBER m_padPink CONSTANT FINAL)
    Q_PROPERTY(QColor crosshair MEMBER m_crosshair CONSTANT FINAL)
    Q_PROPERTY(QColor progress MEMBER m_progress CONSTANT FINAL)
    Q_PROPERTY(QColor health MEMBER m_health CONSTANT FINAL)
    Q_PROPERTY(QColor experience MEMBER m_experience CONSTANT FINAL)
    Q_PROPERTY(QColor flame MEMBER m_flame CONSTANT FINAL)
    Q_PROPERTY(QColor flameCore MEMBER m_flameCore CONSTANT FINAL)
    Q_PROPERTY(QColor durabilityGood MEMBER m_durabilityGood CONSTANT FINAL)
    Q_PROPERTY(QColor durabilityLow MEMBER m_durabilityLow CONSTANT FINAL)
    Q_PROPERTY(QColor durabilityBad MEMBER m_durabilityBad CONSTANT FINAL)
    Q_PROPERTY(QColor infoButton MEMBER m_infoButton CONSTANT FINAL)
    Q_PROPERTY(QColor infoButtonHover MEMBER m_infoButtonHover CONSTANT FINAL)
    Q_PROPERTY(QColor separator MEMBER m_separator CONSTANT FINAL)
    Q_PROPERTY(QColor rowHighlight MEMBER m_rowHighlight CONSTANT FINAL)
    Q_PROPERTY(QColor linkBackground MEMBER m_linkBackground CONSTANT FINAL)
    Q_PROPERTY(QColor linkHover MEMBER m_linkHover CONSTANT FINAL)
    Q_PROPERTY(QColor tooltipBackground MEMBER m_tooltipBackground CONSTANT FINAL)
    Q_PROPERTY(QColor graphFrame MEMBER m_graphFrame CONSTANT FINAL)
    Q_PROPERTY(QColor graphTick MEMBER m_graphTick CONSTANT FINAL)
    Q_PROPERTY(QColor graphLimit MEMBER m_graphLimit CONSTANT FINAL)
    Q_PROPERTY(QColor dialFace MEMBER m_dialFace CONSTANT FINAL)
    Q_PROPERTY(QColor dialRim MEMBER m_dialRim CONSTANT FINAL)
    Q_PROPERTY(QColor clockNight MEMBER m_clockNight CONSTANT FINAL)
    Q_PROPERTY(QColor clockDay MEMBER m_clockDay CONSTANT FINAL)
    Q_PROPERTY(QColor clockRim MEMBER m_clockRim CONSTANT FINAL)
    Q_PROPERTY(QColor clockHand MEMBER m_clockHand CONSTANT FINAL)
    Q_PROPERTY(QColor signBoard MEMBER m_signBoard CONSTANT FINAL)
    Q_PROPERTY(QColor signBorder MEMBER m_signBorder CONSTANT FINAL)
    Q_PROPERTY(QColor mapPaper MEMBER m_mapPaper CONSTANT FINAL)
    Q_PROPERTY(QColor mapPaperEdge MEMBER m_mapPaperEdge CONSTANT FINAL)
    Q_PROPERTY(QColor mapMarker MEMBER m_mapMarker CONSTANT FINAL)
    Q_PROPERTY(QColor mapMarkerOther MEMBER m_mapMarkerOther CONSTANT FINAL)
    Q_PROPERTY(QColor clear MEMBER m_clear CONSTANT FINAL)

public:
    explicit QcUiTheme(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

private:
    QColor m_text = QcUiPalette::color(QcUiPalette::Role::Text);
    QColor m_textSecondary = QcUiPalette::color(QcUiPalette::Role::TextSecondary);
    QColor m_textMuted = QcUiPalette::color(QcUiPalette::Role::TextMuted);
    QColor m_heading = QcUiPalette::color(QcUiPalette::Role::Heading);
    QColor m_link = QcUiPalette::color(QcUiPalette::Role::Link);
    QColor m_warning = QcUiPalette::color(QcUiPalette::Role::Warning);
    QColor m_error = QcUiPalette::color(QcUiPalette::Role::Error);
    QColor m_textOutline = QcUiPalette::color(QcUiPalette::Role::TextOutline);
    QColor m_hudBackdrop = QcUiPalette::color(QcUiPalette::Role::HudBackdrop);
    QColor m_scrim = QcUiPalette::color(QcUiPalette::Role::Scrim);
    QColor m_scrimStrong = QcUiPalette::color(QcUiPalette::Role::ScrimStrong);
    QColor m_deathScrim = QcUiPalette::color(QcUiPalette::Role::DeathScrim);
    QColor m_panel = QcUiPalette::color(QcUiPalette::Role::Panel);
    QColor m_panelRaised = QcUiPalette::color(QcUiPalette::Role::PanelRaised);
    QColor m_panelBorder = QcUiPalette::color(QcUiPalette::Role::PanelBorder);
    QColor m_menuTop = QcUiPalette::color(QcUiPalette::Role::MenuTop);
    QColor m_menuBottom = QcUiPalette::color(QcUiPalette::Role::MenuBottom);
    QColor m_slot = QcUiPalette::color(QcUiPalette::Role::Slot);
    QColor m_hudSlot = QcUiPalette::color(QcUiPalette::Role::HudSlot);
    QColor m_slotHover = QcUiPalette::color(QcUiPalette::Role::SlotHover);
    QColor m_slotBorder = QcUiPalette::color(QcUiPalette::Role::SlotBorder);
    QColor m_swatchBorder = QcUiPalette::color(QcUiPalette::Role::SwatchBorder);
    QColor m_slotPicked = QcUiPalette::color(QcUiPalette::Role::SlotPicked);
    QColor m_iconBackdrop = QcUiPalette::color(QcUiPalette::Role::IconBackdrop);
    QColor m_selection = QcUiPalette::color(QcUiPalette::Role::Selection);
    QColor m_focusRing = QcUiPalette::color(QcUiPalette::Role::FocusRing);
    QColor m_padGlyph = QcUiPalette::color(QcUiPalette::Role::PadGlyph);
    QColor m_padGreen = QcUiPalette::color(QcUiPalette::Role::PadGreen);
    QColor m_padRed = QcUiPalette::color(QcUiPalette::Role::PadRed);
    QColor m_padBlue = QcUiPalette::color(QcUiPalette::Role::PadBlue);
    QColor m_padYellow = QcUiPalette::color(QcUiPalette::Role::PadYellow);
    QColor m_padPink = QcUiPalette::color(QcUiPalette::Role::PadPink);
    QColor m_crosshair = QcUiPalette::color(QcUiPalette::Role::Crosshair);
    QColor m_progress = QcUiPalette::color(QcUiPalette::Role::Progress);
    QColor m_health = QcUiPalette::color(QcUiPalette::Role::Health);
    QColor m_experience = QcUiPalette::color(QcUiPalette::Role::Experience);
    QColor m_flame = QcUiPalette::color(QcUiPalette::Role::Flame);
    QColor m_flameCore = QcUiPalette::color(QcUiPalette::Role::FlameCore);
    QColor m_durabilityGood = QcUiPalette::color(QcUiPalette::Role::DurabilityGood);
    QColor m_durabilityLow = QcUiPalette::color(QcUiPalette::Role::DurabilityLow);
    QColor m_durabilityBad = QcUiPalette::color(QcUiPalette::Role::DurabilityBad);
    QColor m_infoButton = QcUiPalette::color(QcUiPalette::Role::InfoButton);
    QColor m_infoButtonHover = QcUiPalette::color(QcUiPalette::Role::InfoButtonHover);
    QColor m_separator = QcUiPalette::color(QcUiPalette::Role::Separator);
    QColor m_rowHighlight = QcUiPalette::color(QcUiPalette::Role::RowHighlight);
    QColor m_linkBackground = QcUiPalette::color(QcUiPalette::Role::LinkBackground);
    QColor m_linkHover = QcUiPalette::color(QcUiPalette::Role::LinkHover);
    QColor m_tooltipBackground = QcUiPalette::color(QcUiPalette::Role::TooltipBackground);
    QColor m_graphFrame = QcUiPalette::color(QcUiPalette::Role::GraphFrame);
    QColor m_graphTick = QcUiPalette::color(QcUiPalette::Role::GraphTick);
    QColor m_graphLimit = QcUiPalette::color(QcUiPalette::Role::GraphLimit);
    QColor m_dialFace = QcUiPalette::color(QcUiPalette::Role::DialFace);
    QColor m_dialRim = QcUiPalette::color(QcUiPalette::Role::DialRim);
    QColor m_clockNight = QcUiPalette::color(QcUiPalette::Role::ClockNight);
    QColor m_clockDay = QcUiPalette::color(QcUiPalette::Role::ClockDay);
    QColor m_clockRim = QcUiPalette::color(QcUiPalette::Role::ClockRim);
    QColor m_clockHand = QcUiPalette::color(QcUiPalette::Role::ClockHand);
    QColor m_signBoard = QcUiPalette::color(QcUiPalette::Role::SignBoard);
    QColor m_signBorder = QcUiPalette::color(QcUiPalette::Role::SignBorder);
    QColor m_mapPaper = QcUiPalette::color(QcUiPalette::Role::MapPaper);
    QColor m_mapPaperEdge = QcUiPalette::color(QcUiPalette::Role::MapPaperEdge);
    QColor m_mapMarker = QcUiPalette::color(QcUiPalette::Role::MapMarker);
    QColor m_mapMarkerOther = QcUiPalette::color(QcUiPalette::Role::MapMarkerOther);
    QColor m_clear = QcUiPalette::color(QcUiPalette::Role::Clear);
};
