#include "StyledTooltip.h"
#include "UIHelpers.h"

// ============================================================================
// StyledTooltip Implementation
// ============================================================================

StyledTooltip::StyledTooltip() : QWidget(nullptr) {
  // Window flags for tooltip behavior
  setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint |
                 Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);

  // Layout
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Background container with styling
  auto *container = new QWidget(this);
  UIHelpers::applyContainerRole(container);
  layout->addWidget(container);

  auto *innerLayout = new QVBoxLayout(container);
  innerLayout->setContentsMargins(12, 8, 12, 8);

  // Label
  m_label = new QLabel(container);
  UIHelpers::applyPopupLabelRole(m_label);
  m_label->setStyleSheet(
      QString("font-size: %1px; background: transparent; border: none;")
          .arg(ThemeManager::instance().activeTheme().layout.fontSizeHint));
  m_label->setWordWrap(true);
  innerLayout->addWidget(m_label);

  // Opacity effect for fade animation
  m_opacityEffect = new QGraphicsOpacityEffect(this);
  m_opacityEffect->setOpacity(0.0);
  setGraphicsEffect(m_opacityEffect);

  // Fade animation
  m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
  m_fadeAnimation->setDuration(FADE_DURATION_MS);

  // Connect finished signal once in constructor (not per-fadeOut call)
  connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
    if (m_opacityEffect->opacity() < 0.1) {
      hide();
      m_isVisible = false;
    }
  });

  // Show delay timer
  m_showTimer = new QTimer(this);
  m_showTimer->setSingleShot(true);
  connect(m_showTimer, &QTimer::timeout, this,
          [this]() { showAt(m_pendingText, m_pendingPos); });

  // Hide timer (for when mouse leaves)
  m_hideTimer = new QTimer(this);
  m_hideTimer->setSingleShot(true);
  connect(m_hideTimer, &QTimer::timeout, this, [this]() { fadeOut(); });
}

StyledTooltip *StyledTooltip::instance() {
  static StyledTooltip *s_instance = nullptr;
  if (!s_instance) {
    s_instance = new StyledTooltip();
  }
  return s_instance;
}

void StyledTooltip::showTooltip(const QString &text, const QPoint &globalPos) {
  auto *tooltip = instance();
  tooltip->m_pendingText = text;
  tooltip->m_pendingPos = globalPos;
  tooltip->m_showTimer->start(SHOW_DELAY_MS);
  tooltip->m_hideTimer->stop();
}

void StyledTooltip::hideTooltip() {
  auto *tooltip = instance();
  tooltip->m_showTimer->stop();
  if (tooltip->m_isVisible) {
    tooltip->fadeOut();
  }
}

void StyledTooltip::showAt(const QString &text, const QPoint &globalPos) {
  m_label->setText(text);
  adjustSize();

  // Calculate position - offset from cursor
  int offsetX = 15;
  int offsetY = 20;
  int x = globalPos.x() + offsetX;
  int y = globalPos.y() + offsetY;

  // Get screen geometry to ensure tooltip stays on screen
  QScreen *screen = QGuiApplication::screenAt(globalPos);
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen) {
    QRect screenRect = screen->availableGeometry();

    // Adjust if tooltip would go off right edge
    if (x + width() > screenRect.right()) {
      x = globalPos.x() - width() - 5;
    }

    // Adjust if tooltip would go off bottom edge
    if (y + height() > screenRect.bottom()) {
      y = globalPos.y() - height() - 5;
    }

    // Ensure not off left/top edges
    x = qMax(x, screenRect.left());
    y = qMax(y, screenRect.top());
  }

  move(x, y);
  show();
  fadeIn();
}

void StyledTooltip::fadeIn() {
  if (m_fadeAnimation->state() == QAbstractAnimation::Running) {
    m_fadeAnimation->stop();
  }
  m_isVisible = true;
  m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
  m_fadeAnimation->setEndValue(1.0);
  m_fadeAnimation->start();
}

void StyledTooltip::fadeOut() {
  if (m_fadeAnimation->state() == QAbstractAnimation::Running) {
    m_fadeAnimation->stop();
  }
  m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
  m_fadeAnimation->setEndValue(0.0);
  m_fadeAnimation->start();
}

// ============================================================================
// TooltipEventFilter Implementation
// ============================================================================

bool TooltipEventFilter::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::ToolTip) {
    QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
    QWidget *widget = qobject_cast<QWidget *>(obj);

    if (widget && !widget->toolTip().isEmpty()) {
      StyledTooltip::showTooltip(widget->toolTip(), helpEvent->globalPos());
      return true; // Consume the event to prevent Qt's default tooltip
    }
  } else if (event->type() == QEvent::Leave ||
             event->type() == QEvent::MouseButtonPress ||
             event->type() == QEvent::WindowDeactivate) {
    StyledTooltip::hideTooltip();
  }

  return QObject::eventFilter(obj, event);
}
