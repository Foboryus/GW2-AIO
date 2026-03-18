// REVIEW BEFORE BETA: hardcoded colors (L55-59) — integrate with ThemeManager.
#pragma once

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWidget>
#include <cmath>


#include "RadialEngine.h"

/**
 * @brief Renders the radial menu as a transparent overlay widget
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialWidget.cpp)
 */
class RadialWidget : public QWidget {
  Q_OBJECT

public:
  explicit RadialWidget(RadialEngine *engine, QWidget *parent = nullptr);

  // Appearance settings
  void setRadius(int inner, int outer) {
    m_innerRadius = inner;
    m_outerRadius = outer;
    update();
  }
  void setColors(const QColor &bg, const QColor &hover, const QColor &accent);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private slots:
  void onMenuShown(const RadialMenu *menu);
  void onMenuHidden();
  void onHoverChanged(int index);
  void updateCursorTracking();

private:
  void drawSlice(QPainter &painter, int index, bool hovered);
  void drawIcon(QPainter &painter, int index, const QRect &bounds);

  RadialEngine *m_engine;
  QTimer *m_cursorTimer;

  // Appearance
  int m_innerRadius = 60;
  int m_outerRadius = 180;
  QColor m_bgColor{30, 30, 30, 220};
  QColor m_hoverColor{192, 156, 87, 255}; // GW2 gold
  QColor m_accentColor{60, 60, 60, 255};
  QColor m_textColor{224, 224, 224, 255};
  QColor m_disabledColor{100, 100, 100, 200};
};
