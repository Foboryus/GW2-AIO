#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class LabeledToggle;
class AccountProfile;
class DataService;
class APIKeyManager;

/**
 * @brief Login tab for ProfileEditor
 *
 * Manages Local.dat saving, custom GW2 path, and auto-login settings.
 */
class LoginTabWidget : public QWidget {
  Q_OBJECT

public:
  /**
   * @brief Construct the Login tab
   * @param profile Reference to the profile being edited
   * @param parent Parent widget
   */
  explicit LoginTabWidget(AccountProfile &profile,
                          DataService *dataService = nullptr,
                          QWidget *parent = nullptr);

  /**
   * @brief Load login settings from the profile
   */
  void load();

  /**
   * @brief Save login settings to the profile
   */
  void save();

signals:
  /**
   * @brief Emitted when login settings change
   */
  void modified();

private:
  void setupUI();

  AccountProfile &m_profile;
  DataService *m_dataService = nullptr;
  APIKeyManager *m_apiKeyManager = nullptr;

  // Local.dat section
  QLabel *m_localDatLabel = nullptr;
  LabeledToggle *m_autoLoginToggle = nullptr;

  // Custom GW2 path section
  LabeledToggle *m_customPathToggle = nullptr;
  QLabel *m_customPathLabel = nullptr;

  // API key section
  QLineEdit *m_apiKeyInput = nullptr;
  QPushButton *m_apiKeyShowBtn = nullptr;
  QLabel *m_apiKeyStatus = nullptr;
  QPushButton *m_validateBtn = nullptr;
  QPushButton *m_removeKeyBtn = nullptr;
};
