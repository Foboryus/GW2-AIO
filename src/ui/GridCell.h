#pragma once

#include <QFrame>
#include <QMouseEvent>

/**
 * @brief Simple clickable grid cell with current position indicator
 *
 * Used by WindowGridSelector to display a grid of selectable cells.
 */
class GridCell : public QFrame {
  Q_OBJECT

public:
  GridCell(int row, int col, QWidget *parent = nullptr);

  int row() const { return m_row; }
  int col() const { return m_col; }

  bool isSelected() const { return m_selected; }
  void setSelected(bool selected);

  /**
   * @brief Mark as "current" (saved position on different monitor) - shows gray
   */
  void setCurrent(bool current);

signals:
  void clicked(int row, int col, Qt::KeyboardModifiers mods);

protected:
  void mousePressEvent(QMouseEvent *e) override;
  void enterEvent(QEnterEvent *e) override;
  void leaveEvent(QEvent *e) override;
  void changeEvent(QEvent *e) override;

private:
  void updateStyle();

  int m_row, m_col;
  bool m_selected = false;
  bool m_current = false;
  bool m_hovered = false;
  bool m_updatingStyle = false;
};
