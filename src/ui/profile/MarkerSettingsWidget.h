#pragma once

/**
 * @brief Overlay display settings widget for the Profile Editor
 *
 * Mirrors the overlay's Settings tab with native Qt widgets.
 * Settings are read/written via MarkerSettingsManager.
 * Bidirectional sync: overlay changes → this widget updates, and vice versa.
 *
 * Excluded: Details Tracker toggle (overlay-only, in-game context).
 *
 * DO NOT ADD:
 * - Pack/category toggles (belongs in MarkersTabWidget)
 * - Inline styles (use UIHelpers roles)
 */

#include <QWidget>

class QLabel;
class QSlider;
class QVBoxLayout;
class QPushButton;
class LabeledToggle;
class MarkerSettingsManager;

class MarkerSettingsWidget : public QWidget {
  Q_OBJECT

public:
  explicit MarkerSettingsWidget(MarkerSettingsManager *settings,
                                QWidget *parent = nullptr);

  /**
   * @brief Sync all widget states from MarkerSettingsManager
   * Called when overlay changes settings (via settingsChanged signal)
   */
  void syncFromSettings();

signals:
  void modified();

private:
  void setupUI();
  void connectSignals();
  void updateConditionalVisibility();

  MarkerSettingsManager *m_settings;
  bool m_suppressWrite = false; // Prevent write-back during sync

  // --- General Rendering section ---
  LabeledToggle *m_renderingEnabledToggle = nullptr;
  QWidget *m_renderSubTogglesContainer = nullptr;
  QWidget *m_renderDetailsContainer = nullptr;
  LabeledToggle *m_render3dToggle = nullptr;
  LabeledToggle *m_renderMinimapToggle = nullptr;
  LabeledToggle *m_renderBigMapToggle = nullptr;
  QSlider *m_renderDistSlider = nullptr;
  QLabel *m_renderDistLabel = nullptr;
  LabeledToggle *m_heightFilterToggle = nullptr;
  QWidget *m_heightSettingsContainer = nullptr;
  QSlider *m_heightRangeSlider = nullptr;
  QLabel *m_heightRangeLabel = nullptr;

  // --- Master content container (hidden when Markers & Trails OFF) ---
  QWidget *m_masterContentContainer = nullptr;

  // --- 3D World section ---
  QSlider *m_overlayOpacitySlider = nullptr;
  QLabel *m_overlayOpacityLabel = nullptr;
  QSlider *m_markerScaleSlider = nullptr;
  QLabel *m_markerScaleLabel = nullptr;
  LabeledToggle *m_showDistanceToggle = nullptr;
  QWidget *m_distanceSettingsContainer = nullptr;
  QSlider *m_fontSizeSlider = nullptr;
  QLabel *m_fontSizeLabel = nullptr;
  QSlider *m_distOffsetSlider = nullptr;
  QLabel *m_distOffsetLabel = nullptr;
  LabeledToggle *m_exclusionEnabledToggle = nullptr;
  QWidget *m_exclusionDetailsContainer = nullptr;
  LabeledToggle *m_minimapZoneToggle = nullptr;
  LabeledToggle *m_skillBarZoneToggle = nullptr;
  LabeledToggle *m_chatZoneToggle = nullptr;
  QSlider *m_fadeEdgeSlider = nullptr;
  QLabel *m_fadeEdgeLabel = nullptr;

  // --- Map section ---
  QSlider *m_minimapOpacitySlider = nullptr;
  QLabel *m_minimapOpacityLabel = nullptr;
  QSlider *m_trailWidthSlider = nullptr;
  QLabel *m_trailWidthLabel = nullptr;
  QSlider *m_minimapMarkerScaleSlider = nullptr;
  QLabel *m_minimapMarkerScaleLabel = nullptr;
  QSlider *m_minimapMarkerOpacitySlider = nullptr;
  QLabel *m_minimapMarkerOpacityLabel = nullptr;

  // --- Overlay section ---
  LabeledToggle *m_hideInCombatToggle = nullptr;
  LabeledToggle *m_showInBigMapToggle = nullptr;
};
