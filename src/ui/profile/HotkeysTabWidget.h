#pragma once

/**
 * @brief Hotkeys tab for ProfileEditor
 *
 * Per-profile global hotkey configuration with import/export.
 * Uses QGridLayout for scalable two-column layout.
 *
 * DO NOT ADD:
 * - Hotkey registration (belongs in MainWindow)
 * - Inline implementations
 */

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
struct AccountProfile;
class DataService;

class HotkeysTabWidget : public QWidget {
  Q_OBJECT

public:
  explicit HotkeysTabWidget(AccountProfile &profile, DataService *dataService,
                            QWidget *parent = nullptr);

  void load();
  void save();

signals:
  void modified();

private slots:
  void onExportClicked();
  void onImportClicked();

private:
  void setupUI();
  QString checkConflicts() const;

  AccountProfile &m_profile;
  DataService *m_dataService;

  QLineEdit *m_focusEdit = nullptr;
  QLineEdit *m_minimizeEdit = nullptr;
  QPushButton *m_clearFocusBtn = nullptr;
  QPushButton *m_clearMinimizeBtn = nullptr;
  QLabel *m_statusLabel = nullptr;
};
