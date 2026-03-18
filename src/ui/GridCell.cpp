/**
 * @file GridCell.cpp
 * @brief Simple clickable grid cell with current position indicator
 *
 * Extracted from WindowGridSelector.cpp for cleaner organization.
 */

#include "GridCell.h"
#include "core/ThemeManager.h"

GridCell::GridCell(int row, int col, QWidget *parent)
    : QFrame(parent), m_row(row), m_col(col) {
  setFrameStyle(QFrame::Box);
  setMinimumSize(30, 25);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMouseTracking(true);
  updateStyle();
}

void GridCell::setSelected(bool selected) {
  m_selected = selected;
  updateStyle();
}

void GridCell::setCurrent(bool current) {
  m_current = current;
  updateStyle();
}

void GridCell::mousePressEvent(QMouseEvent *e) {
  emit clicked(m_row, m_col, e->modifiers());
}

void GridCell::enterEvent(QEnterEvent *e) {
  m_hovered = true;
  updateStyle();
  QFrame::enterEvent(e);
}

void GridCell::leaveEvent(QEvent *e) {
  m_hovered = false;
  updateStyle();
  QFrame::leaveEvent(e);
}

void GridCell::changeEvent(QEvent *e) {
  if (e->type() == QEvent::StyleChange && !m_updatingStyle) {
    updateStyle();
  }
  QFrame::changeEvent(e);
}

void GridCell::updateStyle() {
  m_updatingStyle = true;
  const auto &theme = ThemeManager::instance().activeTheme();
  const auto &c = theme.colors;
  const auto &w = theme.widgets;

  if (m_selected) {
    // Gold for selected (new selection)
    setStyleSheet(
        QString("GridCell { background-color: %1; border: 2px solid %2; }")
            .arg(c.textAccent, c.textAccent));
  } else if (m_current) {
    // Gray for current position on different monitor
    setStyleSheet(
        QString("GridCell { background-color: %1; border: 2px dashed %2; }")
            .arg(w.scrollbarHandle, c.textSecondary));
  } else if (m_hovered) {
    setStyleSheet(
        QString("GridCell { background-color: %1; border: 1px solid %2; }")
            .arg(w.comboboxDropdownBg, w.inputBorder));
  } else {
    setStyleSheet(
        QString("GridCell { background-color: %1; border: 1px solid %2; }")
            .arg(c.windowSurface, w.groupboxBorder));
  }
  m_updatingStyle = false;
}
