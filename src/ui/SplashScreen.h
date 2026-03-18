#pragma once

#include "core/AppConfig.h"
#include "core/ThemeManager.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSplashScreen>
#include <QSvgRenderer>
#include <QTimer>

/**
 * @brief Splash screen shown during application startup
 *
 * Transparent background with AIO gear logo overlapping top-left corner.
 * Styled to match GW2's aesthetic.
 */
class SplashScreen : public QSplashScreen {
  Q_OBJECT

public:
  explicit SplashScreen();

  void setStatus(const QString &message);
  void setProgress(int percent);

private:
  void createPixmap();

  int m_progress = 0;
  QString m_status;
};

// Implementation
inline SplashScreen::SplashScreen() : QSplashScreen() {
  createPixmap();
  setWindowFlags(Qt::SplashScreen | Qt::FramelessWindowHint |
                 Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);
}

inline void SplashScreen::createPixmap() {
  const auto &theme = ThemeManager::instance().activeTheme();
  const auto &c = theme.colors;
  const auto &w = theme.widgets;

  // Canvas size — extra space top-left for the logo overhang
  const int canvasW = 520;
  const int canvasH = 320;
  const int logoSize = 72;
  const int logoOverhang = 20; // How much the logo sits outside the box

  // Content box (the dark panel)
  const int boxX = 40;
  const int boxY = 40;
  const int boxW = canvasW - 50;
  const int boxH = canvasH - 50;
  const int boxRadius = 12;

  QPixmap pixmap(canvasW, canvasH);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  // === Dark panel with subtle border ===
  QPainterPath boxPath;
  boxPath.addRoundedRect(boxX, boxY, boxW, boxH, boxRadius, boxRadius);

  // Panel fill — dark gradient from theme window bg
  QColor bgColor(c.windowBg);
  QLinearGradient panelGrad(boxX, boxY, boxX, boxY + boxH);
  panelGrad.setColorAt(0, QColor(bgColor.red() + 12, bgColor.green() + 12,
                                 bgColor.blue() + 12, 240));
  panelGrad.setColorAt(
      1, QColor(bgColor.red(), bgColor.green(), bgColor.blue(), 245));
  painter.fillPath(boxPath, panelGrad);

  // Panel border — subtle accent
  QColor accentColor(c.textAccent);
  painter.setPen(QPen(
      QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 120),
      1.5));
  painter.drawPath(boxPath);

  // === AIO gear logo — top-left, overlapping the box ===
  int logoX = boxX - logoOverhang + 5;
  int logoY = boxY - logoOverhang + 5;

  // Draw a dark circle behind the logo for contrast
  painter.setPen(Qt::NoPen);
  painter.setBrush(
      QColor(bgColor.red() + 2, bgColor.green() + 2, bgColor.blue() + 2, 230));
  painter.drawEllipse(logoX - 4, logoY - 4, logoSize + 8, logoSize + 8);

  // Accent ring around the logo
  painter.setPen(QPen(
      QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 180),
      2));
  painter.setBrush(Qt::NoBrush);
  painter.drawEllipse(logoX - 4, logoY - 4, logoSize + 8, logoSize + 8);

  // Render the SVG icon
  QSvgRenderer svgRenderer(QString(":/icons/app-icon.svg"));
  if (svgRenderer.isValid()) {
    svgRenderer.render(&painter, QRectF(logoX, logoY, logoSize, logoSize));
  }

  // === Title — right of the logo ===
  int textStartX = logoX + logoSize + 20;
  QFont titleFont("Segoe UI", 26, QFont::Bold);
  painter.setFont(titleFont);
  painter.setPen(accentColor);
  painter.drawText(QRect(textStartX, boxY + 8, boxW - textStartX + boxX, 42),
                   Qt::AlignLeft | Qt::AlignVCenter, "GW2 AIO");

  // === Subtitle ===
  QFont subFont("Segoe UI", 11);
  painter.setFont(subFont);
  painter.setPen(QColor(c.textSecondary));
  painter.drawText(QRect(textStartX, boxY + 50, boxW - textStartX + boxX, 22),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   "All-in-One Manager for Guild Wars 2");

  // === Separator line ===
  int sepY = boxY + 85;
  QLinearGradient sepGrad(boxX + 20, sepY, boxX + boxW - 20, sepY);
  sepGrad.setColorAt(
      0, QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 0));
  sepGrad.setColorAt(0.2, QColor(accentColor.red(), accentColor.green(),
                                 accentColor.blue(), 80));
  sepGrad.setColorAt(0.8, QColor(accentColor.red(), accentColor.green(),
                                 accentColor.blue(), 80));
  sepGrad.setColorAt(
      1, QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 0));
  painter.setPen(QPen(QBrush(sepGrad), 1));
  painter.drawLine(boxX + 20, sepY, boxX + boxW - 20, sepY);

  // === Status text — centered in middle area ===
  int statusY = boxY + 100;
  painter.setFont(QFont("Segoe UI", 10));
  painter.setPen(QColor(c.textHint));
  painter.drawText(QRect(boxX + 20, statusY, boxW - 40, 80), Qt::AlignCenter,
                   m_status);

  // === Progress bar — near bottom ===
  int barY = boxY + boxH - 35;
  int barX = boxX + 30;
  int barW = boxW - 60;
  int barH = 4;

  // Track
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(w.sliderGroove));
  painter.drawRoundedRect(barX, barY, barW, barH, 2, 2);

  // Fill
  int fillW = (barW * m_progress) / 100;
  if (fillW > 0) {
    QColor handleColor(w.sliderHandle);
    QLinearGradient barGrad(barX, barY, barX + fillW, barY);
    barGrad.setColorAt(0, handleColor);
    barGrad.setColorAt(1, handleColor.lighter(120));
    painter.setBrush(barGrad);
    painter.drawRoundedRect(barX, barY, fillW, barH, 2, 2);
  }

  // === Version — bottom right ===
  painter.setFont(QFont("Segoe UI", 9));
  painter.setPen(QColor(c.textHint).darker(150));
  painter.drawText(QRect(boxX, boxY + boxH - 22, boxW - 15, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString("v%1").arg(APP_VERSION));

  painter.end();

  setPixmap(pixmap);
}

inline void SplashScreen::setStatus(const QString &message) {
  m_status = message;
  createPixmap();
  QApplication::processEvents();
}

inline void SplashScreen::setProgress(int percent) {
  m_progress = qBound(0, percent, 100);
  createPixmap();
  QApplication::processEvents();
}
