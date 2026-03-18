#pragma once
/**
 * @file StyledTooltip.h
 * @brief Custom styled tooltip with true rounded corners and fade animation
 *
 * Features:
 * - True rounded corners (WA_TranslucentBackground)
 * - 200ms show delay
 * - Fade in/out animation
 * - Shows when window is unfocused
 * - Smart positioning (stays on screen)
 */

#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHelpEvent>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

/**
 * @brief Custom tooltip widget with AIO styling
 * Singleton pattern - use StyledTooltip::show() to display
 */
class StyledTooltip : public QWidget {
  Q_OBJECT

public:
  /**
   * @brief Show tooltip at global position
   * @param text Tooltip text
   * @param globalPos Position to show at (usually cursor position)
   */
  static void showTooltip(const QString &text, const QPoint &globalPos);

  /**
   * @brief Hide the tooltip immediately
   */
  static void hideTooltip();

private:
  explicit StyledTooltip();
  static StyledTooltip *instance();

  void showAt(const QString &text, const QPoint &globalPos);
  void fadeIn();
  void fadeOut();

  QLabel *m_label;
  QTimer *m_showTimer;
  QTimer *m_hideTimer;
  QGraphicsOpacityEffect *m_opacityEffect;
  QPropertyAnimation *m_fadeAnimation;
  QPoint m_pendingPos;
  QString m_pendingText;
  bool m_isVisible = false;

  static constexpr int SHOW_DELAY_MS = 200;
  static constexpr int FADE_DURATION_MS = 150;
};

/**
 * @brief Global event filter to intercept tooltip events
 * Install on QApplication to replace Qt's default tooltips
 */
class TooltipEventFilter : public QObject {
  Q_OBJECT

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;
};
