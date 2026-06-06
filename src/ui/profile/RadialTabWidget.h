#pragma once

/**
 * @brief Radial menu tab for ProfileEditor
 *
 * Configures per-profile radial menu settings:
 * wheels (mount/novelty/marker), display, interaction, queuing.
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialTabWidget.cpp)
 * - Rendering logic (belongs in RadialController/RadialWheel)
 * - Inline styles (use UIHelpers role-based styling)
 */

#include <QWidget>

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QVBoxLayout;
class LabeledToggle;
struct AccountProfile;
class DataService;
class RadialSettingsManager;

class RadialTabWidget : public QWidget {
  Q_OBJECT

public:
  explicit RadialTabWidget(AccountProfile &profile,
                           DataService *dataService = nullptr,
                           QWidget *parent = nullptr);

  /**
   * @brief Load settings from RadialSettingsManager
   */
  void load();

  /**
   * @brief Save settings to RadialSettingsManager
   */
  void save();

signals:
  void modified();

private:
  void setupUI();
  void setupGeneralSection(QVBoxLayout *contentLayout);
  void setupMountSection(QVBoxLayout *contentLayout);
  void setupNoveltySection(QVBoxLayout *contentLayout);
  void setupMarkerSection(QVBoxLayout *contentLayout);
  void setupDisplaySection(QVBoxLayout *contentLayout);
  void setupInteractionSection(QVBoxLayout *contentLayout);

  /**
   * @brief Detect GW2 keybinds from newest exported InputBinds XML
   */
  void detectGw2Keybinds();

  /**
   * @brief Browse for a specific GW2 InputBinds XML file
   */
  void browseGw2Keybinds();

  /**
   * @brief Apply keybinds from a parsed XML file to element tables
   */
  void applyKeybindsFromFile(const QString &xmlPath);

  /**
   * @brief Show first-time radial setup guide popup
   */
  void showFirstTimeSetupGuide();

  /**
   * @brief Create a wheel element table with enable toggles
   * @param elements Map of element key → config
   * @param labels Map of element key → display name
   * @return The table widget
   */
  QTableWidget *createElementTable(
      const QMap<QString, QString> &labels);

  /**
   * @brief Create a wheel element table with ordered entries
   * @param entries Ordered list of (key, displayName) pairs
   * @return The table widget (rows starting with '_' have no toggle)
   */
  QTableWidget *createOrderedElementTable(
      const QList<QPair<QString, QString>> &entries);

  AccountProfile &m_profile;
  DataService *m_dataService = nullptr;
  RadialSettingsManager *m_radialSettings = nullptr;

  // --- General ---
  LabeledToggle *m_masterToggle = nullptr;
  QComboBox *m_iconStyleCombo = nullptr;
  QLabel *m_detectStatusLabel = nullptr;

  // --- Wheels ---
  LabeledToggle *m_mountWheelToggle = nullptr;
  QKeySequenceEdit *m_mountHotkeyEdit = nullptr;
  QTableWidget *m_mountTable = nullptr;

  LabeledToggle *m_noveltyWheelToggle = nullptr;
  QKeySequenceEdit *m_noveltyHotkeyEdit = nullptr;
  QTableWidget *m_noveltyTable = nullptr;

  LabeledToggle *m_markerWheelToggle = nullptr;
  QKeySequenceEdit *m_markerHotkeyEdit = nullptr;
  QTableWidget *m_markerTable = nullptr;

  // --- Display ---
  QSlider *m_scaleSlider = nullptr;
  QLabel *m_scaleLabel = nullptr;
  QSlider *m_opacitySlider = nullptr;
  QLabel *m_opacityLabel = nullptr;
  QSpinBox *m_animTimeSpin = nullptr;

  // --- Interaction ---
  QComboBox *m_centerBehaviorCombo = nullptr;
  LabeledToggle *m_noHoldToggle = nullptr;
  LabeledToggle *m_clickSelectToggle = nullptr;
  LabeledToggle *m_resetCursorToggle = nullptr;
  LabeledToggle *m_lockCameraToggle = nullptr;
  LabeledToggle *m_fastMountSwapToggle = nullptr;

  // --- Queuing ---
  LabeledToggle *m_queuingToggle = nullptr;
  QSpinBox *m_queueTimeoutSpin = nullptr;
  QSpinBox *m_queueDelaySpin = nullptr;

  // Guard: prevents circular reload when save() triggers settingsChanged
  bool m_saving = false;
};
