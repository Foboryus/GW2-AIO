#pragma once

#include <QStringList>
#include <QWidget>


class QListWidget;
class AccountProfile;

/**
 * @brief Addons tab for ProfileEditor
 *
 * Manages DLL injection list for the profile.
 * Allows adding/removing DLLs that will be injected on launch.
 */
class AddonsTabWidget : public QWidget {
  Q_OBJECT

public:
  /**
   * @brief Construct the Addons tab
   * @param profile Reference to the profile being edited
   * @param parent Parent widget
   */
  explicit AddonsTabWidget(AccountProfile &profile, QWidget *parent = nullptr);

  /**
   * @brief Load DLL list from the profile
   */
  void load();

  /**
   * @brief Save DLL list to the profile
   */
  void save();

signals:
  /**
   * @brief Emitted when DLL list changes
   */
  void modified();

private slots:
  void onAddDll();
  void onRemoveDll();

private:
  void setupUI();

  AccountProfile &m_profile;
  QListWidget *m_dllList = nullptr;
};
