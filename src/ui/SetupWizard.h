#pragma once

#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "ToggleSwitch.h"
#include "core/GW2Detector.h"
#include "core/SettingsManager.h"

/**
 * @brief First-run setup wizard - Custom frameless dialog following dev
 * standards
 *
 * Uses UIHelpers for styling, SVG icons only, gold border pattern.
 */
class SetupWizard : public QDialog {
  Q_OBJECT

public:
  explicit SetupWizard(SettingsManager *settings, QWidget *parent = nullptr);

  QString gw2Path() const { return m_gw2Path; }
  QStringList enabledFeatures() const { return m_enabledFeatures; }

protected:
  // Drag handling (same as MainWindow)
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
  void onNextClicked();
  void onBackClicked();
  void onFinishClicked();

private:
  void setupUI();
  void updateNavigation();
  QWidget *createWelcomePage();
  QWidget *createGW2PathPage();
  QWidget *createFeaturesPage();
  QWidget *createCompletePage();

  SettingsManager *m_settings;
  QString m_gw2Path;
  QStringList m_enabledFeatures;

  // UI components
  QStackedWidget *m_pages;
  QPushButton *m_backBtn;
  QPushButton *m_nextBtn;
  QPushButton *m_finishBtn;
  QLabel *m_titleLabel;

  // Page-specific components
  QLineEdit *m_pathEdit;
  QLabel *m_pathStatusLabel;
  GW2Detector m_detector;

  // Drag handling
  QPoint m_dragPos;
  bool m_dragging = false;
};
