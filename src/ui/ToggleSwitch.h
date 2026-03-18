#pragma once

/**
 * @brief iOS-style Toggle Switch - Inspired by Toggle.tsx
 *
 * A modern animated toggle switch instead of checkbox.
 *
 * DO NOT ADD:
 * - Inline implementations (use ToggleSwitch.cpp)
 */

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QWidget>

class ToggleSwitch : public QWidget {
  Q_OBJECT
  Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
  Q_PROPERTY(qreal handlePosition READ handlePosition WRITE setHandlePosition)

public:
  explicit ToggleSwitch(QWidget *parent = nullptr);

  bool isChecked() const { return m_checked; }
  void setChecked(bool checked); // Instant (programmatic)
  void toggleWithAnimation();    // Animated (user interaction)

  qreal handlePosition() const { return m_handlePos; }
  void setHandlePosition(qreal pos);

signals:
  void toggled(bool checked);
  void clicked(); // Emitted when user clicks, before state change

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *e) override;
  void changeEvent(QEvent *e) override;

private:
  void animateToggle();

  bool m_checked = false;
  qreal m_handlePos = 0.0;
  QPropertyAnimation *m_animation;
};

/**
 * @brief Toggle with label - like Toggle.tsx
 */
class LabeledToggle : public QWidget {
  Q_OBJECT

public:
  explicit LabeledToggle(const QString &label, QWidget *parent = nullptr);

  bool isChecked() const { return m_toggle->isChecked(); }
  void setChecked(bool checked) { m_toggle->setChecked(checked); }
  ToggleSwitch *toggle() { return m_toggle; }

signals:
  void toggled(bool checked);

protected:
  void mousePressEvent(QMouseEvent *e) override;

private:
  ToggleSwitch *m_toggle;
  QLabel *m_label;
};
