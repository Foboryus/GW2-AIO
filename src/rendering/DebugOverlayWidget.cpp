#include "DebugOverlayWidget.h"

#include "core/MumbleLink.h"

#include <QVBoxLayout>

DebugOverlayWidget::DebugOverlayWidget(MumbleLink *mumble, QWidget *parent)
    : QWidget(parent), m_mumble(mumble) {
  // Frameless, topmost, click-through when not dragging
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);

  // Dark semi-transparent background
  // REVIEW BEFORE BETA: inline setStyleSheet (debug tool — intentionally not themed)
  setStyleSheet(
      "QWidget { background: rgba(20, 20, 20, 200); border-radius: 6px; }"
      "QLabel { color: #00FF00; font-family: 'Consolas', monospace; "
      "font-size: 11px; background: transparent; }");

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(2);

  m_label = new QLabel("Waiting for MumbleLink...", this);
  m_label->setTextFormat(Qt::RichText);
  layout->addWidget(m_label);

  setFixedWidth(320);

  // Update every 100ms
  m_timer = new QTimer(this);
  connect(m_timer, &QTimer::timeout, this, &DebugOverlayWidget::updateDisplay);
  m_timer->start(100);
}

void DebugOverlayWidget::attachToWindow(WId gw2Hwnd) {
  if (!gw2Hwnd)
    return;

  // Store HWND for drag constraining
  m_gw2Hwnd = reinterpret_cast<HWND>(gw2Hwnd);

  // Position near top-left of GW2 window
  RECT rect = {};
  GetClientRect(m_gw2Hwnd, &rect);
  POINT topLeft = {rect.left, rect.top};
  ClientToScreen(m_gw2Hwnd, &topLeft);
  move(topLeft.x + 10, topLeft.y + 10);
  show();
}

void DebugOverlayWidget::updateDisplay() {
  if (!m_mumble || !m_mumble->isConnected()) {
    m_label->setText("MumbleLink: <b style='color:#FF4444'>DISCONNECTED</b>");
    adjustSize();
    return;
  }

  auto pos = m_mumble->playerPosition();
  auto cam = m_mumble->cameraPosition();
  auto camF = m_mumble->cameraFront();
  uint32_t mapId = m_mumble->mapId();
  uint32_t pid = m_mumble->processId();
  float fov = m_mumble->fov();
  bool mapOpen = m_mumble->isMapOpen();
  QString charName = m_mumble->characterName();
  QString linkName = m_mumble->linkName();

  auto n = [](float v, int prec = 2) { return QString::number(v, 'f', prec); };

  QString html =
      QStringLiteral("<b style='color:#C09C57'>GW2 AIO Details Tracker</b><br>"
                     "<b>Link:</b> ") +
      linkName + " &nbsp; <b>Char:</b> " + charName +
      "<br>"
      "<b>PID:</b> " +
      QString::number(pid) + " &nbsp; <b>Map:</b> " + QString::number(mapId) +
      " &nbsp; <b>FOV:</b> " + n(fov, 3) +
      "<br>"
      "<b>Pos:</b> " +
      n(pos.x()) + ", " + n(pos.y()) + ", " + n(pos.z()) +
      "<br>"
      "<b>Cam:</b> " +
      n(cam.x()) + ", " + n(cam.y()) + ", " + n(cam.z()) +
      "<br>"
      "<b>Dir:</b> " +
      n(camF.x(), 3) + ", " + n(camF.y(), 3) + ", " + n(camF.z(), 3) +
      "<br>"
      "<b>Map UI:</b> " +
      (mapOpen ? "OPEN" : "closed");

  m_label->setText(html);
  adjustSize();
}

void DebugOverlayWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = true;
    m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
  }
}

void DebugOverlayWidget::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging && (event->buttons() & Qt::LeftButton)) {
    QPoint newPos = event->globalPosition().toPoint() - m_dragPos;

    // Constrain to GW2 window bounds if available
    if (m_gw2Hwnd && IsWindow(m_gw2Hwnd)) {
      RECT gw2Rect = {};
      GetWindowRect(m_gw2Hwnd, &gw2Rect);

      int maxX = gw2Rect.right - width();
      int maxY = gw2Rect.bottom - height();
      int minX = gw2Rect.left;
      int minY = gw2Rect.top;

      newPos.setX(qBound(minX, newPos.x(), maxX));
      newPos.setY(qBound(minY, newPos.y(), maxY));
    }

    move(newPos);
    event->accept();
  }
}

void DebugOverlayWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    m_dragging = false;
    event->accept();
  }
}
