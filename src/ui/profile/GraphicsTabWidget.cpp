#include "GraphicsTabWidget.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

#include "core/DataService.h"
#include "core/GFXManager.h"
#include "ui/ProfileEditor.h"
#include "ui/ToggleSwitch.h"
#include "ui/UIHelpers.h"

GraphicsTabWidget::GraphicsTabWidget(AccountProfile &profile,
                                     DataService *dataService, QWidget *parent)
    : QWidget(parent), m_profile(profile), m_dataService(dataService) {
  setupUI();
}

// Helper to create a styled combo box with tooltip
static QComboBox *createCombo(const QStringList &items,
                              const QString &tooltip = "") {
  auto *combo = new QComboBox();
  combo->addItems(items);
  combo->setMinimumWidth(160); // Increased from 140
  combo->setMinimumHeight(32);
  // comboBoxStyle removed — handled by global QSS
  if (!tooltip.isEmpty()) {
    combo->setToolTip(tooltip);
  }
  return combo;
}

void GraphicsTabWidget::setupUI() {
  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  // === Header: Preset + Buttons ===
  auto *headerLayout = new QHBoxLayout();

  auto *presetLabel = new QLabel("Preset:");
  presetLabel->setStyleSheet("font-weight: bold;");
  headerLayout->addWidget(presetLabel);

  m_presetCombo = new QComboBox();
  m_presetCombo->addItems(GFXManager::presetNames());
  m_presetCombo->setMinimumWidth(180);
  // comboBoxStyle removed — handled by global QSS
  m_presetCombo->setToolTip("Select a preset to auto-fill all settings");
  connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GraphicsTabWidget::onPresetChanged);
  headerLayout->addWidget(m_presetCombo);

  headerLayout->addStretch();

  auto *exportBtn =
      new QPushButton(UIHelpers::themedIcon("download"), "Export");
  UIHelpers::applyNeutralStyle(exportBtn);
  exportBtn->setToolTip("Export settings to file for sharing");
  connect(exportBtn, &QPushButton::clicked, this,
          &GraphicsTabWidget::onExportClicked);
  headerLayout->addWidget(exportBtn);

  auto *importBtn = new QPushButton(UIHelpers::themedIcon("upload"), "Import");
  UIHelpers::applyNeutralStyle(importBtn);
  importBtn->setToolTip("Import settings from a GFXSettings.xml file");
  connect(importBtn, &QPushButton::clicked, this,
          &GraphicsTabWidget::onImportClicked);
  headerLayout->addWidget(importBtn);

  auto *loadGw2Btn =
      new QPushButton(UIHelpers::themedIcon("refresh"), "Load GW2");
  UIHelpers::applyNeutralStyle(loadGw2Btn);
  loadGw2Btn->setToolTip("Load your current GW2 graphics settings");
  connect(loadGw2Btn, &QPushButton::clicked, this,
          &GraphicsTabWidget::onLoadFromGW2Clicked);
  headerLayout->addWidget(loadGw2Btn);

  layout->addLayout(headerLayout);

  // === Status Label ===
  m_statusLabel = new QLabel("");
  UIHelpers::applyHintRole(m_statusLabel);
  m_statusLabel->setFixedHeight(20); // Prevent layout shift when text changes
  layout->addWidget(m_statusLabel);

  // === Sync Warning ===
  m_syncWarningLabel = new QLabel();
  UIHelpers::applyWarningColorRole(m_syncWarningLabel);
  m_syncWarningLabel->setStyleSheet(
      QString("padding: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.paddingSmall));
  m_syncWarningLabel->setWordWrap(true);
  m_syncWarningLabel->hide();
  layout->addWidget(m_syncWarningLabel);

  // === Display Settings ===
  auto *displayGroup = new QGroupBox("Display");
  auto *displayGrid = new QGridLayout(displayGroup);
  displayGrid->setHorizontalSpacing(8);
  // Consistent column widths across all groups for alignment
  displayGrid->setColumnMinimumWidth(0, 85);  // Left label
  displayGrid->setColumnMinimumWidth(1, 180); // Left combo
  displayGrid->setColumnMinimumWidth(2, 40);  // Spacer
  displayGrid->setColumnMinimumWidth(3, 85);  // Right label
  displayGrid->setColumnStretch(1, 1);        // Left combo stretches
  displayGrid->setColumnStretch(4, 1);        // Right combo stretches

  // Resolution detection
  m_resolutionCombo = new QComboBox();
  m_resolutionCombo->setMinimumWidth(200);
  // comboBoxStyle removed — handled by global QSS
  m_resolutionCombo->setToolTip("Screen resolution for GW2");
  QList<QScreen *> screens = QGuiApplication::screens();
  QSet<QString> resolutions;
  for (QScreen *screen : screens) {
    QSize size = screen->size();
    qreal refresh = screen->refreshRate();
    QString res = QString("%1x%2 @ %3Hz")
                      .arg(size.width())
                      .arg(size.height())
                      .arg(qRound(refresh));
    if (!resolutions.contains(res)) {
      resolutions.insert(res);
      m_resolutionCombo->addItem(res, QSize(size.width(), size.height()));
    }
    // Also add common lower resolutions
    QList<QSize> common = {
        {1920, 1080}, {1680, 1050}, {1600, 900}, {1280, 720}};
    for (const QSize &s : common) {
      if (s.width() <= size.width()) {
        QString r = QString("%1x%2 @ %3Hz")
                        .arg(s.width())
                        .arg(s.height())
                        .arg(qRound(refresh));
        if (!resolutions.contains(r)) {
          resolutions.insert(r);
          m_resolutionCombo->addItem(r, s);
        }
      }
    }
  }
  connect(m_resolutionCombo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &GraphicsTabWidget::onSettingChanged);

  m_screenModeCombo =
      createCombo({"Windowed", "Fullscreen", "Windowed Fullscreen"},
                  "Windowed Fullscreen recommended for overlays");
  connect(m_screenModeCombo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &GraphicsTabWidget::onSettingChanged);
  connect(m_screenModeCombo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &GraphicsTabWidget::checkWindowTabSync);

  m_frameLimitCombo =
      createCombo({"Unlimited", "144", "120", "60", "30"},
                  "Limit frame rate to reduce heat and power usage");
  connect(m_frameLimitCombo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &GraphicsTabWidget::onSettingChanged);

  m_vsyncToggle = new LabeledToggle("VSync");
  m_vsyncToggle->setToolTip("Sync frame rate to monitor refresh rate");
  connect(m_vsyncToggle, &LabeledToggle::toggled, this,
          &GraphicsTabWidget::onSettingChanged);

  m_dpiScalingToggle = new LabeledToggle("DPI Scaling");
  m_dpiScalingToggle->setToolTip("Scale UI based on Windows DPI settings");
  connect(m_dpiScalingToggle, &LabeledToggle::toggled, this,
          &GraphicsTabWidget::onSettingChanged);

  m_gammaSlider = new QSlider(Qt::Horizontal);
  m_gammaSlider->setRange(50, 350);
  m_gammaSlider->setValue(100);
  m_gammaSlider->setToolTip("Brightness adjustment (0.5 - 3.5)");
  m_gammaLabel = new QLabel("1.0");
  connect(m_gammaSlider, &QSlider::valueChanged, [this](int val) {
    m_gammaLabel->setText(QString::number(val / 100.0, 'f', 1));
    onSettingChanged();
  });

  int row = 0;
  // Left pair: columns 0,1 | Spacer: column 2 | Right pair: columns 3,4
  displayGrid->addWidget(new QLabel("Resolution"), row, 0);
  displayGrid->addWidget(m_resolutionCombo, row, 1);
  displayGrid->addWidget(new QLabel("Mode"), row, 3);
  displayGrid->addWidget(m_screenModeCombo, row, 4);
  row++;
  displayGrid->addWidget(new QLabel("Frame Limit"), row, 0);
  displayGrid->addWidget(m_frameLimitCombo, row, 1);
  displayGrid->addWidget(m_vsyncToggle, row, 3, 1, 2);
  row++;
  displayGrid->addWidget(new QLabel("Gamma"), row, 0);
  auto *gammaLayout = new QHBoxLayout();
  gammaLayout->addWidget(m_gammaSlider);
  gammaLayout->addWidget(m_gammaLabel);
  displayGrid->addLayout(gammaLayout, row, 1);
  displayGrid->addWidget(m_dpiScalingToggle, row, 3, 1, 2);

  layout->addWidget(displayGroup);

  // === Quality Settings ===
  auto *qualityGroup = new QGroupBox("Quality");
  auto *qualityGrid = new QGridLayout(qualityGroup);
  qualityGrid->setHorizontalSpacing(8);
  qualityGrid->setColumnMinimumWidth(0, 85);  // Left label
  qualityGrid->setColumnMinimumWidth(1, 180); // Left combo
  qualityGrid->setColumnMinimumWidth(2, 40);  // Spacer
  qualityGrid->setColumnMinimumWidth(3, 85);  // Right label
  qualityGrid->setColumnStretch(1, 1);
  qualityGrid->setColumnStretch(4, 1);

  m_shadowsCombo = createCombo({"Off", "Low", "Medium", "High", "Ultra"},
                               "Shadow quality - High impact on performance");
  m_reflectionsCombo = createCombo({"None", "Terrain & Sky", "All"},
                                   "Water reflections quality");
  m_texturesCombo = createCombo({"Low", "Medium", "High"},
                                "Texture resolution - Requires VRAM");
  m_shadersCombo =
      createCombo({"Low", "Medium", "High"}, "Shader quality affects lighting");
  m_environmentCombo =
      createCombo({"Low", "Medium", "High"}, "Terrain and foliage detail");
  m_animationCombo =
      createCombo({"Low", "Medium", "High"}, "Character animation quality");
  m_lodDistanceCombo = createCombo({"Low", "Medium", "High", "Ultra"},
                                   "Level of detail distance");
  m_antiAliasingCombo = createCombo({"None", "FXAA", "SMAA Low", "SMAA High"},
                                    "Edge smoothing - SMAA High recommended");
  m_samplingCombo = createCombo({"Subsample", "Native", "Supersample"},
                                "Render resolution scaling");

  // Connect all
  for (QComboBox *c :
       {m_shadowsCombo, m_reflectionsCombo, m_texturesCombo, m_shadersCombo,
        m_environmentCombo, m_animationCombo, m_lodDistanceCombo,
        m_antiAliasingCombo, m_samplingCombo}) {
    connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &GraphicsTabWidget::onSettingChanged);
  }

  row = 0;
  // Left pair: columns 0,1 | Spacer: column 2 | Right pair: columns 3,4
  qualityGrid->addWidget(new QLabel("Shadows"), row, 0);
  qualityGrid->addWidget(m_shadowsCombo, row, 1);
  qualityGrid->addWidget(new QLabel("Reflections"), row, 3);
  qualityGrid->addWidget(m_reflectionsCombo, row, 4);
  row++;
  qualityGrid->addWidget(new QLabel("Textures"), row, 0);
  qualityGrid->addWidget(m_texturesCombo, row, 1);
  qualityGrid->addWidget(new QLabel("Shaders"), row, 3);
  qualityGrid->addWidget(m_shadersCombo, row, 4);
  row++;
  qualityGrid->addWidget(new QLabel("Environment"), row, 0);
  qualityGrid->addWidget(m_environmentCombo, row, 1);
  qualityGrid->addWidget(new QLabel("Animation"), row, 3);
  qualityGrid->addWidget(m_animationCombo, row, 4);
  row++;
  qualityGrid->addWidget(new QLabel("LOD Distance"), row, 0);
  qualityGrid->addWidget(m_lodDistanceCombo, row, 1);
  qualityGrid->addWidget(new QLabel("Antialiasing"), row, 3);
  qualityGrid->addWidget(m_antiAliasingCombo, row, 4);
  row++;
  qualityGrid->addWidget(new QLabel("Sampling"), row, 0);
  qualityGrid->addWidget(m_samplingCombo, row, 1);

  layout->addWidget(qualityGroup);

  // === Character Settings ===
  auto *charGroup = new QGroupBox("Characters");
  auto *charGrid = new QGridLayout(charGroup);
  charGrid->setHorizontalSpacing(8);
  charGrid->setColumnMinimumWidth(0, 85);  // Left label
  charGrid->setColumnMinimumWidth(1, 180); // Left combo
  charGrid->setColumnMinimumWidth(2, 40);  // Spacer
  charGrid->setColumnMinimumWidth(3, 85);  // Right label
  charGrid->setColumnStretch(1, 1);
  charGrid->setColumnStretch(4, 1);

  m_charModelLimitCombo =
      createCombo({"Lowest", "Low", "Medium", "High", "Highest"},
                  "Max visible characters - Big impact in crowds");
  m_charModelQualityCombo = createCombo(
      {"Lowest", "Low", "Medium", "High", "Highest"}, "Character detail level");
  m_highResCharToggle = new LabeledToggle("Hi-Res Textures");
  m_highResCharToggle->setToolTip("High resolution character textures");
  m_effectLodToggle = new LabeledToggle("Effect LOD");
  m_effectLodToggle->setToolTip("Reduce particle effect detail");

  connect(m_charModelLimitCombo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &GraphicsTabWidget::onSettingChanged);
  connect(m_charModelQualityCombo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &GraphicsTabWidget::onSettingChanged);
  connect(m_highResCharToggle, &LabeledToggle::toggled, this,
          &GraphicsTabWidget::onSettingChanged);
  connect(m_effectLodToggle, &LabeledToggle::toggled, this,
          &GraphicsTabWidget::onSettingChanged);

  // Left pair: columns 0,1 | Spacer: column 2 | Right pair: columns 3,4
  charGrid->addWidget(new QLabel("Model Limit"), 0, 0);
  charGrid->addWidget(m_charModelLimitCombo, 0, 1);
  charGrid->addWidget(new QLabel("Model Quality"), 0, 3);
  charGrid->addWidget(m_charModelQualityCombo, 0, 4);
  charGrid->addWidget(m_highResCharToggle, 1, 0, 1, 2);
  charGrid->addWidget(m_effectLodToggle, 1, 3, 1, 2);

  layout->addWidget(charGroup);

  // === Advanced Settings ===
  auto *advGroup = new QGroupBox("Advanced");
  auto *advGrid = new QGridLayout(advGroup);
  advGrid->setHorizontalSpacing(8);
  advGrid->setColumnMinimumWidth(0, 85);  // Left label
  advGrid->setColumnMinimumWidth(1, 180); // Left combo
  advGrid->setColumnMinimumWidth(2, 40);  // Spacer
  advGrid->setColumnMinimumWidth(3, 85);  // Right label
  advGrid->setColumnStretch(1, 1);
  advGrid->setColumnStretch(4, 1);

  m_bestTexFilterToggle = new LabeledToggle("Best Tex Filtering");
  m_bestTexFilterToggle->setToolTip("Anisotropic texture filtering");
  m_screenShadowsToggle = new LabeledToggle("Screen Space Shadows");
  m_screenShadowsToggle->setToolTip(
      "Additional shadow detail (requires High/Ultra shadows)");
  m_postProcCombo = createCombo({"Off", "Low", "Medium", "High", "Custom"},
                                "Post-processing effects (bloom, etc)");
  m_depthBlurToggle = new LabeledToggle("Depth Blur");
  m_depthBlurToggle->setToolTip("Blur distant objects for stylized look");

  connect(m_bestTexFilterToggle, &LabeledToggle::toggled, this,
          &GraphicsTabWidget::onSettingChanged);
  connect(m_screenShadowsToggle, &LabeledToggle::toggled, this,
          &GraphicsTabWidget::onSettingChanged);
  connect(m_postProcCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GraphicsTabWidget::onSettingChanged);
  connect(m_depthBlurToggle, &LabeledToggle::toggled, this,
          &GraphicsTabWidget::onSettingChanged);

  // Left pair: columns 0,1 | Spacer: column 2 | Right pair: columns 3,4
  advGrid->addWidget(m_bestTexFilterToggle, 0, 0, 1, 2);
  advGrid->addWidget(m_screenShadowsToggle, 0, 3, 1, 2);
  advGrid->addWidget(new QLabel("Postprocessing"), 1, 0);
  advGrid->addWidget(m_postProcCombo, 1, 1);
  advGrid->addWidget(m_depthBlurToggle, 1, 3, 1, 2);

  layout->addWidget(advGroup);

  // === Apply Toggle ===
  m_useCustomGfxToggle =
      new LabeledToggle("Use custom graphics (apply on launch)");
  m_useCustomGfxToggle->setToolTip(
      "When enabled, these custom settings will be applied when GW2 launches.\n"
      "Settings are always saved with your profile.");
  connect(m_useCustomGfxToggle, &LabeledToggle::toggled, this,
          &GraphicsTabWidget::modified);
  layout->addWidget(m_useCustomGfxToggle);

  layout->addStretch();
}

void GraphicsTabWidget::onPresetChanged(int index) {
  if (m_loading)
    return;

  QString presetName = m_presetCombo->currentText();
  if (presetName == "Custom")
    return; // Don't auto-fill for custom

  GfxSettings preset = GFXManager::getPreset(presetName);
  m_loading = true;
  loadSettingsToUI(preset);
  m_loading = false;

  // Auto-enable toggle when user selects a preset
  if (!m_useCustomGfxToggle->isChecked()) {
    m_useCustomGfxToggle->setChecked(true);
  }

  emit modified();
}

void GraphicsTabWidget::onSettingChanged() {
  if (m_loading)
    return;

  // Any manual change switches to Custom preset
  m_presetCombo->blockSignals(true);
  m_presetCombo->setCurrentText("Custom");
  m_presetCombo->blockSignals(false);

  // Auto-enable the apply toggle when user makes changes
  // (they clearly want custom settings if they're editing them)
  if (!m_useCustomGfxToggle->isChecked()) {
    m_useCustomGfxToggle->setChecked(true);
  }

  emit modified();
}

void GraphicsTabWidget::loadSettingsToUI(const GfxSettings &s) {
  // Display
  // Find matching resolution
  QString resStr = QString("%1x%2").arg(s.width).arg(s.height);
  for (int i = 0; i < m_resolutionCombo->count(); i++) {
    if (m_resolutionCombo->itemText(i).startsWith(resStr)) {
      m_resolutionCombo->setCurrentIndex(i);
      break;
    }
  }

  // Screen mode
  if (s.screenMode == "windowed")
    m_screenModeCombo->setCurrentIndex(0);
  else if (s.screenMode == "fullscreen")
    m_screenModeCombo->setCurrentIndex(1);
  else
    m_screenModeCombo->setCurrentIndex(2);

  // Frame limit
  if (s.frameLimit == "unlimited")
    m_frameLimitCombo->setCurrentIndex(0);
  else if (s.frameLimit == "144")
    m_frameLimitCombo->setCurrentIndex(1);
  else if (s.frameLimit == "120")
    m_frameLimitCombo->setCurrentIndex(2);
  else if (s.frameLimit == "60")
    m_frameLimitCombo->setCurrentIndex(3);
  else if (s.frameLimit == "30")
    m_frameLimitCombo->setCurrentIndex(4);

  m_vsyncToggle->setChecked(s.verticalSync);
  m_dpiScalingToggle->setChecked(s.dpiScaling);
  m_gammaSlider->setValue(static_cast<int>(s.gamma * 100));

  // Shadows: off, low, medium, high, ultra
  QStringList shadowOpts = {"off", "low", "medium", "high", "ultra"};
  int shadowIdx = shadowOpts.indexOf(s.shadows.toLower());
  if (shadowIdx >= 0)
    m_shadowsCombo->setCurrentIndex(shadowIdx);

  // Reflections: none, terrain, all
  if (s.reflections == "none")
    m_reflectionsCombo->setCurrentIndex(0);
  else if (s.reflections == "terrain")
    m_reflectionsCombo->setCurrentIndex(1);
  else
    m_reflectionsCombo->setCurrentIndex(2);

  // Simple 3-level settings
  auto setThreeLevel = [](QComboBox *c, const QString &val) {
    if (val == "low")
      c->setCurrentIndex(0);
    else if (val == "medium")
      c->setCurrentIndex(1);
    else
      c->setCurrentIndex(2);
  };
  setThreeLevel(m_texturesCombo, s.textureDetail);
  setThreeLevel(m_shadersCombo, s.shaders);
  setThreeLevel(m_environmentCombo, s.environment);
  setThreeLevel(m_animationCombo, s.animation);

  // LOD: low, medium, high, ultra
  QStringList lodOpts = {"low", "medium", "high", "ultra"};
  int lodIdx = lodOpts.indexOf(s.lodDistance.toLower());
  if (lodIdx >= 0)
    m_lodDistanceCombo->setCurrentIndex(lodIdx);

  // AA: none, fxaa, smaa_low, smaa_high
  if (s.antiAliasing == "none")
    m_antiAliasingCombo->setCurrentIndex(0);
  else if (s.antiAliasing == "fxaa")
    m_antiAliasingCombo->setCurrentIndex(1);
  else if (s.antiAliasing == "smaa_low")
    m_antiAliasingCombo->setCurrentIndex(2);
  else
    m_antiAliasingCombo->setCurrentIndex(3);

  // Sampling: subsample, native, supersample
  if (s.sampling == "subsample")
    m_samplingCombo->setCurrentIndex(0);
  else if (s.sampling == "native")
    m_samplingCombo->setCurrentIndex(1);
  else
    m_samplingCombo->setCurrentIndex(2);

  // Characters
  QStringList charOpts = {"lowest", "low", "medium", "high", "highest"};
  int limitIdx = charOpts.indexOf(s.charModelLimit.toLower());
  if (limitIdx >= 0)
    m_charModelLimitCombo->setCurrentIndex(limitIdx);
  int qualIdx = charOpts.indexOf(s.charModelQuality.toLower());
  if (qualIdx >= 0)
    m_charModelQualityCombo->setCurrentIndex(qualIdx);

  m_highResCharToggle->setChecked(s.highResCharacter);
  m_effectLodToggle->setChecked(s.effectLod);

  // Advanced
  m_bestTexFilterToggle->setChecked(s.bestTextureFiltering);
  m_screenShadowsToggle->setChecked(s.screenspaceShadows);

  QStringList postOpts = {"off", "low", "medium", "high", "custom"};
  int postIdx = postOpts.indexOf(s.postProc.toLower());
  if (postIdx >= 0)
    m_postProcCombo->setCurrentIndex(postIdx);

  m_depthBlurToggle->setChecked(s.depthBlur);
}

GfxSettings GraphicsTabWidget::gatherSettingsFromUI() {
  GfxSettings s;

  // Resolution
  QVariant data = m_resolutionCombo->currentData();
  if (data.isValid()) {
    QSize size = data.toSize();
    s.width = size.width();
    s.height = size.height();
  }
  // Extract refresh rate from text
  QString resText = m_resolutionCombo->currentText();
  QRegularExpression re("@ (\\d+)Hz");
  auto match = re.match(resText);
  if (match.hasMatch()) {
    s.refreshRate = match.captured(1).toInt();
  }

  // Screen mode
  int modeIdx = m_screenModeCombo->currentIndex();
  s.screenMode = (modeIdx == 0)   ? "windowed"
                 : (modeIdx == 1) ? "fullscreen"
                                  : "windowed_fullscreen";

  // Frame limit
  QStringList frameLimits = {"unlimited", "144", "120", "60", "30"};
  s.frameLimit =
      frameLimits.value(m_frameLimitCombo->currentIndex(), "unlimited");

  s.verticalSync = m_vsyncToggle->isChecked();
  s.dpiScaling = m_dpiScalingToggle->isChecked();
  s.gamma = m_gammaSlider->value() / 100.0;

  // Quality
  QStringList shadowOpts = {"off", "low", "medium", "high", "ultra"};
  s.shadows = shadowOpts.value(m_shadowsCombo->currentIndex(), "high");

  QStringList reflOpts = {"none", "terrain", "all"};
  s.reflections = reflOpts.value(m_reflectionsCombo->currentIndex(), "all");

  QStringList threeOpts = {"low", "medium", "high"};
  s.textureDetail = threeOpts.value(m_texturesCombo->currentIndex(), "high");
  s.shaders = threeOpts.value(m_shadersCombo->currentIndex(), "high");
  s.environment = threeOpts.value(m_environmentCombo->currentIndex(), "high");
  s.animation = threeOpts.value(m_animationCombo->currentIndex(), "high");

  QStringList lodOpts = {"low", "medium", "high", "ultra"};
  s.lodDistance = lodOpts.value(m_lodDistanceCombo->currentIndex(), "high");

  QStringList aaOpts = {"none", "fxaa", "smaa_low", "smaa_high"};
  s.antiAliasing =
      aaOpts.value(m_antiAliasingCombo->currentIndex(), "smaa_high");

  QStringList sampOpts = {"subsample", "native", "supersample"};
  s.sampling = sampOpts.value(m_samplingCombo->currentIndex(), "native");

  // Characters
  QStringList charOpts = {"lowest", "low", "medium", "high", "highest"};
  s.charModelLimit =
      charOpts.value(m_charModelLimitCombo->currentIndex(), "high");
  s.charModelQuality =
      charOpts.value(m_charModelQualityCombo->currentIndex(), "high");
  s.highResCharacter = m_highResCharToggle->isChecked();
  s.effectLod = m_effectLodToggle->isChecked();

  // Advanced
  s.bestTextureFiltering = m_bestTexFilterToggle->isChecked();
  s.screenspaceShadows = m_screenShadowsToggle->isChecked();

  QStringList postOpts = {"off", "low", "medium", "high", "custom"};
  s.postProc = postOpts.value(m_postProcCombo->currentIndex(), "high");
  s.depthBlur = m_depthBlurToggle->isChecked();

  s.presetName = m_presetCombo->currentText();

  return s;
}

void GraphicsTabWidget::checkWindowTabSync() {
  // Check if auto-position is enabled and user selected fullscreen
  if (m_profile.useCustomWindow && m_screenModeCombo->currentIndex() == 1) {
    m_syncWarningLabel->setText(
        "Fullscreen mode ignores window positioning. "
        "Consider using 'Windowed Fullscreen' instead.");
    m_syncWarningLabel->show();
  } else {
    m_syncWarningLabel->hide();
  }
}

void GraphicsTabWidget::onSaveClicked() {
  if (m_profile.id.isEmpty()) {
    m_statusLabel->setText("Cannot save - profile has no ID.");
    UIHelpers::applyErrorColorRole(m_statusLabel);
    return;
  }

  GfxSettings settings = gatherSettingsFromUI();
  GFXManager gfxMgr(m_dataService ? m_dataService->savedGfxDir() : QString());

  QString saveDir = gfxMgr.savedGfxPath();
  QString destPath = saveDir + m_profile.id + "_GFX.xml";

  if (gfxMgr.writeGfxFile(destPath, settings)) {
    m_profile.gfxSettingsPath = destPath;
    m_statusLabel->setText("Saved: " + QFileInfo(destPath).fileName());
    UIHelpers::applySuccessColorRole(m_statusLabel);
    emit modified();
  } else {
    m_statusLabel->setText("Failed to save settings");
    UIHelpers::applyErrorColorRole(m_statusLabel);
  }
}

void GraphicsTabWidget::onExportClicked() {
  QString path = QFileDialog::getSaveFileName(
      this, "Export GFX Settings", "GFXSettings.xml", "XML Files (*.xml)");
  if (path.isEmpty())
    return;

  GfxSettings settings = gatherSettingsFromUI();
  GFXManager gfxMgr(m_dataService ? m_dataService->savedGfxDir() : QString());

  if (gfxMgr.writeGfxFile(path, settings)) {
    m_statusLabel->setText("Exported to: " + QFileInfo(path).fileName());
    UIHelpers::applySuccessColorRole(m_statusLabel);
  } else {
    m_statusLabel->setText("Export failed");
    UIHelpers::applyErrorColorRole(m_statusLabel);
  }
}

void GraphicsTabWidget::onImportClicked() {
  QString path = QFileDialog::getOpenFileName(this, "Import GFX Settings",
                                              QString(), "XML Files (*.xml)");
  if (path.isEmpty())
    return;

  GFXManager gfxMgr(m_dataService ? m_dataService->savedGfxDir() : QString());
  GfxSettings settings = gfxMgr.parseGfxFile(path);

  m_loading = true;
  loadSettingsToUI(settings);
  m_presetCombo->setCurrentText("Custom");
  m_loading = false;

  m_statusLabel->setText("Imported from: " + QFileInfo(path).fileName());
  UIHelpers::applySuccessColorRole(m_statusLabel);
  emit modified();
}

void GraphicsTabWidget::onLoadFromGW2Clicked() {
  GFXManager gfxMgr(m_dataService ? m_dataService->savedGfxDir() : QString());
  QString gw2Path = gfxMgr.gw2GfxPath();

  if (!QFile::exists(gw2Path)) {
    m_statusLabel->setText("No GW2 GFXSettings found. Launch GW2 first.");
    UIHelpers::applyErrorColorRole(m_statusLabel);
    return;
  }

  GfxSettings settings = gfxMgr.parseGfxFile(gw2Path);

  m_loading = true;
  loadSettingsToUI(settings);
  m_presetCombo->setCurrentText("Custom");
  m_loading = false;

  m_statusLabel->setText("Loaded current GW2 settings");
  UIHelpers::applySuccessColorRole(m_statusLabel);
  emit modified();
}

void GraphicsTabWidget::load() {
  m_loading = true;

  // Debug logging for load
  QFile logFile(
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/gfx_debug.log");
  if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    return;
  QTextStream log(&logFile);
  log << "\n=== GraphicsTabWidget::load() "
      << QDateTime::currentDateTime().toString() << " ===\n";
  log << "  m_profile.gfxSettingsPath: " << m_profile.gfxSettingsPath << "\n";
  log << "  m_profile.useCustomGfx: " << m_profile.useCustomGfx << "\n";
  log << "  File exists: " << QFile::exists(m_profile.gfxSettingsPath) << "\n";

  // Load from saved file if exists
  if (!m_profile.gfxSettingsPath.isEmpty() &&
      QFile::exists(m_profile.gfxSettingsPath)) {
    // Close log file before parseGfxFile (it logs to same file)
    logFile.close();

    GFXManager gfxMgr(m_dataService ? m_dataService->savedGfxDir() : QString());
    GfxSettings settings = gfxMgr.parseGfxFile(m_profile.gfxSettingsPath);

    // Reopen log file
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append |
                      QIODevice::Text))
      return;
    log.setDevice(&logFile);

    log << "  Loaded preset from file: " << settings.presetName << "\n";
    log << "  Loaded shadows: " << settings.shadows << "\n";
    loadSettingsToUI(settings);
    // Restore saved preset name
    m_presetCombo->setCurrentText(settings.presetName);
    m_statusLabel->setText("Loaded: " +
                           QFileInfo(m_profile.gfxSettingsPath).fileName());
    UIHelpers::applyHintRole(m_statusLabel);
  } else {
    log << "  Loading default High preset (no saved file)\n";
    // Load default High preset
    GfxSettings defaults = GFXManager::getPreset("High");
    loadSettingsToUI(defaults);
    m_presetCombo->setCurrentText("High");
  }
  logFile.close();

  m_useCustomGfxToggle->setChecked(m_profile.useCustomGfx);

  // Check window sync on load
  checkWindowTabSync();

  m_loading = false;
}

void GraphicsTabWidget::save() {
  m_profile.useCustomGfx = m_useCustomGfxToggle->isChecked();

  // Direct file logging for debugging
  QFile logFile(
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/gfx_debug.log");
  if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    return;
  QTextStream log(&logFile);
  log << "\n=== GraphicsTabWidget::save() "
      << QDateTime::currentDateTime().toString() << " ===\n";
  log << "  useCustomGfx: " << m_profile.useCustomGfx << "\n";
  log << "  nickname: " << m_profile.nickname << "\n";
  log << "  existing gfxSettingsPath: " << m_profile.gfxSettingsPath << "\n";
  log << "  presetCombo currentText: " << m_presetCombo->currentText() << "\n";

  // ALWAYS save GFX settings when profile is saved (if nickname is valid)
  // The toggle only controls whether settings are APPLIED on launch
  if (!m_profile.id.isEmpty()) {
    GfxSettings settings = gatherSettingsFromUI();
    GFXManager gfxMgr(m_dataService ? m_dataService->savedGfxDir() : QString());

    QString saveDir = gfxMgr.savedGfxPath();
    QString destPath = saveDir + m_profile.id + "_GFX.xml";

    log << "  saveDir: " << saveDir << "\n";
    log << "  destPath: " << destPath << "\n";
    log << "  settings.presetName: " << settings.presetName << "\n";

    if (gfxMgr.writeGfxFile(destPath, settings)) {
      m_profile.gfxSettingsPath = destPath;
      log << "  SUCCESS: gfxSettingsPath set to: " << m_profile.gfxSettingsPath
          << "\n";

      // Only show warning if toggle is off (save indicator handles success)
      if (!m_profile.useCustomGfx) {
        m_statusLabel->setText("Settings saved (won't apply - toggle is off)");
        UIHelpers::applyWarningColorRole(m_statusLabel);
      }
    } else {
      log << "  FAIL: Could not write GFX file!\n";
      m_statusLabel->setText("Failed to save settings");
      UIHelpers::applyErrorColorRole(m_statusLabel);
    }
  } else {
    log << "  SKIPPED: nickname is empty\n";
  }
  logFile.close();
}
