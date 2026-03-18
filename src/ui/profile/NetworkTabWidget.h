#pragma once

#include <QWidget>

class QComboBox;
class QGroupBox;
class QLabel;
class LabeledToggle;
class AccountProfile;
class ServerManager;

/**
 * @brief Network tab for profile editor
 *
 * Manages per-profile network settings with three mutually exclusive modes:
 * - UseDefault: No custom args (ArenaNet defaults)
 * - UseGlobal: Follow Global Network tab settings
 * - Custom: Profile-specific server override
 */
class NetworkTabWidget : public QWidget {
  Q_OBJECT

public:
  explicit NetworkTabWidget(AccountProfile &profile,
                            ServerManager *serverManager = nullptr,
                            QWidget *parent = nullptr);

  void load();
  void save();

signals:
  void modified();

private:
  void setupUI();

  AccountProfile &m_profile;
  ServerManager *m_serverManager;

  // Three interlocked toggles - only one can be ON at a time
  LabeledToggle *m_defaultToggle = nullptr;
  LabeledToggle *m_globalToggle = nullptr;
  LabeledToggle *m_customToggle = nullptr;

  // Server dropdown (enabled only when Custom is selected)
  QComboBox *m_serverCombo = nullptr;
  QGroupBox *m_serverGroup = nullptr;

  // Shows current global network args
  QLabel *m_globalArgsLabel = nullptr;
};
