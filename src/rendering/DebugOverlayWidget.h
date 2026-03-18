#pragma once

/**
 * @brief GW2 AIO Details Tracker — shows live MumbleLink data over the game
 *
 * A frameless, draggable, always-on-top Qt widget that displays:
 * - Character name and MumbleLink segment name
 * - Player position (X, Y, Z)
 * - Camera position and direction
 * - Map ID, FOV, process ID
 *
 * Dragging is constrained to the game window bounds.
 * Toggled on/off from the Marker Packs Settings tab.
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>

class MumbleLink;

class DebugOverlayWidget : public QWidget {
  Q_OBJECT

public:
  explicit DebugOverlayWidget(MumbleLink *mumble, QWidget *parent = nullptr);

  void attachToWindow(WId gw2Hwnd);

private:
  void updateDisplay();

  // Dragging support (constrained to game window)
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

  MumbleLink *m_mumble = nullptr;
  QLabel *m_label = nullptr;
  QTimer *m_timer;
  QPoint m_dragPos;
  bool m_dragging = false;
  HWND m_gw2Hwnd = nullptr;
};
