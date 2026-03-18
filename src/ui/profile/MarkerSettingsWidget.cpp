#include "MarkerSettingsWidget.h"

#include "features/markers/MarkerSettingsManager.h"
#include "ui/ToggleSwitch.h"
#include "ui/UIHelpers.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MarkerSettingsWidget::MarkerSettingsWidget(MarkerSettingsManager *settings,
                                           QWidget *parent)
    : QWidget(parent), m_settings(settings) {
  setupUI();
  connectSignals();
  syncFromSettings();
}

// ---------------------------------------------------------------------------
// UI Setup
// ---------------------------------------------------------------------------

void MarkerSettingsWidget::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(8, 8, 8, 8);
  mainLayout->setSpacing(12);

  // Helper: consistent grid column config for slider rows
  auto configureSliderGrid = [](QGridLayout *g) {
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(6);
    g->setColumnMinimumWidth(0, 105);
    g->setColumnStretch(1, 1);
  };

  // =========================================================================
  // 1. General Rendering (top section — master toggle)
  // =========================================================================
  auto *renderGroup = new QGroupBox("General Rendering");
  UIHelpers::applyRole(renderGroup, "groupBox");
  auto *renderLayout = new QVBoxLayout(renderGroup);
  renderLayout->setSpacing(8);

  // Master toggle — "grandfather" of all marker settings
  m_renderingEnabledToggle = new LabeledToggle("Markers & Trails");
  m_renderingEnabledToggle->setToolTip(
      "Master toggle — disable all marker and trail rendering");
  renderLayout->addWidget(m_renderingEnabledToggle);

  // Sub-toggles (hidden when master OFF)
  m_renderSubTogglesContainer = new QWidget();
  auto *subLayout = new QVBoxLayout(m_renderSubTogglesContainer);
  subLayout->setContentsMargins(16, 0, 0, 0);
  subLayout->setSpacing(4);

  m_render3dToggle = new LabeledToggle("3D World");
  m_render3dToggle->setToolTip("Show markers and trails in the 3D game world");
  m_renderMinimapToggle = new LabeledToggle("Minimap");
  m_renderMinimapToggle->setToolTip("Show markers and trails on the minimap");
  m_renderBigMapToggle = new LabeledToggle("Big Map (M)");
  m_renderBigMapToggle->setToolTip(
      "Show markers and trails on the full-screen map");

  // All 3 sub-toggles on one row
  auto *subRow = new QHBoxLayout();
  subRow->setSpacing(16);
  subRow->addWidget(m_render3dToggle);
  subRow->addWidget(m_renderMinimapToggle);
  subRow->addWidget(m_renderBigMapToggle);
  subRow->addStretch();
  subLayout->addLayout(subRow);

  renderLayout->addWidget(m_renderSubTogglesContainer);

  // Render distance + Height filter container (hidden when master OFF)
  m_renderDetailsContainer = new QWidget();
  auto *renderDetailsLayout = new QVBoxLayout(m_renderDetailsContainer);
  renderDetailsLayout->setContentsMargins(0, 0, 0, 0);
  renderDetailsLayout->setSpacing(8);

  // Render Distance slider
  auto *rdGrid = new QGridLayout();
  configureSliderGrid(rdGrid);

  m_renderDistLabel = UIHelpers::createLabel(this, "200m");
  UIHelpers::applyHintRole(m_renderDistLabel);
  m_renderDistSlider = new QSlider(Qt::Horizontal);
  m_renderDistSlider->setRange(50, 500);
  m_renderDistSlider->setValue(200);
  m_renderDistSlider->setToolTip(
      "Maximum distance at which 3D markers and trails are visible (50m to "
      "500m)");

  rdGrid->addWidget(UIHelpers::createLabel(this, "Render Distance"), 0, 0);
  auto *rdRow = new QHBoxLayout();
  rdRow->addWidget(m_renderDistSlider, 1);
  rdRow->addWidget(m_renderDistLabel);
  rdGrid->addLayout(rdRow, 0, 1);
  renderDetailsLayout->addLayout(rdGrid);

  // Height Filter toggle
  m_heightFilterToggle = new LabeledToggle("Height Filter");
  m_heightFilterToggle->setToolTip(
      "Hide markers on different floors or elevations");
  renderDetailsLayout->addWidget(m_heightFilterToggle);

  // Height Range slider (conditional — shown when Height Filter is ON)
  m_heightSettingsContainer = new QWidget();
  auto *hfGrid = new QGridLayout(m_heightSettingsContainer);
  configureSliderGrid(hfGrid);
  hfGrid->setContentsMargins(16, 0, 0, 0);

  m_heightRangeLabel = UIHelpers::createLabel(this, "20m");
  UIHelpers::applyHintRole(m_heightRangeLabel);
  m_heightRangeSlider = new QSlider(Qt::Horizontal);
  m_heightRangeSlider->setRange(5, 50);
  m_heightRangeSlider->setValue(20);
  m_heightRangeSlider->setToolTip(
      "Vertical range above and below player to show markers (5m to 50m)");

  hfGrid->addWidget(UIHelpers::createLabel(this, "Height Range"), 0, 0);
  auto *hrRow = new QHBoxLayout();
  hrRow->addWidget(m_heightRangeSlider, 1);
  hrRow->addWidget(m_heightRangeLabel);
  hfGrid->addLayout(hrRow, 0, 1);

  renderDetailsLayout->addWidget(m_heightSettingsContainer);
  renderLayout->addWidget(m_renderDetailsContainer);
  mainLayout->addWidget(renderGroup);

  // =========================================================================
  // Master content container (hidden when Markers & Trails is OFF)
  // Contains: 3D World + Map. Overlay stays outside.
  // =========================================================================
  m_masterContentContainer = new QWidget();
  auto *masterLayout = new QVBoxLayout(m_masterContentContainer);
  masterLayout->setContentsMargins(0, 0, 0, 0);
  masterLayout->setSpacing(12);

  // =========================================================================
  // 2. 3D World + Map side-by-side
  // =========================================================================
  auto *sideBySide = new QHBoxLayout();
  sideBySide->setSpacing(12);

  // --- 3D World (left) ---
  auto *worldGroup = new QGroupBox("3D World");
  UIHelpers::applyRole(worldGroup, "groupBox");
  auto *worldLayout = new QVBoxLayout(worldGroup);
  worldLayout->setSpacing(8);

  // Overlay Opacity + Marker Scale
  auto *wg1 = new QGridLayout();
  configureSliderGrid(wg1);

  m_overlayOpacityLabel = UIHelpers::createLabel(this, "100%");
  UIHelpers::applyHintRole(m_overlayOpacityLabel);
  m_overlayOpacitySlider = new QSlider(Qt::Horizontal);
  m_overlayOpacitySlider->setRange(0, 100);
  m_overlayOpacitySlider->setValue(100);
  m_overlayOpacitySlider->setToolTip(
      "Opacity of 3D markers and trails in the game world");

  wg1->addWidget(UIHelpers::createLabel(this, "Overlay Opacity"), 0, 0);
  auto *ooRow = new QHBoxLayout();
  ooRow->addWidget(m_overlayOpacitySlider, 1);
  ooRow->addWidget(m_overlayOpacityLabel);
  wg1->addLayout(ooRow, 0, 1);

  m_markerScaleLabel = UIHelpers::createLabel(this, "1.0\xc3\x97");
  UIHelpers::applyHintRole(m_markerScaleLabel);
  m_markerScaleSlider = new QSlider(Qt::Horizontal);
  m_markerScaleSlider->setRange(50, 300);
  m_markerScaleSlider->setValue(100);
  m_markerScaleSlider->setToolTip(
      "Scale multiplier for 3D marker size (0.5\xc3\x97 to 3.0\xc3\x97)");

  wg1->addWidget(UIHelpers::createLabel(this, "Marker Scale"), 1, 0);
  auto *msRow = new QHBoxLayout();
  msRow->addWidget(m_markerScaleSlider, 1);
  msRow->addWidget(m_markerScaleLabel);
  wg1->addLayout(msRow, 1, 1);

  worldLayout->addLayout(wg1);

  // Distance Labels (inside 3D World)
  m_showDistanceToggle = new LabeledToggle("Show Distance");
  m_showDistanceToggle->setToolTip("Show distance labels on 3D markers");
  worldLayout->addWidget(m_showDistanceToggle);

  m_distanceSettingsContainer = new QWidget();
  auto *distGrid = new QGridLayout(m_distanceSettingsContainer);
  configureSliderGrid(distGrid);
  distGrid->setContentsMargins(16, 0, 0, 0);

  m_fontSizeLabel = UIHelpers::createLabel(this, "12px");
  UIHelpers::applyHintRole(m_fontSizeLabel);
  m_fontSizeSlider = new QSlider(Qt::Horizontal);
  m_fontSizeSlider->setRange(8, 24);
  m_fontSizeSlider->setValue(12);
  m_fontSizeSlider->setToolTip("Font size for distance labels (8px to 24px)");

  distGrid->addWidget(UIHelpers::createLabel(this, "Font Size"), 0, 0);
  auto *fsRow = new QHBoxLayout();
  fsRow->addWidget(m_fontSizeSlider, 1);
  fsRow->addWidget(m_fontSizeLabel);
  distGrid->addLayout(fsRow, 0, 1);

  m_distOffsetLabel = UIHelpers::createLabel(this, "20px");
  UIHelpers::applyHintRole(m_distOffsetLabel);
  m_distOffsetSlider = new QSlider(Qt::Horizontal);
  m_distOffsetSlider->setRange(0, 60);
  m_distOffsetSlider->setValue(20);
  m_distOffsetSlider->setToolTip(
      "Vertical offset of distance labels above markers (0px to 60px)");

  distGrid->addWidget(UIHelpers::createLabel(this, "Label Offset"), 1, 0);
  auto *loRow = new QHBoxLayout();
  loRow->addWidget(m_distOffsetSlider, 1);
  loRow->addWidget(m_distOffsetLabel);
  distGrid->addLayout(loRow, 1, 1);

  worldLayout->addWidget(m_distanceSettingsContainer);

  // Exclusion Zones (inside 3D World)
  m_exclusionEnabledToggle = new LabeledToggle("Exclusion Zones");
  m_exclusionEnabledToggle->setToolTip("Hide markers behind GW2 UI elements");
  worldLayout->addWidget(m_exclusionEnabledToggle);

  m_exclusionDetailsContainer = new QWidget();
  auto *exContainerLayout = new QVBoxLayout(m_exclusionDetailsContainer);
  exContainerLayout->setContentsMargins(16, 0, 0, 0);
  exContainerLayout->setSpacing(4);

  m_minimapZoneToggle = new LabeledToggle("Minimap");
  m_minimapZoneToggle->setToolTip("Hide markers behind the minimap area");
  m_skillBarZoneToggle = new LabeledToggle("Skill Bar");
  m_skillBarZoneToggle->setToolTip("Hide markers behind the skill bar area");
  m_chatZoneToggle = new LabeledToggle("Chat");
  m_chatZoneToggle->setToolTip("Hide markers behind the chat window area");

  auto *exToggleRow1 = new QHBoxLayout();
  exToggleRow1->setSpacing(16);
  exToggleRow1->addWidget(m_minimapZoneToggle);
  exToggleRow1->addWidget(m_skillBarZoneToggle);
  exToggleRow1->addStretch();
  exContainerLayout->addLayout(exToggleRow1);

  auto *exToggleRow2 = new QHBoxLayout();
  exToggleRow2->setSpacing(16);
  exToggleRow2->addWidget(m_chatZoneToggle);
  exToggleRow2->addStretch();
  exContainerLayout->addLayout(exToggleRow2);

  // Fade Edge slider
  auto *feGrid = new QGridLayout();
  configureSliderGrid(feGrid);

  m_fadeEdgeLabel = UIHelpers::createLabel(this, "2%");
  UIHelpers::applyHintRole(m_fadeEdgeLabel);
  m_fadeEdgeSlider = new QSlider(Qt::Horizontal);
  m_fadeEdgeSlider->setRange(0, 5);
  m_fadeEdgeSlider->setValue(2);
  m_fadeEdgeSlider->setToolTip(
      "Gradual fade width at exclusion zone edges (0% to 5%)");

  feGrid->addWidget(UIHelpers::createLabel(this, "Fade Edge"), 0, 0);
  auto *feRow = new QHBoxLayout();
  feRow->addWidget(m_fadeEdgeSlider, 1);
  feRow->addWidget(m_fadeEdgeLabel);
  feGrid->addLayout(feRow, 0, 1);
  exContainerLayout->addLayout(feGrid);

  worldLayout->addWidget(m_exclusionDetailsContainer);
  worldLayout->addStretch();

  sideBySide->addWidget(worldGroup, 1);

  // --- Map (right) ---
  auto *mapGroup = new QGroupBox("Map");
  UIHelpers::applyRole(mapGroup, "groupBox");
  auto *mapLayout = new QVBoxLayout(mapGroup);
  mapLayout->setSpacing(8);

  auto *mg = new QGridLayout();
  configureSliderGrid(mg);

  // Map Opacity
  m_minimapOpacityLabel = UIHelpers::createLabel(this, "100%");
  UIHelpers::applyHintRole(m_minimapOpacityLabel);
  m_minimapOpacitySlider = new QSlider(Qt::Horizontal);
  m_minimapOpacitySlider->setRange(0, 100);
  m_minimapOpacitySlider->setValue(100);
  m_minimapOpacitySlider->setToolTip(
      "Opacity of markers and trails on the minimap and big map");

  mg->addWidget(UIHelpers::createLabel(this, "Map Opacity"), 0, 0);
  auto *moRow = new QHBoxLayout();
  moRow->addWidget(m_minimapOpacitySlider, 1);
  moRow->addWidget(m_minimapOpacityLabel);
  mg->addLayout(moRow, 0, 1);

  // Trail Width
  m_trailWidthLabel = UIHelpers::createLabel(this, "5.0\xc3\x97");
  UIHelpers::applyHintRole(m_trailWidthLabel);
  m_trailWidthSlider = new QSlider(Qt::Horizontal);
  m_trailWidthSlider->setRange(10, 100);
  m_trailWidthSlider->setValue(50);
  m_trailWidthSlider->setToolTip(
      "Width multiplier for trails on minimap and big map (1.0\xc3\x97 to "
      "10.0\xc3\x97)");

  mg->addWidget(UIHelpers::createLabel(this, "Trail Width"), 1, 0);
  auto *twRow = new QHBoxLayout();
  twRow->addWidget(m_trailWidthSlider, 1);
  twRow->addWidget(m_trailWidthLabel);
  mg->addLayout(twRow, 1, 1);

  // Marker Size
  m_minimapMarkerScaleLabel = UIHelpers::createLabel(this, "1.0\xc3\x97");
  UIHelpers::applyHintRole(m_minimapMarkerScaleLabel);
  m_minimapMarkerScaleSlider = new QSlider(Qt::Horizontal);
  m_minimapMarkerScaleSlider->setRange(50, 300);
  m_minimapMarkerScaleSlider->setValue(100);
  m_minimapMarkerScaleSlider->setToolTip(
      "Scale multiplier for markers on minimap and big map (0.5\xc3\x97 to "
      "3.0\xc3\x97)");

  mg->addWidget(UIHelpers::createLabel(this, "Marker Size"), 2, 0);
  auto *mmsRow = new QHBoxLayout();
  mmsRow->addWidget(m_minimapMarkerScaleSlider, 1);
  mmsRow->addWidget(m_minimapMarkerScaleLabel);
  mg->addLayout(mmsRow, 2, 1);

  // Marker Opacity
  m_minimapMarkerOpacityLabel = UIHelpers::createLabel(this, "100%");
  UIHelpers::applyHintRole(m_minimapMarkerOpacityLabel);
  m_minimapMarkerOpacitySlider = new QSlider(Qt::Horizontal);
  m_minimapMarkerOpacitySlider->setRange(0, 100);
  m_minimapMarkerOpacitySlider->setValue(100);
  m_minimapMarkerOpacitySlider->setToolTip(
      "Opacity of markers on the minimap and big map");

  mg->addWidget(UIHelpers::createLabel(this, "Marker Opacity"), 3, 0);
  auto *mmoRow = new QHBoxLayout();
  mmoRow->addWidget(m_minimapMarkerOpacitySlider, 1);
  mmoRow->addWidget(m_minimapMarkerOpacityLabel);
  mg->addLayout(mmoRow, 3, 1);

  mapLayout->addLayout(mg);
  mapLayout->addStretch();

  sideBySide->addWidget(mapGroup, 1);

  masterLayout->addLayout(sideBySide);
  mainLayout->addWidget(m_masterContentContainer);

  // =========================================================================
  // 3. Overlay (always visible — not dependent on master toggle)
  // =========================================================================
  auto *overlayGroup = new QGroupBox("Overlay");
  UIHelpers::applyRole(overlayGroup, "groupBox");
  auto *overlayRow = new QHBoxLayout(overlayGroup);
  overlayRow->setSpacing(16);

  m_hideInCombatToggle = new LabeledToggle("Hide in Combat");
  m_hideInCombatToggle->setToolTip(
      "Hide the entire AIO overlay panel when in combat");
  overlayRow->addWidget(m_hideInCombatToggle);

  m_showInBigMapToggle = new LabeledToggle("Show in Big Map");
  m_showInBigMapToggle->setToolTip(
      "Show the diamond icon and marker packs panel when the big map (M) is "
      "open");
  overlayRow->addWidget(m_showInBigMapToggle);

  // Third column spacer (reserved for future controls)
  overlayRow->addStretch();

  mainLayout->addWidget(overlayGroup);

  mainLayout->addStretch();

  updateConditionalVisibility();
}

// ---------------------------------------------------------------------------
// Signal connections
// ---------------------------------------------------------------------------

void MarkerSettingsWidget::connectSignals() {
  if (!m_settings)
    return;

  // --- Overlay Opacity ---
  connect(m_overlayOpacitySlider, &QSlider::valueChanged, this,
          [this](int val) {
            m_overlayOpacityLabel->setText(QString::number(val) + "%");
            if (!m_suppressWrite) {
              m_settings->setOverlayOpacity(val / 100.0);
              emit modified();
            }
          });

  // --- Minimap Opacity ---
  connect(m_minimapOpacitySlider, &QSlider::valueChanged, this,
          [this](int val) {
            m_minimapOpacityLabel->setText(QString::number(val) + "%");
            if (!m_suppressWrite) {
              m_settings->setMinimapOpacity(val / 100.0);
              emit modified();
            }
          });

  // --- Marker Scale ---
  connect(m_markerScaleSlider, &QSlider::valueChanged, this, [this](int val) {
    qreal scale = val / 100.0;
    m_markerScaleLabel->setText(QString::number(scale, 'f', 1) + "×");
    if (!m_suppressWrite) {
      m_settings->setMarkerScale(scale);
      emit modified();
    }
  });

  // --- Show Distance ---
  connect(m_showDistanceToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setShowDistance(checked);
              emit modified();
            }
            updateConditionalVisibility();
          });

  // --- Hide in Combat ---
  connect(m_hideInCombatToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setHideInCombat(checked);
              emit modified();
            }
          });

  connect(m_showInBigMapToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setShowInBigMap(checked);
              emit modified();
            }
          });

  // --- Rendering Toggles ---
  connect(m_renderingEnabledToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setRenderingEnabled(checked);
              emit modified();
            }
            updateConditionalVisibility();
          });
  connect(m_render3dToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setRender3dEnabled(checked);
              emit modified();
            }
          });
  connect(m_renderMinimapToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setRenderMinimapEnabled(checked);
              emit modified();
            }
          });
  connect(m_renderBigMapToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setRenderBigMapEnabled(checked);
              emit modified();
            }
          });

  // --- Font Size ---
  connect(m_fontSizeSlider, &QSlider::valueChanged, this, [this](int val) {
    m_fontSizeLabel->setText(QString::number(val) + "px");
    if (!m_suppressWrite) {
      m_settings->setDistanceFontSize(val);
      emit modified();
    }
  });

  // --- Distance Offset ---
  connect(m_distOffsetSlider, &QSlider::valueChanged, this, [this](int val) {
    m_distOffsetLabel->setText(QString::number(val) + "px");
    if (!m_suppressWrite) {
      m_settings->setDistanceLabelOffset(val);
      emit modified();
    }
  });

  // --- Exclusion Enabled ---
  connect(m_exclusionEnabledToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setExclusionEnabled(checked);
              emit modified();
            }
            updateConditionalVisibility();
          });

  // --- Minimap Zone ---
  connect(m_minimapZoneToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setMinimapZoneEnabled(checked);
              emit modified();
            }
          });

  // --- Skill Bar Zone ---
  connect(m_skillBarZoneToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setSkillBarZoneEnabled(checked);
              emit modified();
            }
          });

  // --- Chat Zone ---
  connect(m_chatZoneToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setChatZoneEnabled(checked);
              emit modified();
            }
          });

  // --- Fade Edge ---
  connect(m_fadeEdgeSlider, &QSlider::valueChanged, this, [this](int val) {
    m_fadeEdgeLabel->setText(QString::number(val) + "%");
    if (!m_suppressWrite) {
      m_settings->setExclusionFadeEdge(val / 100.0f);
      emit modified();
    }
  });

  // --- Render Distance ---
  connect(m_renderDistSlider, &QSlider::valueChanged, this, [this](int val) {
    m_renderDistLabel->setText(QString::number(val) + "m");
    if (!m_suppressWrite) {
      m_settings->setMaxRenderDistance(static_cast<qreal>(val));
      emit modified();
    }
  });

  // --- Height Filter ---
  connect(m_heightFilterToggle, &LabeledToggle::toggled, this,
          [this](bool checked) {
            if (!m_suppressWrite) {
              m_settings->setHeightFilterEnabled(checked);
              emit modified();
            }
            updateConditionalVisibility();
          });

  // --- Height Range ---
  connect(m_heightRangeSlider, &QSlider::valueChanged, this, [this](int val) {
    m_heightRangeLabel->setText(QString::number(val) + "m");
    if (!m_suppressWrite) {
      m_settings->setHeightFilterRange(static_cast<float>(val));
      emit modified();
    }
  });

  // --- Trail Width ---
  connect(m_trailWidthSlider, &QSlider::valueChanged, this, [this](int val) {
    float width = val / 10.0f;
    m_trailWidthLabel->setText(
        QString::number(static_cast<double>(width), 'f', 1) +
        QString::fromUtf8("\xc3\x97"));
    if (!m_suppressWrite) {
      m_settings->setMinimapTrailWidth(width);
      emit modified();
    }
  });

  // --- Map Marker Size ---
  connect(m_minimapMarkerScaleSlider, &QSlider::valueChanged, this,
          [this](int val) {
            qreal scale = val / 100.0;
            m_minimapMarkerScaleLabel->setText(
                QString::number(scale, 'f', 1) +
                QString::fromUtf8("\xc3\x97"));
            if (!m_suppressWrite) {
              m_settings->setMinimapMarkerScale(scale);
              emit modified();
            }
          });

  // --- Map Marker Opacity ---
  connect(m_minimapMarkerOpacitySlider, &QSlider::valueChanged, this,
          [this](int val) {
            m_minimapMarkerOpacityLabel->setText(QString::number(val) + "%");
            if (!m_suppressWrite) {
              m_settings->setMinimapMarkerOpacity(val / 100.0);
              emit modified();
            }
          });
}

// ---------------------------------------------------------------------------
// Sync from settings (bidirectional — overlay → AIO)
// ---------------------------------------------------------------------------

void MarkerSettingsWidget::syncFromSettings() {
  if (!m_settings)
    return;

  m_suppressWrite = true;

  m_overlayOpacitySlider->setValue(qRound(m_settings->overlayOpacity() * 100));
  m_minimapOpacitySlider->setValue(qRound(m_settings->minimapOpacity() * 100));
  m_markerScaleSlider->setValue(qRound(m_settings->markerScale() * 100));

  m_showDistanceToggle->toggle()->setChecked(m_settings->showDistance());
  m_hideInCombatToggle->toggle()->setChecked(m_settings->hideInCombat());
  m_showInBigMapToggle->toggle()->setChecked(m_settings->showInBigMap());

  m_renderingEnabledToggle->toggle()->setChecked(
      m_settings->renderingEnabled());
  m_render3dToggle->toggle()->setChecked(m_settings->render3dEnabled());
  m_renderMinimapToggle->toggle()->setChecked(
      m_settings->renderMinimapEnabled());
  m_renderBigMapToggle->toggle()->setChecked(
      m_settings->renderBigMapEnabled());

  m_fontSizeSlider->setValue(m_settings->distanceFontSize());
  m_distOffsetSlider->setValue(m_settings->distanceLabelOffset());

  m_exclusionEnabledToggle->toggle()->setChecked(
      m_settings->exclusionEnabled());
  m_minimapZoneToggle->toggle()->setChecked(m_settings->minimapZoneEnabled());
  m_skillBarZoneToggle->toggle()->setChecked(m_settings->skillBarZoneEnabled());
  m_chatZoneToggle->toggle()->setChecked(m_settings->chatZoneEnabled());
  m_fadeEdgeSlider->setValue(qRound(m_settings->exclusionFadeEdge() * 100.0f));

  m_renderDistSlider->setValue(qRound(m_settings->maxRenderDistance()));
  m_heightFilterToggle->toggle()->setChecked(m_settings->heightFilterEnabled());
  m_heightRangeSlider->setValue(qRound(m_settings->heightFilterRange()));
  m_trailWidthSlider->setValue(qRound(m_settings->minimapTrailWidth() * 10));
  m_minimapMarkerScaleSlider->setValue(
      qRound(m_settings->minimapMarkerScale() * 100));
  m_minimapMarkerOpacitySlider->setValue(
      qRound(m_settings->minimapMarkerOpacity() * 100));

  m_suppressWrite = false;

  updateConditionalVisibility();
}

// ---------------------------------------------------------------------------
// Conditional visibility
// ---------------------------------------------------------------------------

void MarkerSettingsWidget::updateConditionalVisibility() {
  // Master "grandfather" toggle
  bool renderOn = m_renderingEnabledToggle && m_renderingEnabledToggle->toggle()
                      ? m_renderingEnabledToggle->toggle()->isChecked()
                      : true;
  if (m_renderSubTogglesContainer) {
    m_renderSubTogglesContainer->setVisible(renderOn);
  }
  if (m_renderDetailsContainer) {
    m_renderDetailsContainer->setVisible(renderOn);
  }
  if (m_masterContentContainer) {
    m_masterContentContainer->setVisible(renderOn);
  }

  // Height filter → height range
  bool hfOn = m_heightFilterToggle && m_heightFilterToggle->toggle()
                  ? m_heightFilterToggle->toggle()->isChecked()
                  : false;
  if (m_heightSettingsContainer) {
    m_heightSettingsContainer->setVisible(hfOn);
  }

  // Show Distance → font size + label offset
  bool distOn = m_showDistanceToggle && m_showDistanceToggle->toggle()
                    ? m_showDistanceToggle->toggle()->isChecked()
                    : false;
  if (m_distanceSettingsContainer) {
    m_distanceSettingsContainer->setVisible(distOn);
  }

  // Exclusion Zones → zone toggles + fade edge
  bool exOn = m_exclusionEnabledToggle && m_exclusionEnabledToggle->toggle()
                  ? m_exclusionEnabledToggle->toggle()->isChecked()
                  : false;
  if (m_exclusionDetailsContainer) {
    m_exclusionDetailsContainer->setVisible(exOn);
  }
}

