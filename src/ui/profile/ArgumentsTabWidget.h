#pragma once

#include <QList>
#include <QWidget>


class LabeledToggle;
class AccountProfile;
struct LaunchArg;

/**
 * @brief Arguments tab for ProfileEditor
 *
 * Displays toggleable launch arguments for the GW2 executable.
 * Each toggle corresponds to a standard command-line argument.
 */
class ArgumentsTabWidget : public QWidget {
  Q_OBJECT

public:
  /**
   * @brief Construct the Arguments tab
   * @param profile Reference to the profile being edited
   * @param standardArgs List of available arguments with descriptions
   * @param parent Parent widget
   */
  explicit ArgumentsTabWidget(AccountProfile &profile,
                              const QList<LaunchArg> &standardArgs,
                              QWidget *parent = nullptr);

  /**
   * @brief Load argument states from the profile
   */
  void load();

  /**
   * @brief Save argument states to the profile
   */
  void save();

signals:
  /**
   * @brief Emitted when user toggles any argument
   */
  void modified();

private:
  void setupUI();

  AccountProfile &m_profile;
  QList<LaunchArg> m_standardArgs;
  QList<LabeledToggle *> m_argToggles;
};
