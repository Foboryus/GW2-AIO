/**
 * @file RadialWidget.cpp
 * @brief Renders the radial menu as a transparent overlay widget
 *
 * DO NOT ADD:
 * - State management (belongs in RadialEngine)
 * - Configuration (belongs in RadialConfig)
 */

#include "RadialWidget.h"

#include <QCursor>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

RadialWidget::RadialWidget(RadialEngine *engine, QWidget *parent)
    : QWidget(parent,
              Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      m_engine(engine), m_cursorTimer(new QTimer(this)) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setMouseTracking(true);

  // Set size to cover screen
  setFixedSize(m_outerRadius * 2 + 40, m_outerRadius * 2 + 40);

  // Connect engine signals
  connect(m_engine, &RadialEngine::menuShown, this, &RadialWidget::onMenuShown);
  connect(m_engine, &RadialEngine::menuHidden, this,
          &RadialWidget::onMenuHidden);
  connect(m_engine, &RadialEngine::hoverChanged, this,
          &RadialWidget::onHoverChanged);

  // Cursor tracking timer
  connect(m_cursorTimer, &QTimer::timeout, this,
          &RadialWidget::updateCursorTracking);
  m_cursorTimer->setInterval(16); // ~60fps
}

void RadialWidget::setColors(const QColor &bg, const QColor &hover,
                             const QColor &accent) {
  m_bgColor = bg;
  m_hoverColor = hover;
  m_accentColor = accent;
  update();
}

void RadialWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);

  const RadialMenu *menu = m_engine->activeMenu();
  if (!menu)
    return;

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  QPoint center(width() / 2, height() / 2);
  int itemCount = menu->items.size();

  if (itemCount == 0)
    return;

  // Draw each slice
  for (int i = 0; i < itemCount; i++) {
    drawSlice(painter, i, i == m_engine->hoveredIndex());
  }

  // Draw center circle (deadzone indicator)
  painter.setBrush(m_bgColor);
  painter.setPen(QPen(m_accentColor, 2));
  painter.drawEllipse(center, m_innerRadius - 5, m_innerRadius - 5);

  // Draw menu name in center
  painter.setPen(m_textColor);
  painter.setFont(QFont("Segoe UI", 10, QFont::Bold));
  painter.drawText(QRect(center.x() - 50, center.y() - 10, 100, 20),
                   Qt::AlignCenter, menu->name);
}

void RadialWidget::drawSlice(QPainter &painter, int index, bool hovered) {
  const RadialMenu *menu = m_engine->activeMenu();
  if (!menu || index >= menu->items.size())
    return;

  const RadialItem &item = menu->items[index];
  int itemCount = menu->items.size();

  QPoint center(width() / 2, height() / 2);

  // Calculate slice angles
  double sliceAngle = 360.0 / itemCount;
  double startAngle = 90 - (index * sliceAngle) - sliceAngle / 2; // 90° = top

  // Create slice path
  QPainterPath path;
  QRectF outerRect(center.x() - m_outerRadius, center.y() - m_outerRadius,
                   m_outerRadius * 2, m_outerRadius * 2);
  QRectF innerRect(center.x() - m_innerRadius, center.y() - m_innerRadius,
                   m_innerRadius * 2, m_innerRadius * 2);

  path.arcMoveTo(outerRect, startAngle);
  path.arcTo(outerRect, startAngle, sliceAngle);
  path.arcTo(innerRect, startAngle + sliceAngle, -sliceAngle);
  path.closeSubpath();

  // Fill
  QColor fillColor = !item.enabled ? m_disabledColor
                     : hovered     ? m_hoverColor
                                   : m_bgColor;
  painter.fillPath(path, fillColor);

  // Border
  painter.setPen(QPen(m_accentColor, 1));
  painter.drawPath(path);

  // Calculate icon position (center of slice)
  double midAngle = (startAngle + sliceAngle / 2) * M_PI / 180.0;
  int iconRadius = (m_innerRadius + m_outerRadius) / 2;
  int iconX = center.x() + iconRadius * std::cos(midAngle);
  int iconY = center.y() - iconRadius * std::sin(midAngle);

  // Draw icon/text
  QRect iconRect(iconX - 25, iconY - 25, 50, 50);
  drawIcon(painter, index, iconRect);
}

void RadialWidget::drawIcon(QPainter &painter, int index, const QRect &bounds) {
  const RadialMenu *menu = m_engine->activeMenu();
  if (!menu || index >= menu->items.size())
    return;

  const RadialItem &item = menu->items[index];

  // Draw icon (emoji for now)
  painter.setPen(m_textColor);
  painter.setFont(QFont("Segoe UI Emoji", 24));
  painter.drawText(bounds, Qt::AlignCenter, item.icon);

  // Draw name below
  QRect nameRect = bounds.adjusted(-10, 35, 10, 55);
  painter.setFont(QFont("Segoe UI", 9));
  painter.drawText(nameRect, Qt::AlignCenter, item.name);
}

void RadialWidget::mouseMoveEvent(QMouseEvent *event) {
  m_engine->updateHover(mapToGlobal(event->pos()));
}

void RadialWidget::showEvent(QShowEvent *event) {
  Q_UNUSED(event);
  m_cursorTimer->start();
}

void RadialWidget::hideEvent(QHideEvent *event) {
  Q_UNUSED(event);
  m_cursorTimer->stop();
}

void RadialWidget::onMenuShown(const RadialMenu *menu) {
  Q_UNUSED(menu);

  // Center widget on cursor
  QPoint center = m_engine->menuCenter();
  move(center.x() - width() / 2, center.y() - height() / 2);

  show();
  update();
}

void RadialWidget::onMenuHidden() { hide(); }

void RadialWidget::onHoverChanged(int index) {
  Q_UNUSED(index);
  update();
}

void RadialWidget::updateCursorTracking() {
  m_engine->updateHover(QCursor::pos());
}
