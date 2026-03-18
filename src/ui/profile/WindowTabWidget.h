#pragma once

#include <QWidget>

class QSpinBox;
class LabeledToggle;
class AccountProfile;

/**
 * @brief Window tab for ProfileEditor
 *
 * Manages window position and size for multi-boxing.
 * Uses WindowGridSelector for visual monitor selection.
 */
class WindowTabWidget : public QWidget {
  Q_OBJECT

public:
  /**
   * @brief Construct the Window tab
   * @param profile Reference to the profile being edited
   * @param parent Parent widget
   */
  explicit WindowTabWidget(AccountProfile &profile, QWidget *parent = nullptr);

  /**
   * @brief Load window settings from the profile
   */
  void load();

  /**
   * @brief Save window settings to the profile
   */
  void save();

signals:
  /**
   * @brief Emitted when window settings change
   */
  void modified();

private:
  void setupUI();

  AccountProfile &m_profile;

  LabeledToggle *m_customWindowToggle = nullptr;
  QSpinBox *m_windowX = nullptr;
  QSpinBox *m_windowY = nullptr;
  QSpinBox *m_windowWidth = nullptr;
  QSpinBox *m_windowHeight = nullptr;

  // Groups that need to be enabled/disabled based on toggle
  QWidget *m_posGroup = nullptr;
  QWidget *m_previewGroup = nullptr;
};
