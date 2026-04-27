/**
 * @file RadialTabWidget.cpp
 * @brief Radial menu tab for ProfileEditor
 *
 * DO NOT ADD:
 * - Inline styles (use UIHelpers role-based styling)
 * - Rendering logic (belongs in child process)
 */

#include "RadialTabWidget.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/DataService.h"
#include "core/ProfileManager.h" // For AccountProfile
#include "core/RadialSettings.h"
#include "core/RadialSettingsManager.h"
#include "ui/ToggleSwitch.h"
#include "ui/UIHelpers.h"

RadialTabWidget::RadialTabWidget(AccountProfile &profile,
                                 DataService *dataService, QWidget *parent)
    : QWidget(parent), m_profile(profile), m_dataService(dataService),
      m_radialSettings(dataService ? dataService->radialSettings2() : nullptr) {
  setupUI();
}

// ============================================================================
// Setup UI
// ============================================================================

void RadialTabWidget::setupUI() {
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);

  // Page header
  outerLayout->addWidget(
      UIHelpers::createPageHeader(this, "Radial Menu", "target"));

  // Scroll area (content is extensive)
  auto *scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  UIHelpers::applyRole(scrollArea, "scrollArea");

  auto *scrollContent = new QWidget();
  auto *contentLayout = new QVBoxLayout(scrollContent);
  contentLayout->setContentsMargins(16, 12, 16, 16);
  contentLayout->setSpacing(16);

  // Build sections
  setupGeneralSection(contentLayout);
  setupMountSection(contentLayout);
  setupNoveltySection(contentLayout);
  setupMarkerSection(contentLayout);
  setupDisplaySection(contentLayout);
  setupInteractionSection(contentLayout);

  contentLayout->addStretch();

  scrollArea->setWidget(scrollContent);
  outerLayout->addWidget(scrollArea);
}

// ============================================================================
// General Section
// ============================================================================

void RadialTabWidget::setupGeneralSection(QVBoxLayout *contentLayout) {
  auto *group = new QGroupBox("General");
  UIHelpers::applyRole(group, "groupBox");
  auto *layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  // Master enable toggle
  m_masterToggle = new LabeledToggle("Enable Radial Menus", group);
  connect(m_masterToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_masterToggle);

  // Icon style
  auto *styleRow = new QHBoxLayout();
  styleRow->setSpacing(8);
  auto *styleLabel = new QLabel("Icon Style:", group);
  UIHelpers::applyRole(styleLabel, "label");
  styleRow->addWidget(styleLabel);

  m_iconStyleCombo = new QComboBox(group);
  m_iconStyleCombo->addItem("AIO SVG (Gold)", "svg");
  m_iconStyleCombo->addItem("Classic PNG (GW2Radial)", "png_mit");
  UIHelpers::applyRole(m_iconStyleCombo, "comboBox");
  connect(m_iconStyleCombo, &QComboBox::currentIndexChanged, this,
          &RadialTabWidget::modified);
  styleRow->addWidget(m_iconStyleCombo);
  styleRow->addStretch();
  layout->addLayout(styleRow);

  contentLayout->addWidget(group);
}

// ============================================================================
// Wheel element table helper
// ============================================================================

QTableWidget *RadialTabWidget::createElementTable(
    const QMap<QString, QString> &labels) {
  auto *table = new QTableWidget(labels.size(), 2);
  table->setHorizontalHeaderLabels({"Element", "Enabled"});
  table->horizontalHeader()->setStretchLastSection(false);
  table->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  table->setColumnWidth(1, 70);
  table->verticalHeader()->setVisible(false);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  UIHelpers::applyRole(table, "table");

  // Fixed row height for compact layout
  table->verticalHeader()->setDefaultSectionSize(32);
  int maxRows = qMin(static_cast<int>(labels.size()), 10);
  // Header height + rows + small padding
  table->setFixedHeight(table->horizontalHeader()->height() +
                        maxRows * 32 + 4);

  int row = 0;
  for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
    // Name column
    auto *nameItem = new QTableWidgetItem(it.value());
    nameItem->setData(Qt::UserRole, it.key()); // Store key for save
    table->setItem(row, 0, nameItem);

    // Enabled column — ToggleSwitch for clear visual feedback
    auto *toggle = new ToggleSwitch();
    toggle->setChecked(true);
    toggle->setFixedSize(40, 20);
    connect(toggle, &ToggleSwitch::toggled, this,
            &RadialTabWidget::modified);

    // Center the toggle in the cell
    auto *container = new QWidget();
    auto *hbox = new QHBoxLayout(container);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setAlignment(Qt::AlignCenter);
    hbox->addWidget(toggle);
    table->setCellWidget(row, 1, container);

    ++row;
  }

  return table;
}

// ============================================================================
// Mount Section
// ============================================================================

void RadialTabWidget::setupMountSection(QVBoxLayout *contentLayout) {
  auto *group = new QGroupBox("Mount Wheel");
  UIHelpers::applyRole(group, "groupBox");
  auto *layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  m_mountWheelToggle = new LabeledToggle("Enable Mount Wheel", group);
  connect(m_mountWheelToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_mountWheelToggle);

  // Mount element table
  QMap<QString, QString> mountLabels;
  mountLabels["raptor"] = "Raptor";
  mountLabels["springer"] = "Springer";
  mountLabels["skimmer"] = "Skimmer";
  mountLabels["jackal"] = "Jackal";
  mountLabels["griffon"] = "Griffon";
  mountLabels["beetle"] = "Roller Beetle";
  mountLabels["warclaw"] = "Warclaw";
  mountLabels["skyscale"] = "Skyscale";
  mountLabels["turtle"] = "Siege Turtle";
  mountLabels["skiff"] = "Skiff";

  m_mountTable = createElementTable(mountLabels);
  layout->addWidget(m_mountTable);

  contentLayout->addWidget(group);
}

// ============================================================================
// Novelty Section
// ============================================================================

void RadialTabWidget::setupNoveltySection(QVBoxLayout *contentLayout) {
  auto *group = new QGroupBox("Novelty Wheel");
  UIHelpers::applyRole(group, "groupBox");
  auto *layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  m_noveltyWheelToggle = new LabeledToggle("Enable Novelty Wheel", group);
  connect(m_noveltyWheelToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_noveltyWheelToggle);

  QMap<QString, QString> noveltyLabels;
  noveltyLabels["chair"] = "Chair";
  noveltyLabels["instrument"] = "Instrument";
  noveltyLabels["heldItem"] = "Held Item";
  noveltyLabels["travelToy"] = "Travel Toy";
  noveltyLabels["tonic"] = "Tonic";
  noveltyLabels["jadeWaypoint"] = "Jade Bot Waypoint";
  noveltyLabels["fishing"] = "Fishing";
  noveltyLabels["scanForRift"] = "Scan for Rift";
  noveltyLabels["summonDoorway"] = "Summon Doorway";

  m_noveltyTable = createElementTable(noveltyLabels);
  layout->addWidget(m_noveltyTable);

  contentLayout->addWidget(group);
}

// ============================================================================
// Marker Section
// ============================================================================

void RadialTabWidget::setupMarkerSection(QVBoxLayout *contentLayout) {
  auto *group = new QGroupBox("Marker Wheel");
  UIHelpers::applyRole(group, "groupBox");
  auto *layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  m_markerWheelToggle = new LabeledToggle("Enable Marker Wheel", group);
  connect(m_markerWheelToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_markerWheelToggle);

  QMap<QString, QString> markerLabels;
  markerLabels["arrow"] = "Arrow";
  markerLabels["circle"] = "Circle";
  markerLabels["heart"] = "Heart";
  markerLabels["square"] = "Square";
  markerLabels["star"] = "Star";
  markerLabels["spiral"] = "Spiral";
  markerLabels["triangle"] = "Triangle";
  markerLabels["x"] = "X";
  markerLabels["clear"] = "Clear All";

  m_markerTable = createElementTable(markerLabels);
  layout->addWidget(m_markerTable);

  contentLayout->addWidget(group);
}

// ============================================================================
// Display Section
// ============================================================================

void RadialTabWidget::setupDisplaySection(QVBoxLayout *contentLayout) {
  auto *group = new QGroupBox("Display");
  UIHelpers::applyRole(group, "groupBox");
  auto *layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  // Scale slider
  auto *scaleRow = new QHBoxLayout();
  auto *scaleTitleLabel = new QLabel("Wheel Scale:", group);
  UIHelpers::applyRole(scaleTitleLabel, "label");
  scaleRow->addWidget(scaleTitleLabel);

  m_scaleSlider = new QSlider(Qt::Horizontal, group);
  m_scaleSlider->setRange(50, 200); // 0.5x – 2.0x
  m_scaleSlider->setValue(100);     // 1.0x default
  m_scaleSlider->setTickInterval(25);
  scaleRow->addWidget(m_scaleSlider);

  m_scaleLabel = new QLabel("1.0x", group);
  UIHelpers::applyRole(m_scaleLabel, "label");
  m_scaleLabel->setFixedWidth(40);
  scaleRow->addWidget(m_scaleLabel);
  layout->addLayout(scaleRow);

  connect(m_scaleSlider, &QSlider::valueChanged, this,
          [this](int value) {
            m_scaleLabel->setText(
                QString::number(value / 100.0, 'f', 1) + "x");
            emit modified();
          });

  // Opacity slider
  auto *opacityRow = new QHBoxLayout();
  auto *opacityTitleLabel = new QLabel("Opacity:", group);
  UIHelpers::applyRole(opacityTitleLabel, "label");
  opacityRow->addWidget(opacityTitleLabel);

  m_opacitySlider = new QSlider(Qt::Horizontal, group);
  m_opacitySlider->setRange(10, 100); // 10% – 100%
  m_opacitySlider->setValue(100);
  opacityRow->addWidget(m_opacitySlider);

  m_opacityLabel = new QLabel("100%", group);
  UIHelpers::applyRole(m_opacityLabel, "label");
  m_opacityLabel->setFixedWidth(40);
  opacityRow->addWidget(m_opacityLabel);
  layout->addLayout(opacityRow);

  connect(m_opacitySlider, &QSlider::valueChanged, this,
          [this](int value) {
            m_opacityLabel->setText(QString::number(value) + "%");
            emit modified();
          });

  // Animation time
  auto *animRow = new QHBoxLayout();
  auto *animLabel = new QLabel("Animation Time (ms):", group);
  UIHelpers::applyRole(animLabel, "label");
  animRow->addWidget(animLabel);

  m_animTimeSpin = new QSpinBox(group);
  m_animTimeSpin->setRange(0, 1000);
  m_animTimeSpin->setValue(150);
  m_animTimeSpin->setSuffix(" ms");
  UIHelpers::applyRole(m_animTimeSpin, "spinBox");
  connect(m_animTimeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &RadialTabWidget::modified);
  animRow->addWidget(m_animTimeSpin);
  animRow->addStretch();
  layout->addLayout(animRow);

  contentLayout->addWidget(group);
}

// ============================================================================
// Interaction Section
// ============================================================================

void RadialTabWidget::setupInteractionSection(QVBoxLayout *contentLayout) {
  auto *group = new QGroupBox("Interaction");
  UIHelpers::applyRole(group, "groupBox");
  auto *layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  // Center behavior
  auto *centerRow = new QHBoxLayout();
  auto *centerLabel = new QLabel("Center Behavior:", group);
  UIHelpers::applyRole(centerLabel, "label");
  centerRow->addWidget(centerLabel);

  m_centerBehaviorCombo = new QComboBox(group);
  m_centerBehaviorCombo->addItem("Nothing", 0);
  m_centerBehaviorCombo->addItem("Previous Selection", 1);
  m_centerBehaviorCombo->addItem("Favorite", 2);
  m_centerBehaviorCombo->addItem("Pass to Game", 3);
  UIHelpers::applyRole(m_centerBehaviorCombo, "comboBox");
  connect(m_centerBehaviorCombo, &QComboBox::currentIndexChanged, this,
          &RadialTabWidget::modified);
  centerRow->addWidget(m_centerBehaviorCombo);
  centerRow->addStretch();
  layout->addLayout(centerRow);

  // Toggles
  m_noHoldToggle = new LabeledToggle("No-Hold Mode (click to select)", group);
  connect(m_noHoldToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_noHoldToggle);

  m_clickSelectToggle =
      new LabeledToggle("Click to Select (click on element)", group);
  connect(m_clickSelectToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_clickSelectToggle);

  m_resetCursorToggle =
      new LabeledToggle("Reset Cursor After Selection", group);
  connect(m_resetCursorToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_resetCursorToggle);

  m_lockCameraToggle =
      new LabeledToggle("Lock Camera When Wheel Open", group);
  connect(m_lockCameraToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_lockCameraToggle);

  // Queuing sub-section
  auto *queueSeparator = new QLabel("Queuing", group);
  UIHelpers::applyRole(queueSeparator, "sectionLabel");
  layout->addWidget(queueSeparator);

  m_queuingToggle =
      new LabeledToggle("Enable Input Queuing", group);
  connect(m_queuingToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_queuingToggle);

  auto *queueTimeoutRow = new QHBoxLayout();
  auto *queueTimeoutLabel = new QLabel("Queue Timeout:", group);
  UIHelpers::applyRole(queueTimeoutLabel, "label");
  queueTimeoutRow->addWidget(queueTimeoutLabel);

  m_queueTimeoutSpin = new QSpinBox(group);
  m_queueTimeoutSpin->setRange(1000, 30000);
  m_queueTimeoutSpin->setValue(5000);
  m_queueTimeoutSpin->setSuffix(" ms");
  m_queueTimeoutSpin->setSingleStep(500);
  UIHelpers::applyRole(m_queueTimeoutSpin, "spinBox");
  connect(m_queueTimeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &RadialTabWidget::modified);
  queueTimeoutRow->addWidget(m_queueTimeoutSpin);
  queueTimeoutRow->addStretch();
  layout->addLayout(queueTimeoutRow);

  auto *queueDelayRow = new QHBoxLayout();
  auto *queueDelayLabel = new QLabel("Conditional Delay:", group);
  UIHelpers::applyRole(queueDelayLabel, "label");
  queueDelayRow->addWidget(queueDelayLabel);

  m_queueDelaySpin = new QSpinBox(group);
  m_queueDelaySpin->setRange(0, 5000);
  m_queueDelaySpin->setValue(500);
  m_queueDelaySpin->setSuffix(" ms");
  m_queueDelaySpin->setSingleStep(100);
  UIHelpers::applyRole(m_queueDelaySpin, "spinBox");
  connect(m_queueDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &RadialTabWidget::modified);
  queueDelayRow->addWidget(m_queueDelaySpin);
  queueDelayRow->addStretch();
  layout->addLayout(queueDelayRow);

  contentLayout->addWidget(group);
}

// ============================================================================
// Load / Save
// ============================================================================

static void loadElementTable(QTableWidget *table,
                             const QMap<QString, RadialElementConfig> &configs) {
  for (int row = 0; row < table->rowCount(); ++row) {
    auto *nameItem = table->item(row, 0);
    if (!nameItem)
      continue;
    QString key = nameItem->data(Qt::UserRole).toString();
    if (configs.contains(key)) {
      auto *container = table->cellWidget(row, 1);
      if (container) {
        auto *toggle = container->findChild<ToggleSwitch *>();
        if (toggle)
          toggle->setChecked(configs[key].enabled);
      }
    }
  }
}

static QMap<QString, RadialElementConfig> saveElementTable(
    QTableWidget *table,
    const QMap<QString, RadialElementConfig> &existing) {
  QMap<QString, RadialElementConfig> result = existing;
  for (int row = 0; row < table->rowCount(); ++row) {
    auto *nameItem = table->item(row, 0);
    if (!nameItem)
      continue;
    QString key = nameItem->data(Qt::UserRole).toString();
    if (result.contains(key)) {
      auto *container = table->cellWidget(row, 1);
      if (container) {
        auto *toggle = container->findChild<ToggleSwitch *>();
        result[key].enabled = toggle ? toggle->isChecked() : true;
      }
      result[key].sortOrder = row;
    }
  }
  return result;
}

void RadialTabWidget::load() {
  if (!m_radialSettings) {
    return;
  }

  m_radialSettings->loadForProfile(m_profile.id);
  RadialSettings s = m_radialSettings->settings();

  // Block signals during load to prevent false "modified" emissions
  const bool blocked = blockSignals(true);

  // General
  m_masterToggle->setChecked(s.radialEnabled);
  int styleIndex = m_iconStyleCombo->findData(s.iconStyle);
  if (styleIndex >= 0)
    m_iconStyleCombo->setCurrentIndex(styleIndex);

  // Wheels
  m_mountWheelToggle->setChecked(s.mountWheelEnabled);
  loadElementTable(m_mountTable, s.mounts);

  m_noveltyWheelToggle->setChecked(s.noveltyWheelEnabled);
  loadElementTable(m_noveltyTable, s.novelties);

  m_markerWheelToggle->setChecked(s.markerWheelEnabled);
  loadElementTable(m_markerTable, s.markers);

  // Display
  m_scaleSlider->setValue(static_cast<int>(s.wheelScale * 100.0f));
  m_opacitySlider->setValue(static_cast<int>(s.opacity * 100.0f));
  m_animTimeSpin->setValue(s.animationTimeMs);

  // Interaction
  int centerIdx = m_centerBehaviorCombo->findData(
      static_cast<int>(s.centerBehavior));
  if (centerIdx >= 0)
    m_centerBehaviorCombo->setCurrentIndex(centerIdx);

  m_noHoldToggle->setChecked(s.noHoldMode);
  m_clickSelectToggle->setChecked(s.clickSelectMode);
  m_resetCursorToggle->setChecked(s.resetCursorAfterKeybind);
  m_lockCameraToggle->setChecked(s.lockCameraWhenOverlayed);

  // Queuing
  m_queuingToggle->setChecked(s.enableQueuing);
  m_queueTimeoutSpin->setValue(s.maxQueueWaitMs);
  m_queueDelaySpin->setValue(s.conditionalDelayMs);

  blockSignals(blocked);
}

void RadialTabWidget::save() {
  if (!m_radialSettings) {
    return;
  }

  RadialSettings s = m_radialSettings->settings();

  // General
  s.radialEnabled = m_masterToggle->isChecked();
  s.iconStyle = m_iconStyleCombo->currentData().toString();

  // Wheels
  s.mountWheelEnabled = m_mountWheelToggle->isChecked();
  s.mounts = saveElementTable(m_mountTable, s.mounts);

  s.noveltyWheelEnabled = m_noveltyWheelToggle->isChecked();
  s.novelties = saveElementTable(m_noveltyTable, s.novelties);

  s.markerWheelEnabled = m_markerWheelToggle->isChecked();
  s.markers = saveElementTable(m_markerTable, s.markers);

  // Display
  s.wheelScale = m_scaleSlider->value() / 100.0f;
  s.opacity = m_opacitySlider->value() / 100.0f;
  s.animationTimeMs = m_animTimeSpin->value();

  // Interaction
  s.centerBehavior = static_cast<RadialCenterBehavior>(
      m_centerBehaviorCombo->currentData().toInt());
  s.noHoldMode = m_noHoldToggle->isChecked();
  s.clickSelectMode = m_clickSelectToggle->isChecked();
  s.resetCursorAfterKeybind = m_resetCursorToggle->isChecked();
  s.lockCameraWhenOverlayed = m_lockCameraToggle->isChecked();

  // Queuing
  s.enableQueuing = m_queuingToggle->isChecked();
  s.maxQueueWaitMs = m_queueTimeoutSpin->value();
  s.conditionalDelayMs = m_queueDelaySpin->value();

  m_radialSettings->setSettings(s);
  if (!m_radialSettings->saveForProfile(m_profile.id)) {
    qWarning() << "RadialTabWidget: Failed to save radial settings for"
               << m_profile.id;
  }
}
