/**
 * @file ToggleSwitch.cpp
 * @brief iOS-style Toggle Switch - Inspired by Toggle.tsx
 *
 * A modern animated toggle switch instead of checkbox.
 * Colors are driven by ThemeData::ToggleTokens via ThemeManager.
 *
 * DO NOT ADD:
 * - Complex animations (keep it simple)
 * - Platform OS variations (iOS style is the only style)
 */

#include "ToggleSwitch.h"

#include "core/ThemeManager.h"
#include "ui/UIHelpers.h"

// ToggleSwitch implementation
ToggleSwitch::ToggleSwitch(QWidget *parent) : QWidget(parent) {
  setFixedSize(52, 24);
  setCursor(Qt::PointingHandCursor);

  m_animation = new QPropertyAnimation(this, "handlePosition", this);
  m_animation->setDuration(150);
  m_animation->setEasingCurve(QEasingCurve::InOutQuad);
}

void ToggleSwitch::setChecked(bool checked) {
  if (m_checked != checked) {
    m_checked = checked;
    // Instant position — no animation for programmatic changes
    m_animation->stop();
    m_handlePos = m_checked ? 1.0 : 0.0;
    update();
    emit toggled(m_checked);
  }
}

void ToggleSwitch::setHandlePosition(qreal pos) {
  m_handlePos = pos;
  update();
}

void ToggleSwitch::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // Read live theme colors
  const auto &tok = ThemeManager::instance().activeTheme().toggle;
  QColor trackColor = m_checked ? QColor(tok.onBg) : QColor(tok.offBg);
  QColor handleColor(tok.handleColor);

  // Track background
  qreal radius = static_cast<qreal>(height()) / 2.0;
  QPainterPath track;
  track.addRoundedRect(QRectF(0, 0, width(), height()), radius, radius);
  p.fillPath(track, trackColor);

  // Handle — symmetric 2px padding on all sides
  const qreal pad = 2.0;
  qreal handleSize = height() - pad * 2;
  qreal travel = width() - handleSize - pad * 2;
  qreal handleX = pad + m_handlePos * travel;
  qreal handleY = pad;

  // Shadow
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 30));
  p.drawEllipse(QRectF(handleX + 1, handleY + 2, handleSize, handleSize));

  // Handle circle
  QPainterPath handle;
  handle.addEllipse(QRectF(handleX, handleY, handleSize, handleSize));
  p.setBrush(handleColor);
  p.drawPath(handle);
}

void ToggleSwitch::toggleWithAnimation() {
  m_checked = !m_checked;
  animateToggle();
  emit toggled(m_checked);
}

void ToggleSwitch::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    emit clicked(); // Emit before state change
    toggleWithAnimation();
    e->accept(); // Consume event to prevent drag propagation
  }
}

void ToggleSwitch::changeEvent(QEvent *e) {
  // Repaint when theme changes — QPainter reads live tokens in paintEvent
  if (e->type() == QEvent::StyleChange) {
    update();
  }
  QWidget::changeEvent(e);
}

void ToggleSwitch::animateToggle() {
  m_animation->stop();
  m_animation->setStartValue(m_handlePos);
  m_animation->setEndValue(m_checked ? 1.0 : 0.0);
  m_animation->start();
}

// LabeledToggle implementation
LabeledToggle::LabeledToggle(const QString &label, QWidget *parent)
    : QWidget(parent) {
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 4, 0, 4);

  m_toggle = new ToggleSwitch(this);
  layout->addWidget(m_toggle);

  m_label = new QLabel(label, this);
  UIHelpers::applyLabelRole(m_label);
  m_label->setCursor(Qt::PointingHandCursor);
  layout->addWidget(m_label);

  layout->addStretch();

  connect(m_toggle, &ToggleSwitch::toggled, this, &LabeledToggle::toggled);
}

void LabeledToggle::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    m_toggle->toggleWithAnimation();
    e->accept();
  }
}
