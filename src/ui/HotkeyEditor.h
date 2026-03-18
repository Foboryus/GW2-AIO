#pragma once

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>


#include "features/radial/RadialConfig.h"
#include "features/radial/RadialMenu.h"


/**
 * @brief Visual editor for radial menu hotkeys
 *
 * DO NOT ADD:
 * - Inline implementations (use HotkeyEditor.cpp)
 */
class HotkeyEditor : public QWidget {
  Q_OBJECT

public:
  explicit HotkeyEditor(RadialConfig *config, QWidget *parent = nullptr);

  /**
   * @brief Refresh the menu list
   */
  void refresh();

signals:
  void hotkeysChanged();

private slots:
  void onMenuSelected(int index);
  void onHotkeyRecorded(const QKeySequence &keySequence);
  void onSaveClicked();
  void onResetClicked();

private:
  void setupUI();
  void loadMenus();
  void updateHotkeyDisplay();

  RadialConfig *m_config;

  QListWidget *m_menuList;
  QLabel *m_hotkeyLabel;
  QKeySequenceEdit *m_hotkeyEdit;
  QPushButton *m_saveButton;
  QPushButton *m_resetButton;

  int m_selectedMenuIndex = -1;
};
