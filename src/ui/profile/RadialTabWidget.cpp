/**
 * @file RadialTabWidget.cpp
 * @brief Radial menu tab for ProfileEditor
 *
 * DO NOT ADD:
 * - Inline styles (use UIHelpers role-based styling)
 * - Rendering logic (belongs in child process)
 */

#include "RadialTabWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/DataService.h"
#include "core/ProfileManager.h" // For AccountProfile
#include "core/Gw2KeybindParser.h"
#include "core/RadialSettings.h"
#include "core/RadialSettingsManager.h"
#include "ui/ToggleSwitch.h"
#include "ui/UIHelpers.h"

RadialTabWidget::RadialTabWidget(AccountProfile &profile,
                                 DataService *dataService, QWidget *parent)
    : QWidget(parent), m_profile(profile), m_dataService(dataService),
      m_radialSettings(dataService ? dataService->radialSettings2() : nullptr) {
  setupUI();

  // Reload when settings change externally (e.g., overlay changed them in-game).
  // Guard: skip reload when we are the source (save() sets m_saving = true).
  if (m_radialSettings) {
    connect(m_radialSettings, &RadialSettingsManager::settingsChanged,
            this, [this]() {
      if (!m_saving) {
        load();
      }
    });
  }
}

// ============================================================================
// VK ↔ QKeySequence Conversion Helpers
// ============================================================================

// clang-format off
// Windows VK code → Qt::Key mapping for keys that don't map directly.
// A-Z (0x41-0x5A) and 0-9 (0x30-0x39) map 1:1 and are handled as fallthrough.
static const QMap<int, int> &vkToQtKeyMap() {
  static const QMap<int, int> map = {
      // Numpad 0-9: VK 0x60-0x69 → Qt has no dedicated numpad keys,
      // map to digit keys (Qt::Key_0 - Qt::Key_9) with KeypadModifier added
      {0x60, Qt::Key_0}, {0x61, Qt::Key_1}, {0x62, Qt::Key_2},
      {0x63, Qt::Key_3}, {0x64, Qt::Key_4}, {0x65, Qt::Key_5},
      {0x66, Qt::Key_6}, {0x67, Qt::Key_7}, {0x68, Qt::Key_8},
      {0x69, Qt::Key_9},
      // F1-F12: VK 0x70-0x7B → Qt::Key_F1 = 0x01000030
      {0x70, Qt::Key_F1},  {0x71, Qt::Key_F2},  {0x72, Qt::Key_F3},
      {0x73, Qt::Key_F4},  {0x74, Qt::Key_F5},  {0x75, Qt::Key_F6},
      {0x76, Qt::Key_F7},  {0x77, Qt::Key_F8},  {0x78, Qt::Key_F9},
      {0x79, Qt::Key_F10}, {0x7A, Qt::Key_F11}, {0x7B, Qt::Key_F12},
      // Navigation
      {0x21, Qt::Key_PageUp},   {0x22, Qt::Key_PageDown},
      {0x23, Qt::Key_End},      {0x24, Qt::Key_Home},
      {0x25, Qt::Key_Left},     {0x26, Qt::Key_Up},
      {0x27, Qt::Key_Right},    {0x28, Qt::Key_Down},
      {0x2D, Qt::Key_Insert},   {0x2E, Qt::Key_Delete},
      // Special
      {0x08, Qt::Key_Backspace}, {0x09, Qt::Key_Tab},
      {0x0D, Qt::Key_Return},   {0x1B, Qt::Key_Escape},
      {0x20, Qt::Key_Space},    {0x14, Qt::Key_CapsLock},
      {0x90, Qt::Key_NumLock},  {0x91, Qt::Key_ScrollLock},
      {0x13, Qt::Key_Pause},    {0x2C, Qt::Key_Print},
      // Numpad operators
      {0x6A, Qt::Key_Asterisk}, {0x6B, Qt::Key_Plus},
      {0x6D, Qt::Key_Minus},    {0x6E, Qt::Key_Period},
      {0x6F, Qt::Key_Slash},
      // OEM keys (US layout common)
      {0xBA, Qt::Key_Semicolon}, {0xBB, Qt::Key_Equal},
      {0xBC, Qt::Key_Comma},    {0xBD, Qt::Key_Minus},
      {0xBE, Qt::Key_Period},   {0xBF, Qt::Key_Slash},
      {0xC0, Qt::Key_QuoteLeft}, {0xDB, Qt::Key_BracketLeft},
      {0xDC, Qt::Key_Backslash}, {0xDD, Qt::Key_BracketRight},
      {0xDE, Qt::Key_Apostrophe},
  };
  return map;
}

// Reverse map: Qt::Key → VK code
static const QMap<int, int> &qtKeyToVkMap() {
  static QMap<int, int> map;
  if (map.isEmpty()) {
    const auto &fwd = vkToQtKeyMap();
    for (auto it = fwd.begin(); it != fwd.end(); ++it) {
      // For numpad digits, don't overwrite the digit→VK mapping
      // (handled separately in keySequenceToVk via KeypadModifier)
      if (it.key() >= 0x60 && it.key() <= 0x69)
        continue;
      map[it.value()] = it.key();
    }
  }
  return map;
}
// clang-format on

// Convert VK code + GW2 modifier flags to a QKeySequence for display.
// modifiers uses GW2 bitmask: 1=Shift, 2=Ctrl, 4=Alt
static QKeySequence vkToKeySequence(int vk, int modifiers) {
  if (vk == 0) {
    return QKeySequence();
  }

  Qt::KeyboardModifiers qtMods;
  if (modifiers & 1)
    qtMods |= Qt::ShiftModifier;
  if (modifiers & 2)
    qtMods |= Qt::ControlModifier;
  if (modifiers & 4)
    qtMods |= Qt::AltModifier;

  int qtKey;
  bool isNumpad = (vk >= 0x60 && vk <= 0x69);
  if (vkToQtKeyMap().contains(vk)) {
    qtKey = vkToQtKeyMap().value(vk);
    if (isNumpad) {
      qtMods |= Qt::KeypadModifier;
    }
  } else {
    // Direct map for A-Z, 0-9
    qtKey = vk;
  }

  return QKeySequence(QKeyCombination(qtMods, static_cast<Qt::Key>(qtKey)));
}

// Convert a QKeySequence back to VK code + GW2 modifier bitmask.
// Output: outModifiers uses GW2 bitmask: 1=Alt, 2=Ctrl, 4=Shift
static void keySequenceToVk(const QKeySequence &seq, int &outVk,
                            int &outModifiers) {
  if (seq.isEmpty()) {
    outVk = 0;
    outModifiers = 0;
    return;
  }
  QKeyCombination combo = seq[0];
  int qtKey = static_cast<int>(combo.key());
  Qt::KeyboardModifiers qtMods = combo.keyboardModifiers();

  // Check if this is a numpad digit (KeypadModifier + Key_0..Key_9)
  if ((qtMods & Qt::KeypadModifier) && qtKey >= Qt::Key_0 &&
      qtKey <= Qt::Key_9) {
    outVk = 0x60 + (qtKey - Qt::Key_0); // VK_NUMPAD0..9
  } else if (qtKeyToVkMap().contains(qtKey)) {
    outVk = qtKeyToVkMap().value(qtKey);
  } else {
    // Direct for A-Z, 0-9
    outVk = qtKey;
  }

  // Convert Qt modifier flags → GW2 bitmask (1=Shift, 2=Ctrl, 4=Alt)
  outModifiers = 0;
  if (qtMods & Qt::ShiftModifier)
    outModifiers |= 1;
  if (qtMods & Qt::ControlModifier)
    outModifiers |= 2;
  if (qtMods & Qt::AltModifier)
    outModifiers |= 4;
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

  // Detect GW2 Keybinds button row
  auto *detectRow = new QHBoxLayout();
  detectRow->setSpacing(8);
  auto *detectBtn = new QPushButton("Detect GW2 Keybinds", group);
  UIHelpers::applyRole(detectBtn, "primaryButton");
  connect(detectBtn, &QPushButton::clicked, this,
          &RadialTabWidget::detectGw2Keybinds);
  detectRow->addWidget(detectBtn);

  auto *browseBtn = new QPushButton("Browse...", group);
  UIHelpers::applyRole(browseBtn, "neutralButton");
  connect(browseBtn, &QPushButton::clicked, this,
          &RadialTabWidget::browseGw2Keybinds);
  detectRow->addWidget(browseBtn);

  detectRow->addStretch();
  layout->addLayout(detectRow);

  // Status label for detect/browse results
  m_detectStatusLabel = new QLabel("", group);
  UIHelpers::applyRole(m_detectStatusLabel, "hintLabel");
  m_detectStatusLabel->setWordWrap(true);
  layout->addWidget(m_detectStatusLabel);

  contentLayout->addWidget(group);
}

// ============================================================================
// Wheel element table helper
// ============================================================================

QTableWidget *RadialTabWidget::createElementTable(
    const QMap<QString, QString> &labels) {
  auto *table = new QTableWidget(labels.size(), 3);
  table->setHorizontalHeaderLabels({"Element", "GW2 Keybind", "Enabled"});
  table->horizontalHeader()->setStretchLastSection(false);
  table->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  table->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Fixed);
  table->setColumnWidth(1, 140);
  table->setColumnWidth(2, 80);
  table->verticalHeader()->setVisible(false);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  UIHelpers::applyRole(table, "table");

  // Fixed row height for compact layout
  table->verticalHeader()->setDefaultSectionSize(36);
  int maxRows = qMin(static_cast<int>(labels.size()), 10);
  // Header height + rows + small padding
  table->setFixedHeight(table->horizontalHeader()->height() +
                        maxRows * 36 + 4);

  int row = 0;
  for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
    // Column 0: Name
    auto *nameItem = new QTableWidgetItem(it.value());
    nameItem->setData(Qt::UserRole, it.key()); // Store key for save
    table->setItem(row, 0, nameItem);

    // Column 1: GW2 Keybind — QKeySequenceEdit captures one key
    auto *keybindEdit = new QKeySequenceEdit();
    keybindEdit->setMaximumSequenceLength(1);
    UIHelpers::applyRole(keybindEdit, "input");
    connect(keybindEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &RadialTabWidget::modified);
    table->setCellWidget(row, 1, keybindEdit);

    // Column 2: Enabled toggle
    auto *toggle = new ToggleSwitch();
    toggle->setChecked(true);
    connect(toggle, &ToggleSwitch::toggled, this,
            &RadialTabWidget::modified);

    auto *container = new QWidget();
    auto *hbox = new QHBoxLayout(container);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setAlignment(Qt::AlignCenter);
    hbox->addWidget(toggle);
    table->setCellWidget(row, 2, container);

    ++row;
  }

  return table;
}

QTableWidget *RadialTabWidget::createOrderedElementTable(
    const QList<QPair<QString, QString>> &entries) {
  auto *table = new QTableWidget(entries.size(), 3);
  table->setHorizontalHeaderLabels({"Element", "GW2 Keybind", "Enabled"});
  table->horizontalHeader()->setStretchLastSection(false);
  table->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  table->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Fixed);
  table->setColumnWidth(1, 140);
  table->setColumnWidth(2, 80);
  table->verticalHeader()->setVisible(false);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  UIHelpers::applyRole(table, "table");

  // Fixed row height for compact layout
  table->verticalHeader()->setDefaultSectionSize(36);
  int maxRows = qMin(static_cast<int>(entries.size()), 12);
  table->setFixedHeight(table->horizontalHeader()->height() +
                        maxRows * 36 + 4);

  int row = 0;
  for (const auto &entry : entries) {
    const QString &key = entry.first;
    const QString &label = entry.second;

    // Column 0: Name
    auto *nameItem = new QTableWidgetItem(label);
    nameItem->setData(Qt::UserRole, key);
    table->setItem(row, 0, nameItem);

    // Column 1: GW2 Keybind
    auto *keybindEdit = new QKeySequenceEdit();
    keybindEdit->setMaximumSequenceLength(1);
    UIHelpers::applyRole(keybindEdit, "input");
    connect(keybindEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &RadialTabWidget::modified);
    table->setCellWidget(row, 1, keybindEdit);

    // Column 2: Enabled toggle (skip for _dismount — always enabled)
    if (key.startsWith(QLatin1Char('_'))) {
      // No toggle for special rows like _dismount
      auto *placeholder = new QWidget();
      table->setCellWidget(row, 2, placeholder);
    } else {
      auto *toggle = new ToggleSwitch();
      toggle->setChecked(true);
      connect(toggle, &ToggleSwitch::toggled, this,
              &RadialTabWidget::modified);

      auto *container = new QWidget();
      auto *hbox = new QHBoxLayout(container);
      hbox->setContentsMargins(0, 0, 0, 0);
      hbox->setAlignment(Qt::AlignCenter);
      hbox->addWidget(toggle);
      table->setCellWidget(row, 2, container);
    }

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

  // Hotkey
  auto *mountHotkeyRow = new QHBoxLayout();
  mountHotkeyRow->setSpacing(8);
  auto *mountHotkeyLabel = new QLabel("Hotkey:", group);
  UIHelpers::applyRole(mountHotkeyLabel, "label");
  mountHotkeyRow->addWidget(mountHotkeyLabel);
  m_mountHotkeyEdit = new QKeySequenceEdit(group);
  m_mountHotkeyEdit->setMaximumSequenceLength(1);
  UIHelpers::applyRole(m_mountHotkeyEdit, "input");
  connect(m_mountHotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this,
          &RadialTabWidget::modified);
  mountHotkeyRow->addWidget(m_mountHotkeyEdit);
  mountHotkeyRow->addStretch();
  layout->addLayout(mountHotkeyRow);

  // Hint: GW2 keybinds must match in-game settings
  auto *keybindHint = new QLabel(
      "Set each GW2 Keybind to match your in-game mount keybinds "
      "(F11 \u2192 Control Options \u2192 Mounts).",
      group);
  UIHelpers::applyRole(keybindHint, "hintLabel");
  keybindHint->setWordWrap(true);
  layout->addWidget(keybindHint);

  // Mount element table — ordered list (not QMap which sorts alphabetically)
  // _dismount is the generic Mount/Dismount keybind (no toggle needed)
  QList<QPair<QString, QString>> mountEntries;
  mountEntries << qMakePair(QStringLiteral("_dismount"), QStringLiteral("Mount / Dismount"));
  mountEntries << qMakePair(QStringLiteral("raptor"), QStringLiteral("Raptor"));
  mountEntries << qMakePair(QStringLiteral("springer"), QStringLiteral("Springer"));
  mountEntries << qMakePair(QStringLiteral("skimmer"), QStringLiteral("Skimmer"));
  mountEntries << qMakePair(QStringLiteral("jackal"), QStringLiteral("Jackal"));
  mountEntries << qMakePair(QStringLiteral("griffon"), QStringLiteral("Griffon"));
  mountEntries << qMakePair(QStringLiteral("beetle"), QStringLiteral("Roller Beetle"));
  mountEntries << qMakePair(QStringLiteral("warclaw"), QStringLiteral("Warclaw"));
  mountEntries << qMakePair(QStringLiteral("skyscale"), QStringLiteral("Skyscale"));
  mountEntries << qMakePair(QStringLiteral("turtle"), QStringLiteral("Siege Turtle"));
  mountEntries << qMakePair(QStringLiteral("skiff"), QStringLiteral("Skiff"));

  m_mountTable = createOrderedElementTable(mountEntries);
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

  // Hotkey
  auto *noveltyHotkeyRow = new QHBoxLayout();
  noveltyHotkeyRow->setSpacing(8);
  auto *noveltyHotkeyLabel = new QLabel("Hotkey:", group);
  UIHelpers::applyRole(noveltyHotkeyLabel, "label");
  noveltyHotkeyRow->addWidget(noveltyHotkeyLabel);
  m_noveltyHotkeyEdit = new QKeySequenceEdit(group);
  m_noveltyHotkeyEdit->setMaximumSequenceLength(1);
  UIHelpers::applyRole(m_noveltyHotkeyEdit, "input");
  connect(m_noveltyHotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this,
          &RadialTabWidget::modified);
  noveltyHotkeyRow->addWidget(m_noveltyHotkeyEdit);
  noveltyHotkeyRow->addStretch();
  layout->addLayout(noveltyHotkeyRow);

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

  // Hotkey
  auto *markerHotkeyRow = new QHBoxLayout();
  markerHotkeyRow->setSpacing(8);
  auto *markerHotkeyLabel = new QLabel("Hotkey:", group);
  UIHelpers::applyRole(markerHotkeyLabel, "label");
  markerHotkeyRow->addWidget(markerHotkeyLabel);
  m_markerHotkeyEdit = new QKeySequenceEdit(group);
  m_markerHotkeyEdit->setMaximumSequenceLength(1);
  UIHelpers::applyRole(m_markerHotkeyEdit, "input");
  connect(m_markerHotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this,
          &RadialTabWidget::modified);
  markerHotkeyRow->addWidget(m_markerHotkeyEdit);
  markerHotkeyRow->addStretch();
  layout->addLayout(markerHotkeyRow);

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
  m_centerBehaviorCombo->addItem("Mount / Dismount", 4);
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

  m_fastMountSwapToggle =
      new LabeledToggle("Fast Mount Swap (dismount before remount)", group);
  connect(m_fastMountSwapToggle, &LabeledToggle::toggled, this,
          &RadialTabWidget::modified);
  layout->addWidget(m_fastMountSwapToggle);

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

// Load the _dismount row from dedicated dismount keybind fields
static void loadDismountRow(QTableWidget *table, int vk, int modifiers) {
  for (int row = 0; row < table->rowCount(); ++row) {
    auto *nameItem = table->item(row, 0);
    if (!nameItem) continue;
    if (nameItem->data(Qt::UserRole).toString() == QLatin1String("_dismount")) {
      auto *keybindEdit =
          qobject_cast<QKeySequenceEdit *>(table->cellWidget(row, 1));
      if (keybindEdit) {
        keybindEdit->setKeySequence(vkToKeySequence(vk, modifiers));
      }
      break;
    }
  }
}

// Save the _dismount row to dedicated dismount keybind fields
static void saveDismountRow(QTableWidget *table, int &outVk, int &outModifiers) {
  for (int row = 0; row < table->rowCount(); ++row) {
    auto *nameItem = table->item(row, 0);
    if (!nameItem) continue;
    if (nameItem->data(Qt::UserRole).toString() == QLatin1String("_dismount")) {
      auto *keybindEdit =
          qobject_cast<QKeySequenceEdit *>(table->cellWidget(row, 1));
      if (keybindEdit) {
        keySequenceToVk(keybindEdit->keySequence(), outVk, outModifiers);
      }
      break;
    }
  }
}

static void loadElementTable(QTableWidget *table,
                             const QMap<QString, RadialElementConfig> &configs) {
  for (int row = 0; row < table->rowCount(); ++row) {
    auto *nameItem = table->item(row, 0);
    if (!nameItem)
      continue;
    QString key = nameItem->data(Qt::UserRole).toString();
    if (configs.contains(key)) {
      const auto &cfg = configs[key];

      // Column 1: GW2 Keybind
      auto *keybindEdit =
          qobject_cast<QKeySequenceEdit *>(table->cellWidget(row, 1));
      if (keybindEdit) {
        keybindEdit->setKeySequence(
            vkToKeySequence(cfg.scanCode, cfg.modifiers));
      }

      // Column 2: Enabled toggle
      auto *container = table->cellWidget(row, 2);
      if (container) {
        auto *toggle = container->findChild<ToggleSwitch *>();
        if (toggle)
          toggle->setChecked(cfg.enabled);
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
      // Column 1: GW2 Keybind
      auto *keybindEdit =
          qobject_cast<QKeySequenceEdit *>(table->cellWidget(row, 1));
      if (keybindEdit) {
        int vk = 0, mods = 0;
        keySequenceToVk(keybindEdit->keySequence(), vk, mods);
        result[key].scanCode = vk;
        result[key].modifiers = mods;
      }

      // Column 2: Enabled toggle
      auto *container = table->cellWidget(row, 2);
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

  // Show setup guide on first visit (deferred so profile editor is visible)
  QMetaObject::invokeMethod(this, &RadialTabWidget::showFirstTimeSetupGuide,
                            Qt::QueuedConnection);

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
  m_mountHotkeyEdit->setKeySequence(
      vkToKeySequence(s.mountHotkey, s.mountHotkeyModifiers));
  // Load _dismount row from dedicated dismount keybind fields
  loadDismountRow(m_mountTable, s.dismountScanCode, s.dismountModifiers);
  loadElementTable(m_mountTable, s.mounts);

  m_noveltyWheelToggle->setChecked(s.noveltyWheelEnabled);
  m_noveltyHotkeyEdit->setKeySequence(
      vkToKeySequence(s.noveltyHotkey, s.noveltyHotkeyModifiers));
  loadElementTable(m_noveltyTable, s.novelties);

  m_markerWheelToggle->setChecked(s.markerWheelEnabled);
  m_markerHotkeyEdit->setKeySequence(
      vkToKeySequence(s.markerHotkey, s.markerHotkeyModifiers));
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
  m_fastMountSwapToggle->setChecked(s.fastMountSwap);

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
  keySequenceToVk(m_mountHotkeyEdit->keySequence(),
                  s.mountHotkey, s.mountHotkeyModifiers);
  // Save _dismount row to dedicated dismount keybind fields
  saveDismountRow(m_mountTable, s.dismountScanCode, s.dismountModifiers);
  s.mounts = saveElementTable(m_mountTable, s.mounts);

  s.noveltyWheelEnabled = m_noveltyWheelToggle->isChecked();
  keySequenceToVk(m_noveltyHotkeyEdit->keySequence(),
                  s.noveltyHotkey, s.noveltyHotkeyModifiers);
  s.novelties = saveElementTable(m_noveltyTable, s.novelties);

  s.markerWheelEnabled = m_markerWheelToggle->isChecked();
  keySequenceToVk(m_markerHotkeyEdit->keySequence(),
                  s.markerHotkey, s.markerHotkeyModifiers);
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
  s.fastMountSwap = m_fastMountSwapToggle->isChecked();

  // Queuing
  s.enableQueuing = m_queuingToggle->isChecked();
  s.maxQueueWaitMs = m_queueTimeoutSpin->value();
  s.conditionalDelayMs = m_queueDelaySpin->value();

  m_saving = true;
  m_radialSettings->setSettings(s);
  if (!m_radialSettings->saveForProfile(m_profile.id)) {
    qWarning() << "RadialTabWidget: Failed to save radial settings for"
               << m_profile.id;
  }
  m_saving = false;

  // Push updated settings to the running radial child process (if any)
  // so changes take effect immediately without requiring a restart.
  if (m_dataService) {
    m_dataService->pushRadialSettingsToChildren(m_profile.id);
  }
}

// ============================================================================
// Detect GW2 Keybinds
// ============================================================================

// Apply parsed keybinds to a table's QKeySequenceEdit widgets
static int applyKeybindsToTable(
    QTableWidget *table,
    const QMap<QString, Gw2Keybind> &keybinds) {
  int applied = 0;
  for (int row = 0; row < table->rowCount(); ++row) {
    auto *nameItem = table->item(row, 0);
    if (!nameItem)
      continue;
    QString key = nameItem->data(Qt::UserRole).toString();

    if (keybinds.contains(key)) {
      const auto &kb = keybinds[key];
      auto *keybindEdit =
          qobject_cast<QKeySequenceEdit *>(table->cellWidget(row, 1));
      if (keybindEdit) {
        // vkToKeySequence accepts GW2 bitmask (1=Alt, 2=Ctrl, 4=Shift)
        keybindEdit->setKeySequence(
            vkToKeySequence(kb.virtualKey, kb.modifiers));
        ++applied;
      }
    }
  }
  return applied;
}

void RadialTabWidget::applyKeybindsFromFile(const QString &xmlPath) {
  // Parse the XML
  QMap<QString, Gw2Keybind> keybinds =
      Gw2KeybindParser::parseFile(xmlPath);
  if (keybinds.isEmpty()) {
    m_detectStatusLabel->setText(
        "No matching keybinds found in this file.");
    UIHelpers::applyRole(m_detectStatusLabel, "warningLabel");
    return;
  }

  // Extract special _dismount keybind (generic Mount/Dismount, GW2 action 152)
  // This goes into RadialSettings directly, not into the element tables
  bool dismountFound = false;
  if (keybinds.contains(QStringLiteral("_dismount"))) {
    const auto &kb = keybinds[QStringLiteral("_dismount")];
    RadialSettings s = m_radialSettings->settings();
    s.dismountScanCode = kb.virtualKey;
    s.dismountModifiers = kb.modifiers;
    m_radialSettings->setSettings(s);
    keybinds.remove(QStringLiteral("_dismount"));
    dismountFound = true;
    qInfo() << "RadialTabWidget: Imported dismount keybind VK:"
            << kb.virtualKey << "mod:" << kb.modifiers;
  }

  // Apply to all element tables
  int total = 0;
  total += applyKeybindsToTable(m_mountTable, keybinds);
  total += applyKeybindsToTable(m_noveltyTable, keybinds);
  total += applyKeybindsToTable(m_markerTable, keybinds);

  // Show result
  QString fileName = QFileInfo(xmlPath).fileName();
  QString dismountNote = dismountFound
      ? " (+ Mount/Dismount key)"
      : "";
  m_detectStatusLabel->setText(
      QString("Detected %1 keybinds from \"%2\"%3. Click Save to apply.")
          .arg(total)
          .arg(fileName)
          .arg(dismountNote));
  UIHelpers::applyRole(m_detectStatusLabel, "hintLabel");

  // Auto-save so keybinds take effect immediately
  if (total > 0 || dismountFound) {
    save();
    emit modified();
  }
}

void RadialTabWidget::detectGw2Keybinds() {
  // Find the newest XML export
  QString xmlPath = Gw2KeybindParser::findNewestExportFile();
  if (xmlPath.isEmpty()) {
    m_detectStatusLabel->setText(
        "No keybind export found. In GW2, press F11 \u2192 Control Options "
        "\u2192 scroll down \u2192 click Export.");
    UIHelpers::applyRole(m_detectStatusLabel, "warningLabel");
    return;
  }

  applyKeybindsFromFile(xmlPath);
}

void RadialTabWidget::browseGw2Keybinds() {
  // Default to the GW2 InputBinds directory
  QString docsPath =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  QString defaultDir =
      QDir(docsPath).filePath("Guild Wars 2/InputBinds");

  // Fall back to Documents if InputBinds doesn't exist yet
  if (!QDir(defaultDir).exists()) {
    defaultDir = docsPath;
  }

  QString xmlPath = QFileDialog::getOpenFileName(
      this, "Select GW2 Keybind Export", defaultDir,
      "GW2 Keybind Files (*.xml);;All Files (*)");

  if (xmlPath.isEmpty()) {
    return; // User cancelled
  }

  applyKeybindsFromFile(xmlPath);
}

void RadialTabWidget::showFirstTimeSetupGuide() {
  // Check if already dismissed
  QSettings settings;
  if (settings.value("ui/radialSetupGuideDismissed", false).toBool()) {
    return;
  }

  // Build styled popup
  auto *d = UIHelpers::createStyledDialog(this, 480);
  auto *ol = new QVBoxLayout(d);
  ol->setContentsMargins(0, 0, 0, 0);
  auto *bg = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bg);
  ol->addWidget(bg);
  auto *ly = new QVBoxLayout(bg);
  ly->setContentsMargins(20, 20, 20, 20);
  ly->setSpacing(12);

  // Title
  auto *title = new QLabel("Radial Menu Setup");
  UIHelpers::applyGoldTitleRole(title);
  title->setAlignment(Qt::AlignCenter);
  ly->addWidget(title);

  // Instructions
  auto *instructions = new QLabel(
      "To use the Radial Menu, you need to tell AIO which keys "
      "your GW2 mounts are bound to.\n\n"
      "Quick setup:\n"
      "1. In GW2, open Options (F11) \u2192 Control Options \u2192 Mounts\n"
      "2. Make sure each mount has a keybind assigned\n"
      "3. Scroll down and click Export to save your keybinds\n"
      "4. Back in AIO, click Detect GW2 Keybinds to auto-import\n"
      "5. Set a Trigger Hotkey for the Mount Wheel\n"
      "6. Save your profile\n\n"
      "For multiboxing: if each account uses different keybinds, "
      "use Browse to select the correct export file per profile.");
  UIHelpers::applyPopupLabelRole(instructions);
  instructions->setWordWrap(true);
  ly->addWidget(instructions);

  // Don't show again checkbox
  auto *dontShowCheck = new QCheckBox("Don't show this again");
  UIHelpers::applyRole(dontShowCheck, "checkBox");
  ly->addWidget(dontShowCheck);

  // OK button
  auto *okBtn = new QPushButton("Got it");
  okBtn->setMinimumHeight(36);
  UIHelpers::applyPrimaryStyle(okBtn);
  connect(okBtn, &QPushButton::clicked, d, &QDialog::accept);
  ly->addWidget(okBtn);

  UIHelpers::centerDialog(d);
  d->exec();

  // Save preference
  if (dontShowCheck->isChecked()) {
    settings.setValue("ui/radialSetupGuideDismissed", true);
  }

  d->deleteLater();
}
