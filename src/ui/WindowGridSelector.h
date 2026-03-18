#pragma once

/**
 * @brief Visual Grid Window Position Selector
 *
 * Popup dialog for selecting window position and size using a visual grid.
 * Grid is proportional to the selected monitor's aspect ratio.
 * Automatically compensates for window frame (title bar, borders).
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h> // For GetSystemMetrics

#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <set>


// Forward declaration
class GridCell;

// Main dialog - takes monitor index, proportional grid
class WindowGridSelector : public QDialog {
  Q_OBJECT

public:
  // Constructor takes monitor index directly
  explicit WindowGridSelector(int monitorIndex, QWidget *parent = nullptr);
  QRect selectedRect() const { return m_selectedRect; }

  // Set current position to pre-select matching cells
  void setCurrentPosition(QRect currentRect);

signals:
  void positionSelected(QRect rect);

private slots:
  void rebuildGrid();
  void onCellClicked(int row, int col, Qt::KeyboardModifiers mods);
  // Preset selections
  void selectFullScreen();
  void selectLeftHalf();
  void selectRightHalf();
  void selectTopLeft();
  void selectTopRight();
  void selectBottomLeft();
  void selectBottomRight();
  void selectCenter();
  void onApply();

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  void setupUI();
  void selectCell(int row, int col);
  void selectRectangle(int r1, int c1, int r2, int c2);
  void clearSelection();
  void updatePreview();
  void preselectCurrentPosition();

  int m_monitorIndex = 0;
  QComboBox *m_gridCombo = nullptr;
  QWidget *m_gridContainer = nullptr;
  QGridLayout *m_gridLayout = nullptr;
  QLabel *m_previewLabel = nullptr;
  QLabel *m_monitorInfoLabel = nullptr;
  QList<GridCell *> m_cells;
  std::set<std::pair<int, int>> m_selectedCells;
  QRect m_selectedRect;
  QRect m_currentRect;
  int m_lastClickRow = 0, m_lastClickCol = 0;
  QPoint m_dragPos;
  bool m_dragging = false;
};
