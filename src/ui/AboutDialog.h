#pragma once

#include "UIHelpers.h"
#include "core/AppConfig.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

/**
 * @brief About dialog with version, credits, and licenses
 */
class AboutDialog : public QDialog {
  Q_OBJECT

public:
  explicit AboutDialog(QWidget *parent = nullptr);

private:
  void setupUI();
  QString getCreditsText();
  QString getLicenseText();
};

// Implementation
inline AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
  // Frameless window with rounded corners
  setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setupUI();
}

inline void AboutDialog::setupUI() {
  setMinimumSize(500, 450);

  // Outer layout for transparent dialog
  QVBoxLayout *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  // Background container — themed via role
  QWidget *bgContainer = new QWidget();
  UIHelpers::applyWindowBackgroundRole(bgContainer);
  outerLayout->addWidget(bgContainer);

  QVBoxLayout *mainLayout = new QVBoxLayout(bgContainer);
  mainLayout->setContentsMargins(16, 12, 16, 16);
  mainLayout->setSpacing(16);

  // Custom title bar — themed via createTitleBar helper
  auto *titleBar =
      UIHelpers::createTitleBar(bgContainer, "About GW2 AIO Manager",
                                ":/icons/app-icon.svg", [this]() { accept(); });
  mainLayout->addWidget(titleBar);

  // Header
  QHBoxLayout *headerLayout = new QHBoxLayout();

  // Use app-icon.svg instead of emoji
  QLabel *iconLabel = new QLabel();
  QIcon appIcon(":/icons/app-icon.svg");
  iconLabel->setPixmap(appIcon.pixmap(48, 48));
  iconLabel->setStyleSheet("background: transparent; border: none;");
  headerLayout->addWidget(iconLabel);

  QVBoxLayout *titleLayout = new QVBoxLayout();
  QLabel *titleLabel = new QLabel("GW2 AIO Manager");
  UIHelpers::applyRole(titleLabel, "goldTitle");
  titleLayout->addWidget(titleLabel);

  QLabel *versionLabel = new QLabel(QString("Version %1").arg(APP_VERSION));
  UIHelpers::applyRole(versionLabel, "secondary");
  titleLayout->addWidget(versionLabel);

  headerLayout->addLayout(titleLayout);
  headerLayout->addStretch();

  mainLayout->addLayout(headerLayout);

  // Description
  QLabel *descLabel = new QLabel(
      "All-in-one overlay manager for Guild Wars 2.\n"
      "Combines the best features of GW2Radial, TacO, ArcDPS, and Blish-HUD.");
  descLabel->setWordWrap(true);
  mainLayout->addWidget(descLabel);

  // Tabs
  QTabWidget *tabs = new QTabWidget();

  // About tab - plain HTML without emojis
  QTextBrowser *aboutBrowser = new QTextBrowser();
  aboutBrowser->setOpenExternalLinks(true);
  aboutBrowser->setHtml(R"(
        <h3>Features</h3>
        <ul>
            <li><b>Radial Menus</b> - Quick access to mounts, novelties, markers</li>
            <li><b>DPS Tracker</b> - Combat statistics with ArcDPS integration</li>
            <li><b>Marker System</b> - TacO-compatible waypoints and trails</li>
            <li><b>Module Support</b> - Run Blish-HUD modules natively</li>
            <li><b>Multi-Account</b> - Launch multiple GW2 instances</li>
            <li><b>Auto-Update</b> - Automatic addon and self updates</li>
        </ul>
        <h3>Links</h3>
        <p>
            <a href="https://github.com/Foboryus/GW2-AIO">GitHub Repository</a><br>
            <a href="https://github.com/Foboryus/GW2-AIO/issues">Report Issues</a><br>
            <a href="https://github.com/Foboryus/GW2-AIO/wiki">Documentation</a>
        </p>
    )");
  tabs->addTab(aboutBrowser, "About");

  // Credits tab
  QTextBrowser *creditsBrowser = new QTextBrowser();
  creditsBrowser->setHtml(getCreditsText());
  tabs->addTab(creditsBrowser, "Credits");

  // License tab
  QTextBrowser *licenseBrowser = new QTextBrowser();
  licenseBrowser->setPlainText(getLicenseText());
  tabs->addTab(licenseBrowser, "License");

  mainLayout->addWidget(tabs);
}

inline QString AboutDialog::getCreditsText() {
  return R"(
        <h3>Developed By</h3>
        <p><b>Foboryus</b></p>
        
        <h3>Built With</h3>
        <ul>
            <li><b>Qt 6</b> - Cross-platform framework</li>
            <li><b>OpenGL</b> - Graphics rendering</li>
            <li><b>QuaZip</b> - ZIP extraction (Sergei Tachenov, via vcpkg)</li>
        </ul>
        
        <h3>Inspired By</h3>
        <ul>
            <li><b>GW2Radial</b> by Friendly0Fire</li>
            <li><b>GW2TacO</b> by BoyC</li>
            <li><b>Blish-HUD</b> by Blish-HUD Team</li>
            <li><b>ArcDPS</b> by deltaconnected</li>
        </ul>
        
        <h3>Special Thanks</h3>
        <ul>
            <li>ArenaNet for Guild Wars 2</li>
            <li>The GW2 modding community</li>
            <li>All contributors and testers</li>
        </ul>
    )";
}

inline QString AboutDialog::getLicenseText() {
  return R"(MIT License

Copyright (c) 2024 GW2 AIO Manager Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.)";
}
