/**
 * @file HotkeysTabWidget.cpp
 * @brief Per-profile global hotkey configuration
 *
 * Graphics-tab-style layout with QGroupBox + QGridLayout.
 * Includes import/export and conflict detection.
 *
 * DO NOT ADD:
 * - Hotkey registration logic (belongs in MainWindow)
 * - Window focus/minimize actions
 */

#include "HotkeysTabWidget.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "core/AtomicFileWriter.h"
#include "core/DataService.h"
#include "core/ProfileManager.h"
#include "ui/UIHelpers.h"

/**
 * @brief QLineEdit subclass that captures key combos
 *
 * When focused, records the next key+modifier press as a hotkey string.
 * Format: "Ctrl+Shift+F1", "Alt+V", "F5", etc.
 */
class HotkeyLineEdit : public QLineEdit {
public:
  explicit HotkeyLineEdit(QWidget *parent = nullptr) : QLineEdit(parent) {
    setReadOnly(true);
    setPlaceholderText("Click and press a key combo...");
    setAlignment(Qt::AlignCenter);
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    // Ignore bare modifiers
    int key = event->key();
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
        key == Qt::Key_Meta) {
      return;
    }

    // Ignore Escape (used to cancel/clear)
    if (key == Qt::Key_Escape) {
      setText("");
      return;
    }

    // Build modifier string
    QStringList parts;
    Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & Qt::ControlModifier)
      parts << "Ctrl";
    if (mods & Qt::ShiftModifier)
      parts << "Shift";
    if (mods & Qt::AltModifier)
      parts << "Alt";

    // Convert key to readable string
    QString keyStr = keyToString(key);
    if (keyStr.isEmpty())
      return; // Unsupported key

    parts << keyStr;
    setText(parts.join("+"));

    // Emit editingFinished to trigger modified signal
    clearFocus();
  }

private:
  static QString keyToString(int key) {
    // F1-F12
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
      return QString("F%1").arg(key - Qt::Key_F1 + 1);
    }
    // Letters A-Z
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
      return QChar(key);
    }
    // Numbers 0-9
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
      return QChar(key);
    }
    // Special keys
    switch (key) {
    case Qt::Key_Space:
      return "Space";
    case Qt::Key_Tab:
      return "Tab";
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return "Return";
    case Qt::Key_Delete:
      return "Delete";
    case Qt::Key_Home:
      return "Home";
    case Qt::Key_End:
      return "End";
    case Qt::Key_Insert:
      return "Insert";
    case Qt::Key_PageUp:
      return "PageUp";
    case Qt::Key_PageDown:
      return "PageDown";
    default:
      return "";
    }
  }
};

HotkeysTabWidget::HotkeysTabWidget(AccountProfile &profile,
                                   DataService *dataService, QWidget *parent)
    : QWidget(parent), m_profile(profile), m_dataService(dataService) {
  setupUI();
  load();
}

void HotkeysTabWidget::setupUI() {
  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  // === Header: Export/Import Buttons ===
  auto *headerLayout = new QHBoxLayout();
  headerLayout->addStretch();

  auto *exportBtn =
      new QPushButton(UIHelpers::themedIcon("download"), "Export Hotkeys");
  UIHelpers::applyNeutralStyle(exportBtn);
  exportBtn->setToolTip("Export hotkey settings to a file for sharing");
  connect(exportBtn, &QPushButton::clicked, this,
          &HotkeysTabWidget::onExportClicked);
  headerLayout->addWidget(exportBtn);

  auto *importBtn =
      new QPushButton(UIHelpers::themedIcon("upload"), "Import Hotkeys");
  UIHelpers::applyNeutralStyle(importBtn);
  importBtn->setToolTip("Import hotkey settings from a file");
  connect(importBtn, &QPushButton::clicked, this,
          &HotkeysTabWidget::onImportClicked);
  headerLayout->addWidget(importBtn);

  layout->addLayout(headerLayout);

  // === Status Label ===
  m_statusLabel = new QLabel("");
  UIHelpers::applyHintRole(m_statusLabel);
  m_statusLabel->setFixedHeight(20);
  layout->addWidget(m_statusLabel);

  // === Window Management Group ===
  auto *windowGroup = new QGroupBox("Window Management");
  UIHelpers::applyGroupBoxRole(windowGroup);
  auto *grid = new QGridLayout(windowGroup);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(8);
  grid->setColumnMinimumWidth(0, 130);
  grid->setColumnStretch(1, 1);
  grid->setColumnMinimumWidth(2, 70);

  // Row 0: Focus Window
  auto *focusLabel = new QLabel("Focus Window");
  UIHelpers::applyLabelRole(focusLabel);
  focusLabel->setToolTip("Bring this profile's GW2 window to the front");
  grid->addWidget(focusLabel, 0, 0);

  m_focusEdit = new HotkeyLineEdit(this);
  UIHelpers::applyInputFieldRole(m_focusEdit);
  m_focusEdit->setMinimumHeight(36);
  grid->addWidget(m_focusEdit, 0, 1);

  m_clearFocusBtn = new QPushButton("Clear");
  UIHelpers::applyCancelStyle(m_clearFocusBtn);
  m_clearFocusBtn->setFixedWidth(70);
  connect(m_clearFocusBtn, &QPushButton::clicked, this, [this]() {
    m_focusEdit->setText("");
    emit modified();
  });
  grid->addWidget(m_clearFocusBtn, 0, 2);

  // Row 1: Minimize
  auto *minimizeLabel = new QLabel("Minimize");
  UIHelpers::applyLabelRole(minimizeLabel);
  minimizeLabel->setToolTip("Minimize this profile's GW2 window");
  grid->addWidget(minimizeLabel, 1, 0);

  m_minimizeEdit = new HotkeyLineEdit(this);
  UIHelpers::applyInputFieldRole(m_minimizeEdit);
  m_minimizeEdit->setMinimumHeight(36);
  grid->addWidget(m_minimizeEdit, 1, 1);

  m_clearMinimizeBtn = new QPushButton("Clear");
  UIHelpers::applyCancelStyle(m_clearMinimizeBtn);
  m_clearMinimizeBtn->setFixedWidth(70);
  connect(m_clearMinimizeBtn, &QPushButton::clicked, this, [this]() {
    m_minimizeEdit->setText("");
    emit modified();
  });
  grid->addWidget(m_clearMinimizeBtn, 1, 2);

  layout->addWidget(windowGroup);

  layout->addStretch();

  // === Warning ===
  auto *warning = new QLabel(
      "Warning: Hotkeys are system-wide. The key will NOT reach GW2 or other "
      "apps while registered. Use combos like Ctrl+F1 that you don't use "
      "in-game.");
  warning->setWordWrap(true);
  UIHelpers::applyWarningColorRole(warning);
  layout->addWidget(warning);

  // Connect text changes to modified signal
  connect(m_focusEdit, &QLineEdit::textChanged, this,
          &HotkeysTabWidget::modified);
  connect(m_minimizeEdit, &QLineEdit::textChanged, this,
          &HotkeysTabWidget::modified);
}

void HotkeysTabWidget::load() {
  m_focusEdit->blockSignals(true);
  m_focusEdit->setText(m_profile.hotkeyFocus);
  m_focusEdit->blockSignals(false);

  m_minimizeEdit->blockSignals(true);
  m_minimizeEdit->setText(m_profile.hotkeyMinimize);
  m_minimizeEdit->blockSignals(false);
}

void HotkeysTabWidget::save() {
  m_profile.hotkeyFocus = m_focusEdit->text().trimmed();
  m_profile.hotkeyMinimize = m_minimizeEdit->text().trimmed();

  // Check for conflicts with other profiles
  QString conflict = checkConflicts();
  if (!conflict.isEmpty()) {
    m_statusLabel->setText(conflict);
    UIHelpers::applyWarningColorRole(m_statusLabel);
    QTimer::singleShot(5000, m_statusLabel, [this]() {
      m_statusLabel->clear();
      UIHelpers::applyHintRole(m_statusLabel);
    });
  }
}

QString HotkeysTabWidget::checkConflicts() const {
  if (!m_dataService)
    return {};

  const auto profiles = m_dataService->profiles();
  QString focusKey = m_focusEdit->text().trimmed();
  QString minimizeKey = m_minimizeEdit->text().trimmed();

  for (const auto &other : profiles) {
    if (other.id == m_profile.id)
      continue; // Skip self

    if (!focusKey.isEmpty()) {
      if (other.hotkeyFocus == focusKey || other.hotkeyMinimize == focusKey) {
        return QString("Conflict: %1 is already used by '%2'")
            .arg(focusKey, other.nickname);
      }
    }
    if (!minimizeKey.isEmpty()) {
      if (other.hotkeyFocus == minimizeKey ||
          other.hotkeyMinimize == minimizeKey) {
        return QString("Conflict: %1 is already used by '%2'")
            .arg(minimizeKey, other.nickname);
      }
    }
  }

  // Also check self-conflict
  if (!focusKey.isEmpty() && focusKey == minimizeKey) {
    return "Focus and Minimize cannot use the same hotkey";
  }

  return {};
}

void HotkeysTabWidget::onExportClicked() {
  QString defaultName = m_profile.nickname + "_hotkeys.gw2hotkeys";
  QString filePath = QFileDialog::getSaveFileName(
      this, "Export Hotkeys", QDir::homePath() + "/" + defaultName,
      "GW2 AIO Hotkeys (*.gw2hotkeys)");
  if (filePath.isEmpty())
    return;

  QJsonObject obj;
  obj["type"] = "gw2aio_hotkeys";
  obj["version"] = 1;
  obj["hotkeyFocus"] = m_focusEdit->text().trimmed();
  obj["hotkeyMinimize"] = m_minimizeEdit->text().trimmed();

  if (AtomicFileWriter::writeJson(filePath, obj)) {
    m_statusLabel->setText("Hotkeys exported successfully!");
    UIHelpers::applySuccessColorRole(m_statusLabel);
  } else {
    m_statusLabel->setText("Failed to export hotkeys.");
    UIHelpers::applyErrorColorRole(m_statusLabel);
  }

  QTimer::singleShot(3000, m_statusLabel, [this]() {
    m_statusLabel->clear();
    UIHelpers::applyHintRole(m_statusLabel);
  });
}

void HotkeysTabWidget::onImportClicked() {
  QString filePath = QFileDialog::getOpenFileName(
      this, "Import Hotkeys", QDir::homePath(),
      "GW2 AIO Hotkeys (*.gw2hotkeys);;JSON files (*.json)");
  if (filePath.isEmpty())
    return;

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    m_statusLabel->setText("Failed to open file.");
    UIHelpers::applyErrorColorRole(m_statusLabel);
    QTimer::singleShot(3000, m_statusLabel, [this]() {
      m_statusLabel->clear();
      UIHelpers::applyHintRole(m_statusLabel);
    });
    return;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    m_statusLabel->setText("Invalid file format.");
    UIHelpers::applyErrorColorRole(m_statusLabel);
    QTimer::singleShot(3000, m_statusLabel, [this]() {
      m_statusLabel->clear();
      UIHelpers::applyHintRole(m_statusLabel);
    });
    return;
  }

  QJsonObject obj = doc.object();
  if (obj["type"].toString() != "gw2aio_hotkeys") {
    m_statusLabel->setText("Not a valid hotkeys file.");
    UIHelpers::applyErrorColorRole(m_statusLabel);
    QTimer::singleShot(3000, m_statusLabel, [this]() {
      m_statusLabel->clear();
      UIHelpers::applyHintRole(m_statusLabel);
    });
    return;
  }

  // Populate fields — does NOT auto-save
  m_focusEdit->setText(obj["hotkeyFocus"].toString());
  m_minimizeEdit->setText(obj["hotkeyMinimize"].toString());

  m_statusLabel->setText("Hotkeys imported. Save to apply.");
  UIHelpers::applySuccessColorRole(m_statusLabel);
  QTimer::singleShot(3000, m_statusLabel, [this]() {
    m_statusLabel->clear();
    UIHelpers::applyHintRole(m_statusLabel);
  });

  emit modified();
}

#include "HotkeysTabWidget.moc"
