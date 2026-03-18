#include "WindowTabWidget.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ui/ProfileEditor.h" // For AccountProfile
#include "ui/ToggleSwitch.h"  // For LabeledToggle
#include "ui/UIHelpers.h"
#include "ui/WindowGridSelector.h"

WindowTabWidget::WindowTabWidget(AccountProfile &profile, QWidget *parent)
    : QWidget(parent), m_profile(profile) {
  setupUI();
}

void WindowTabWidget::setupUI() {
  auto *layout = new QVBoxLayout(this);

  auto *info =
      new QLabel("Multi-Boxing Window Placement\n\n"
                 "When running multiple GW2 instances, each window can be\n"
                 "automatically positioned after launch.\n\n"
                 "This is useful for:\n"
                 "• Setting up a dual-monitor layout\n"
                 "• Side-by-side windows for alt-tabbing\n"
                 "• Cascade arrangement");
  info->setWordWrap(true);
  UIHelpers::applyHintRole(info);
  info->setStyleSheet(
      QString("padding: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.paddingNormal));
  layout->addWidget(info);

  m_customWindowToggle =
      new LabeledToggle("Automatically position window after launch");
  layout->addWidget(m_customWindowToggle);
  connect(m_customWindowToggle, &LabeledToggle::toggled, this,
          &WindowTabWidget::modified);

  m_posGroup = new QGroupBox("Window Position & Size");
  auto *posLayout = new QFormLayout(m_posGroup);

  auto *posXY = new QHBoxLayout();
  m_windowX = new QSpinBox();
  m_windowX->setRange(-5000, 5000);
  m_windowX->setPrefix("Horizontal: ");
  posXY->addWidget(m_windowX);
  m_windowY = new QSpinBox();
  m_windowY->setRange(-5000, 5000);
  m_windowY->setPrefix("Vertical: ");
  posXY->addWidget(m_windowY);
  posLayout->addRow("Position:", posXY);

  auto *sizeWH = new QHBoxLayout();
  m_windowWidth = new QSpinBox();
  m_windowWidth->setRange(640, 7680);
  m_windowWidth->setValue(1920);
  m_windowWidth->setPrefix("Width: ");
  sizeWH->addWidget(m_windowWidth);
  m_windowHeight = new QSpinBox();
  m_windowHeight->setRange(480, 4320);
  m_windowHeight->setValue(1080);
  m_windowHeight->setPrefix("Height: ");
  sizeWH->addWidget(m_windowHeight);
  posLayout->addRow("Size:", sizeWH);

  connect(m_customWindowToggle, &LabeledToggle::toggled, m_posGroup,
          &QWidget::setEnabled);
  m_posGroup->setEnabled(false);

  // Emit modified when position/size values change
  connect(m_windowX, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &WindowTabWidget::modified);
  connect(m_windowY, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &WindowTabWidget::modified);
  connect(m_windowWidth, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &WindowTabWidget::modified);
  connect(m_windowHeight, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &WindowTabWidget::modified);

  // Monitor Preview Widget
  m_previewGroup = new QGroupBox("Window Preview");
  auto *previewLayout = new QVBoxLayout(m_previewGroup);

  auto *selectMonitorLabel =
      new QLabel("Click a monitor to set window position:");
  selectMonitorLabel->setStyleSheet(
      QString("font-weight: bold; margin-top: %1px;")
          .arg(ThemeManager::instance().activeTheme().layout.paddingNormal));
  previewLayout->addWidget(selectMonitorLabel);

  auto *monitorBtnLayout = new QHBoxLayout();
  QList<QScreen *> screens = QGuiApplication::screens();

  for (int i = 0; i < screens.size(); i++) {
    QScreen *s = screens[i];
    QRect geo = s->geometry();

    auto *monBtn = new QPushButton();
    monBtn->setText(QString("Monitor %1\n%2 x %3")
                        .arg(i + 1)
                        .arg(geo.width())
                        .arg(geo.height()));

    // Make button proportional to monitor resolution (scaled down)
    int btnWidth = geo.width() / 15;
    int btnHeight = geo.height() / 15;
    monBtn->setMinimumSize(btnWidth, btnHeight);
    // NOTE: Monitor buttons intentionally use minimal styling, not UIHelpers.
    // They resize proportionally to monitor resolution.
    // May be replaced with custom widgets later.
    monBtn->setStyleSheet(
        QString("font-size: %1px; padding: %2px;")
            .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint)
            .arg(ThemeManager::instance().activeTheme().layout.paddingNormal));

    // Connect to open grid selector for this monitor
    connect(monBtn, &QPushButton::clicked, [this, i]() {
      WindowGridSelector dialog(i, this);
      // Show current saved position as visual aid
      QRect currentRect(m_windowX->value(), m_windowY->value(),
                        m_windowWidth->value(), m_windowHeight->value());
      dialog.setCurrentPosition(currentRect);

      connect(&dialog, &WindowGridSelector::positionSelected,
              [this](QRect rect) {
                m_windowX->setValue(rect.x());
                m_windowY->setValue(rect.y());
                m_windowWidth->setValue(rect.width());
                m_windowHeight->setValue(rect.height());
                emit modified();
              });
      dialog.exec();
    });
    monitorBtnLayout->addWidget(monBtn);
  }
  monitorBtnLayout->addStretch();
  previewLayout->addLayout(monitorBtnLayout);

  connect(m_customWindowToggle, &LabeledToggle::toggled, m_previewGroup,
          &QWidget::setEnabled);
  m_previewGroup->setEnabled(false);

  // Add in order: Preview first, then Position/Size
  layout->addWidget(m_previewGroup);
  layout->addWidget(m_posGroup);

  layout->addStretch();
}

void WindowTabWidget::load() {
  m_customWindowToggle->blockSignals(true);
  m_customWindowToggle->setChecked(m_profile.useCustomWindow);
  m_customWindowToggle->blockSignals(false);

  m_windowX->blockSignals(true);
  m_windowY->blockSignals(true);
  m_windowWidth->blockSignals(true);
  m_windowHeight->blockSignals(true);

  m_windowX->setValue(m_profile.windowX);
  m_windowY->setValue(m_profile.windowY);
  m_windowWidth->setValue(m_profile.windowWidth > 0 ? m_profile.windowWidth
                                                    : 1920);
  m_windowHeight->setValue(m_profile.windowHeight > 0 ? m_profile.windowHeight
                                                      : 1080);

  m_windowX->blockSignals(false);
  m_windowY->blockSignals(false);
  m_windowWidth->blockSignals(false);
  m_windowHeight->blockSignals(false);

  // Manually update group enabled states (signals were blocked)
  bool enabled = m_profile.useCustomWindow;
  m_posGroup->setEnabled(enabled);
  m_previewGroup->setEnabled(enabled);
}

void WindowTabWidget::save() {
  m_profile.useCustomWindow = m_customWindowToggle->isChecked();
  m_profile.windowX = m_windowX->value();
  m_profile.windowY = m_windowY->value();
  m_profile.windowWidth = m_windowWidth->value();
  m_profile.windowHeight = m_windowHeight->value();
}
