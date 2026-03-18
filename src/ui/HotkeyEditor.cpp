/**
 * @file HotkeyEditor.cpp
 * @brief Visual editor for radial menu hotkeys
 *
 * DO NOT ADD:
 * - Hotkey registration (belongs in HotkeyManager)
 * - Menu items (belongs in RadialConfig)
 */

#include "HotkeyEditor.h"
#include "UIHelpers.h"

HotkeyEditor::HotkeyEditor(RadialConfig *config, QWidget *parent)
    : QWidget(parent), m_config(config) {
  setupUI();
  loadMenus();
}

void HotkeyEditor::setupUI() {
  setWindowTitle("Hotkey Editor");
  setMinimumSize(400, 300);

  // Themed via global QSS — no widget-level stylesheet

  QHBoxLayout *mainLayout = new QHBoxLayout(this);
  mainLayout->setSpacing(16);
  mainLayout->setContentsMargins(16, 16, 16, 16);

  // Left: Menu list
  QGroupBox *menuGroup = new QGroupBox("Radial Menus");
  QVBoxLayout *menuLayout = new QVBoxLayout(menuGroup);

  m_menuList = new QListWidget();
  connect(m_menuList, &QListWidget::currentRowChanged, this,
          &HotkeyEditor::onMenuSelected);
  menuLayout->addWidget(m_menuList);

  mainLayout->addWidget(menuGroup, 1);

  // Right: Hotkey editor
  QGroupBox *hotkeyGroup = new QGroupBox("Hotkey Settings");
  QVBoxLayout *hotkeyLayout = new QVBoxLayout(hotkeyGroup);
  hotkeyLayout->setSpacing(12);

  QLabel *instructionLabel = new QLabel(
      "Click the field below and press your desired key combination:");
  instructionLabel->setWordWrap(true);
  UIHelpers::applyHintRole(instructionLabel);
  hotkeyLayout->addWidget(instructionLabel);

  m_hotkeyLabel = new QLabel("Current: None");
  UIHelpers::applyGoldColorRole(m_hotkeyLabel);
  m_hotkeyLabel->setStyleSheet(
      QString("font-size: %1px; font-weight: bold;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeNormal));
  hotkeyLayout->addWidget(m_hotkeyLabel);

  m_hotkeyEdit = new QKeySequenceEdit();
  m_hotkeyEdit->setMaximumSequenceLength(1);
  connect(m_hotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this,
          &HotkeyEditor::onHotkeyRecorded);
  hotkeyLayout->addWidget(m_hotkeyEdit);

  hotkeyLayout->addStretch();

  // Buttons
  QHBoxLayout *buttonLayout = new QHBoxLayout();

  m_resetButton = new QPushButton("Reset to Default");
  UIHelpers::applyCancelStyle(m_resetButton);
  connect(m_resetButton, &QPushButton::clicked, this,
          &HotkeyEditor::onResetClicked);
  buttonLayout->addWidget(m_resetButton);

  buttonLayout->addStretch();

  m_saveButton = new QPushButton("Save");
  UIHelpers::applyPrimaryStyle(m_saveButton);
  connect(m_saveButton, &QPushButton::clicked, this,
          &HotkeyEditor::onSaveClicked);
  buttonLayout->addWidget(m_saveButton);

  hotkeyLayout->addLayout(buttonLayout);

  mainLayout->addWidget(hotkeyGroup, 1);
}

void HotkeyEditor::refresh() { loadMenus(); }

void HotkeyEditor::loadMenus() {
  m_menuList->clear();

  const auto &menus = m_config->menus();
  for (const RadialMenu &menu : menus) {
    QString text = QString("%1 [%2]").arg(menu.name, menu.hotkey);
    m_menuList->addItem(text);
  }

  if (!menus.isEmpty()) {
    m_menuList->setCurrentRow(0);
  }
}

void HotkeyEditor::onMenuSelected(int index) {
  m_selectedMenuIndex = index;
  updateHotkeyDisplay();
}

void HotkeyEditor::updateHotkeyDisplay() {
  if (m_selectedMenuIndex < 0)
    return;

  const auto &menus = m_config->menus();
  if (m_selectedMenuIndex >= menus.size())
    return;

  const RadialMenu &menu = menus[m_selectedMenuIndex];
  m_hotkeyLabel->setText(QString("Current: %1").arg(menu.hotkey));
  m_hotkeyEdit->clear();
}

void HotkeyEditor::onHotkeyRecorded(const QKeySequence &keySequence) {
  if (keySequence.isEmpty())
    return;

  QString keyStr = keySequence.toString();
  m_hotkeyLabel->setText(QString("New: %1").arg(keyStr));
}

void HotkeyEditor::onSaveClicked() {
  if (m_selectedMenuIndex < 0)
    return;

  QString newHotkey = m_hotkeyEdit->keySequence().toString();
  if (newHotkey.isEmpty())
    return;

  // Get reference to menus and update directly
  QList<RadialMenu> &menus = m_config->menus();
  if (m_selectedMenuIndex < menus.size()) {
    menus[m_selectedMenuIndex].hotkey = newHotkey;

    // Save the updated config
    m_config->saveConfig();

    loadMenus();
    emit hotkeysChanged();
  }
}

void HotkeyEditor::onResetClicked() {
  m_config->resetToDefaults();
  loadMenus();
  emit hotkeysChanged();
}
