#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class LabeledToggle;
class AccountProfile;

/**
 * @brief General tab for profile editor
 *
 * Manages basic profile settings:
 * - Profile name
 * - Profile icon selection
 * - Account provider (Standalone/Steam/Epic)
 * - Process priority
 * - Session history display
 */
class GeneralTabWidget : public QWidget {
  Q_OBJECT

public:
  explicit GeneralTabWidget(AccountProfile &profile, QWidget *parent = nullptr);

  void load();
  void save();

  // Getters for validation (used by ProfileEditor::onAccept)
  QString getNickname() const;
  bool isNicknameEmpty() const;
  void focusNickname();

signals:
  void modified();

private:
  void setupUI();

  AccountProfile &m_profile;

  // Profile name
  QLineEdit *m_nicknameEdit = nullptr;

  // Icon picker
  QLabel *m_iconLabel = nullptr;

  // Account provider toggles (interlocked)
  LabeledToggle *m_providerStandaloneToggle = nullptr;
  LabeledToggle *m_providerSteamToggle = nullptr;
  LabeledToggle *m_providerEpicToggle = nullptr;

  // Performance
  QComboBox *m_priorityCombo = nullptr;
};
