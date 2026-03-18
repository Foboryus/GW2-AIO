#pragma once

#include <QWidget>

class QLabel;
class QComboBox;
class QSlider;
class QGroupBox;
class LabeledToggle;
class AccountProfile;
class DataService;
struct GfxSettings;

/**
 * @brief Graphics tab for ProfileEditor
 *
 * Full graphics settings editor with presets, individual controls,
 * and save/export/import functionality.
 */
class GraphicsTabWidget : public QWidget {
  Q_OBJECT

public:
  explicit GraphicsTabWidget(AccountProfile &profile,
                             DataService *dataService = nullptr,
                             QWidget *parent = nullptr);

  void load();
  void save();

signals:
  void modified();

private slots:
  void onPresetChanged(int index);
  void onSettingChanged();
  void onSaveClicked();
  void onExportClicked();
  void onImportClicked();
  void onLoadFromGW2Clicked();

private:
  void setupUI();
  void loadSettingsToUI(const GfxSettings &settings);
  GfxSettings gatherSettingsFromUI();
  void updatePresetDropdown();
  void checkWindowTabSync();

  AccountProfile &m_profile;
  DataService *m_dataService = nullptr;

  // Header
  QComboBox *m_presetCombo = nullptr;
  QLabel *m_statusLabel = nullptr;

  // Display
  QComboBox *m_resolutionCombo = nullptr;
  QComboBox *m_screenModeCombo = nullptr;
  QComboBox *m_frameLimitCombo = nullptr;
  LabeledToggle *m_vsyncToggle = nullptr;
  LabeledToggle *m_dpiScalingToggle = nullptr;
  QSlider *m_gammaSlider = nullptr;
  QLabel *m_gammaLabel = nullptr;

  // Quality
  QComboBox *m_shadowsCombo = nullptr;
  QComboBox *m_reflectionsCombo = nullptr;
  QComboBox *m_texturesCombo = nullptr;
  QComboBox *m_shadersCombo = nullptr;
  QComboBox *m_environmentCombo = nullptr;
  QComboBox *m_animationCombo = nullptr;
  QComboBox *m_lodDistanceCombo = nullptr;
  QComboBox *m_antiAliasingCombo = nullptr;
  QComboBox *m_samplingCombo = nullptr;

  // Characters
  QComboBox *m_charModelLimitCombo = nullptr;
  QComboBox *m_charModelQualityCombo = nullptr;
  LabeledToggle *m_highResCharToggle = nullptr;
  LabeledToggle *m_effectLodToggle = nullptr;

  // Advanced
  LabeledToggle *m_bestTexFilterToggle = nullptr;
  LabeledToggle *m_screenShadowsToggle = nullptr;
  QComboBox *m_postProcCombo = nullptr;
  LabeledToggle *m_depthBlurToggle = nullptr;

  // Apply toggle
  LabeledToggle *m_useCustomGfxToggle = nullptr;

  // Warning label for window sync
  QLabel *m_syncWarningLabel = nullptr;

  // Track if we're loading (suppress change signals)
  bool m_loading = false;
};
