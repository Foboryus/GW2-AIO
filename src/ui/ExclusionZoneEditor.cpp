/**
 * @file ExclusionZoneEditor.cpp
 * @brief Transparent overlay editor for custom exclusion zones
 *
 * QPainter-based rendering with manual hit-testing.
 * Drag to create zones, delete button to remove.
 * Escape or close button exits editing mode.
 */

#include "ExclusionZoneEditor.h"

#include "core/MumbleLink.h"
#include "features/markers/MarkerSettingsManager.h"
#include "rendering/ExclusionData.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

// ============================================================================
// Constructor
// ============================================================================

ExclusionZoneEditor::ExclusionZoneEditor(QWidget *parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::CrossCursor);
  hide(); // Hidden by default
}

// ============================================================================
// Public API
// ============================================================================

void ExclusionZoneEditor::setMarkerSettings(MarkerSettingsManager *settings) {
  m_markerSettings = settings;
}

void ExclusionZoneEditor::setMumbleLink(MumbleLink *mumble) {
  m_mumbleLink = mumble;
}

void ExclusionZoneEditor::beginEditing() {
  loadZonesFromSettings();
  m_mode = Mode::None;
  m_dragRect = QRectF();
  m_hoverDeleteIndex = -1;
  show();
  raise();
  setFocus();
  update();
}

void ExclusionZoneEditor::finishEditing() {
  saveZonesToSettings();
  // Flush immediately — don't rely on the 2s debounce timer,
  // which may never fire if AIO is closed soon after
  if (m_markerSettings) {
    m_markerSettings->saveNow();
  }
  hide();
  emit editingFinished();
}

// ============================================================================
// Paint
// ============================================================================

void ExclusionZoneEditor::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Semi-transparent dark overlay to indicate editing mode
  painter.fillRect(rect(), QColor(0, 0, 0, 40));

  // Draw border around the entire widget
  painter.setPen(QPen(QColor("#C09C57"), 2.0, Qt::DashLine));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(rect().adjusted(1, 1, -1, -1));

  // Header text
  QFont headerFont("Segoe UI", 14, QFont::Bold);
  painter.setFont(headerFont);
  painter.setPen(QColor("#C09C57"));

  QString headerText = "Exclusion Zone Editor";
  QRectF headerRect(0, 8, width(), 30);
  painter.drawText(headerRect, Qt::AlignHCenter | Qt::AlignTop, headerText);

  // Instructions
  QFont instrFont("Segoe UI", 10);
  painter.setFont(instrFont);
  painter.setPen(QColor(200, 200, 200, 220));

  // Count only custom zones for display and limit
  int customCount = 0;
  for (const auto &z : m_zones) {
    if (z.type == EditorZone::Custom)
      customCount++;
  }

  QString instrText = "Click and drag to create a zone.";
  if (customCount >= kMaxCustomZones) {
    instrText = "Maximum zones reached. Delete a zone to add more.";
  }
  QRectF instrRect(0, 36, width(), 20);
  painter.drawText(instrRect, Qt::AlignHCenter | Qt::AlignTop, instrText);

  // Zone count
  QFont countFont("Segoe UI", 9);
  painter.setFont(countFont);
  painter.setPen(QColor(180, 180, 180, 180));
  QString countText =
      QString("%1 / %2 custom zones").arg(customCount).arg(kMaxCustomZones);
  QRectF countRect(0, 54, width(), 16);
  painter.drawText(countRect, Qt::AlignHCenter | Qt::AlignTop, countText);

  // Close button (centered below header area)
  constexpr int kCloseBtnW = 120;
  constexpr int kCloseBtnH = 32;
  QRectF closeBtnRect((width() - kCloseBtnW) / 2.0, 74, kCloseBtnW, kCloseBtnH);
  m_closeBtnRect = closeBtnRect;
  bool closeHovered = closeBtnRect.contains(mapFromGlobal(QCursor::pos()));
  painter.setBrush(QColor(closeHovered ? "#8B3030" : "#4A2020"));
  painter.setPen(QPen(QColor("#C09C57"), 1.5));
  painter.drawRoundedRect(closeBtnRect, 6, 6);

  painter.setFont(QFont("Segoe UI", 11, QFont::Bold));
  painter.setPen(QColor(closeHovered ? "#FFFFFF" : "#CCCCCC"));
  painter.drawText(closeBtnRect, Qt::AlignCenter, "Close");

  // --- Draw existing zones ---
  for (int i = 0; i < m_zones.size(); ++i) {
    EditorZone &zone = m_zones[i];
    const QRectF &r = zone.rect;

    // Color scheme per type
    QColor fillColor, borderColor;
    switch (zone.type) {
    case EditorZone::Custom:
      fillColor = QColor(192, 156, 87, 50); // Gold tint
      borderColor = QColor("#C09C57");
      break;
    case EditorZone::Predefined:
      fillColor = QColor(70, 130, 200, 50); // Blue tint
      borderColor = QColor("#4682C8");
      break;
    case EditorZone::MinimapRef:
      fillColor = QColor(120, 120, 120, 30); // Dimmed grey
      borderColor = QColor(120, 120, 120, 100);
      break;
    }

    painter.setBrush(fillColor);
    painter.setPen(QPen(borderColor, 2.0,
                        zone.type == EditorZone::MinimapRef ? Qt::DotLine
                                                            : Qt::SolidLine));
    painter.drawRect(r);

    // Zone label
    {
      painter.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
      painter.setPen(zone.type == EditorZone::MinimapRef
                         ? QColor(180, 180, 180, 150)
                         : QColor(255, 255, 255, 200));
      QString label =
          zone.name.isEmpty() ? QString("Zone %1").arg(i + 1) : zone.name;
      // Add type badge for predefined zones
      if (zone.type == EditorZone::Predefined)
        label = QString::fromUtf8("\u2699 ") + label; // Gear symbol prefix
      if (zone.type == EditorZone::MinimapRef)
        label += " (ref)";
      QRectF labelRect(r.left(), r.top(), r.width(),
                       zone.type == EditorZone::MinimapRef ? r.height()
                                                           : r.height() * 0.4);
      painter.drawText(labelRect, Qt::AlignCenter, label);
    }

    // Icons only for Custom and Predefined zones (not MinimapRef)
    if (zone.type == EditorZone::MinimapRef) {
      zone.editButtonRect = QRectF();
      zone.deleteButtonRect = QRectF();
      continue;
    }

    // Centered delete/reset icon (single button, no edit)
    zone.editButtonRect = QRectF(); // No edit button
    qreal iconsX = r.center().x() - kIconBtnSize / 2.0;
    qreal iconsY = r.center().y() + r.height() * 0.05;

    // Delete/Reset button rect
    zone.deleteButtonRect = QRectF(iconsX, iconsY, kIconBtnSize, kIconBtnSize);

    // Draw trash/reset icon button
    bool delHovered =
        zone.deleteButtonRect.contains(mapFromGlobal(QCursor::pos()));
    if (zone.type == EditorZone::Predefined) {
      // Reset button (blue tint) — resets override to default
      painter.setBrush(QColor(delHovered ? "#3A5A8A" : "#2A3A5A"));
    } else {
      // Trash button (red tint)
      painter.setBrush(QColor(delHovered ? "#5A2020" : "#3A2020"));
    }
    painter.setPen(QPen(borderColor, 1.0));
    painter.drawRoundedRect(zone.deleteButtonRect, 4, 4);

    QSvgRenderer trashSvg(QString(zone.type == EditorZone::Predefined
                                      ? ":/icons/refresh.svg"
                                      : ":/icons/trash.svg"));
    QRectF trashIconArea = zone.deleteButtonRect.adjusted(4, 4, -4, -4);
    trashSvg.render(&painter, trashIconArea);
  }

  // --- Draw current drag rectangle (Creating mode) ---
  if (m_mode == Mode::Creating && m_dragRect.width() > 2 &&
      m_dragRect.height() > 2) {
    painter.setBrush(QColor(100, 180, 255, 40)); // Blue tint while dragging
    painter.setPen(QPen(QColor(100, 180, 255, 200), 2.0, Qt::DashLine));
    painter.drawRect(m_dragRect);

    // Size indicator
    painter.setFont(QFont("Segoe UI", 8));
    painter.setPen(QColor(200, 200, 200, 200));
    float pctW = 0, pctH = 0, pctX = 0, pctY = 0;
    pixelsToPercent(m_dragRect, pctX, pctY, pctW, pctH);
    QString sizeText =
        QString("%1% x %2%").arg(qRound(pctW * 100)).arg(qRound(pctH * 100));
    painter.drawText(m_dragRect, Qt::AlignCenter, sizeText);
  }
}

// ============================================================================
// Edge Detection Helper
// ============================================================================

static constexpr int kEdgeGrip = 8; // Pixels from edge for resize detection

int ExclusionZoneEditor::hitTestEdges(const QRectF &r,
                                      const QPointF &pos) const {
  if (!r.contains(pos))
    return Edge::None;

  int edges = 0;
  if (pos.x() - r.left() < kEdgeGrip)
    edges |= Edge::Left;
  if (r.right() - pos.x() < kEdgeGrip)
    edges |= Edge::Right;
  if (pos.y() - r.top() < kEdgeGrip)
    edges |= Edge::Top;
  if (r.bottom() - pos.y() < kEdgeGrip)
    edges |= Edge::Bottom;
  return edges;
}

Qt::CursorShape ExclusionZoneEditor::cursorForEdges(int edges) const {
  bool l = edges & Edge::Left, r = edges & Edge::Right;
  bool t = edges & Edge::Top, b = edges & Edge::Bottom;
  if ((l && t) || (r && b))
    return Qt::SizeFDiagCursor;
  if ((r && t) || (l && b))
    return Qt::SizeBDiagCursor;
  if (l || r)
    return Qt::SizeHorCursor;
  if (t || b)
    return Qt::SizeVerCursor;
  return Qt::ArrowCursor;
}

// ============================================================================
// Mouse Events
// ============================================================================

void ExclusionZoneEditor::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton)
    return;

  QPointF pos = event->position();

  // Close button hit test
  if (m_closeBtnRect.contains(pos)) {
    finishEditing();
    return;
  }

  // Button hit test (delete/reset) — highest priority
  for (int i = 0; i < m_zones.size(); ++i) {
    // Skip MinimapRef zones (non-interactive)
    if (m_zones[i].type == EditorZone::MinimapRef)
      continue;

    if (m_zones[i].deleteButtonRect.isValid() &&
        m_zones[i].deleteButtonRect.contains(pos)) {
      if (m_zones[i].type == EditorZone::Predefined) {
        // Reset predefined zone override to defaults
        if (m_markerSettings) {
          m_markerSettings->resetPredefinedOverride(m_zones[i].predefinedKey);
        }
        loadZonesFromSettings(); // Reload to get default positions
      } else {
        // Delete custom zone
        m_zones.removeAt(i);
        saveZonesToSettings();
      }
      update();
      return;
    }
  }

  // Check if clicking on an existing zone (resize edge or move)
  for (int i = m_zones.size() - 1; i >= 0; --i) {
    // MinimapRef zones are non-interactive
    if (m_zones[i].type == EditorZone::MinimapRef)
      continue;

    const QRectF &r = m_zones[i].rect;
    if (!r.contains(pos))
      continue;

    int edges = hitTestEdges(r, pos);
    if (edges != Edge::None) {
      // Start resizing
      m_mode = Mode::Resizing;
      m_resizeZoneIndex = i;
      m_resizeEdges = edges;
      m_resizeOriginalRect = r;
      m_resizeStart = pos;
      return;
    }

    // Interior click — start moving
    m_mode = Mode::Moving;
    m_moveZoneIndex = i;
    m_moveOffset = pos - r.topLeft();
    return;
  }

  // Count custom zones for limit check
  int customCount = 0;
  for (const auto &z : m_zones) {
    if (z.type == EditorZone::Custom)
      customCount++;
  }

  // Empty area — start creating new zone
  if (customCount < kMaxCustomZones) {
    m_mode = Mode::Creating;
    m_dragStart = pos;
    m_dragRect = QRectF(pos, QSizeF(0, 0));
  }
}

void ExclusionZoneEditor::mouseMoveEvent(QMouseEvent *event) {
  QPointF pos = event->position();

  switch (m_mode) {
  case Mode::Creating: {
    qreal x = qMin(m_dragStart.x(), pos.x());
    qreal y = qMin(m_dragStart.y(), pos.y());
    qreal w = qAbs(pos.x() - m_dragStart.x());
    qreal h = qAbs(pos.y() - m_dragStart.y());
    m_dragRect = QRectF(x, y, w, h);
    update();
    return;
  }
  case Mode::Moving: {
    if (m_moveZoneIndex >= 0 && m_moveZoneIndex < m_zones.size()) {
      QRectF &r = m_zones[m_moveZoneIndex].rect;
      QPointF newTopLeft = pos - m_moveOffset;
      // Clamp to widget bounds
      newTopLeft.setX(qBound(0.0, newTopLeft.x(), (double)width() - r.width()));
      newTopLeft.setY(
          qBound(0.0, newTopLeft.y(), (double)height() - r.height()));
      r.moveTopLeft(newTopLeft);
      update();
    }
    return;
  }
  case Mode::Resizing: {
    if (m_resizeZoneIndex >= 0 && m_resizeZoneIndex < m_zones.size()) {
      QRectF r = m_resizeOriginalRect;
      qreal dx = pos.x() - m_resizeStart.x();
      qreal dy = pos.y() - m_resizeStart.y();

      if (m_resizeEdges & Edge::Left) {
        r.setLeft(qMin(r.left() + dx, r.right() - kMinZoneSize));
      }
      if (m_resizeEdges & Edge::Right) {
        r.setRight(qMax(r.right() + dx, r.left() + kMinZoneSize));
      }
      if (m_resizeEdges & Edge::Top) {
        r.setTop(qMin(r.top() + dy, r.bottom() - kMinZoneSize));
      }
      if (m_resizeEdges & Edge::Bottom) {
        r.setBottom(qMax(r.bottom() + dy, r.top() + kMinZoneSize));
      }

      m_zones[m_resizeZoneIndex].rect = r;
      update();
    }
    return;
  }
  case Mode::None:
    break;
  }

  // --- Hover state updates (when not dragging) ---

  // Update cursor based on what's under it
  for (int i = 0; i < m_zones.size(); ++i) {
    if (m_zones[i].deleteButtonRect.contains(pos)) {
      setCursor(Qt::ArrowCursor);
      m_hoverDeleteIndex = i;
      update();
      return;
    }
  }

  // Check zone edges/interior
  for (int i = m_zones.size() - 1; i >= 0; --i) {
    const QRectF &r = m_zones[i].rect;
    if (!r.contains(pos))
      continue;

    int edges = hitTestEdges(r, pos);
    if (edges != Edge::None) {
      setCursor(cursorForEdges(edges));
    } else {
      setCursor(Qt::SizeAllCursor); // Move cursor
    }
    m_hoverDeleteIndex = -1;
    update();
    return;
  }

  // Over empty area
  setCursor(Qt::CrossCursor);
  if (m_hoverDeleteIndex != -1) {
    m_hoverDeleteIndex = -1;
    update();
  }
}

void ExclusionZoneEditor::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton || m_mode == Mode::None)
    return;

  switch (m_mode) {
  case Mode::Creating: {
    // Only create zone if it's large enough
    if (m_dragRect.width() >= kMinZoneSize &&
        m_dragRect.height() >= kMinZoneSize) {
      EditorZone zone;
      zone.name = QString("Custom %1").arg(m_nextZoneId++);
      zone.rect = m_dragRect;
      m_zones.append(zone);
      saveZonesToSettings();
    }
    m_dragRect = QRectF();
    m_mode = Mode::None;
    break;
  }
  case Mode::Moving:
    saveZonesToSettings(); // Persist final position
    m_mode = Mode::None;
    break;
  case Mode::Resizing:
    saveZonesToSettings(); // Persist final size
    m_mode = Mode::None;
    break;
  default:
    break;
  }

  update();
}

void ExclusionZoneEditor::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    finishEditing();
    return;
  }
  QWidget::keyPressEvent(event);
}

// ============================================================================
// Coordinate Conversion
// ============================================================================

QRectF ExclusionZoneEditor::percentToPixels(float x, float y, float w,
                                            float h) const {
  qreal pw = width();
  qreal ph = height();
  return QRectF(x * pw, y * ph, w * pw, h * ph);
}

void ExclusionZoneEditor::pixelsToPercent(const QRectF &px, float &x, float &y,
                                          float &w, float &h) const {
  qreal pw = width();
  qreal ph = height();
  if (pw <= 0 || ph <= 0) {
    x = y = w = h = 0;
    return;
  }
  x = static_cast<float>(px.x() / pw);
  y = static_cast<float>(px.y() / ph);
  w = static_cast<float>(px.width() / pw);
  h = static_cast<float>(px.height() / ph);
}

// ============================================================================
// Settings Integration
// ============================================================================

void ExclusionZoneEditor::loadZonesFromSettings() {
  m_zones.clear();
  m_nextZoneId = 1;
  if (!m_markerSettings)
    return;

  // --- Predefined zones first (Skill Bar, Chat) ---
  // These are always shown (if their toggle is on) with override or default

  if (m_markerSettings->skillBarZoneEnabled()) {
    EditorZone zone;
    zone.type = EditorZone::Predefined;
    zone.name = "Skill Bar";
    zone.predefinedKey = "SkillBar";
    if (m_markerSettings->hasPredefinedOverride("SkillBar")) {
      auto ov = m_markerSettings->predefinedOverride("SkillBar");
      zone.rect = percentToPixels(ov.x, ov.y, ov.w, ov.h);
    } else {
      // Default: centered bottom, ~40% width, ~9% height
      zone.rect = percentToPixels(0.30f, 0.91f, 0.40f, 0.09f);
    }
    m_zones.append(zone);
  }

  if (m_markerSettings->chatZoneEnabled()) {
    EditorZone zone;
    zone.type = EditorZone::Predefined;
    zone.name = "Chat";
    zone.predefinedKey = "Chat";
    if (m_markerSettings->hasPredefinedOverride("Chat")) {
      auto ov = m_markerSettings->predefinedOverride("Chat");
      zone.rect = percentToPixels(ov.x, ov.y, ov.w, ov.h);
    } else {
      // Default: bottom-left
      zone.rect = percentToPixels(0.0f, 0.75f, 0.28f, 0.25f);
    }
    m_zones.append(zone);
  }

  // --- Minimap reference zone (non-interactive) ---
  if (m_markerSettings->minimapZoneEnabled() && m_mumbleLink &&
      m_mumbleLink->isConnected()) {
    const auto &compass = m_mumbleLink->minimapData();
    if (compass.compassWidth > 0 && compass.compassHeight > 0) {
      EditorZone zone;
      zone.type = EditorZone::MinimapRef;
      zone.name = "Minimap";
      float cw = static_cast<float>(compass.compassWidth) / width();
      float ch = static_cast<float>(compass.compassHeight) / height();
      float cx, cy;
      if (m_mumbleLink->isMinimapTopRight()) {
        cx = 1.0f - cw;
        cy = 0.0f;
      } else {
        // GW2 has a bottom UI bar (~36px at 1080p)
        constexpr float kBottomBarPx = 36.0f;
        float bottomOffset = kBottomBarPx / height();
        cx = 1.0f - cw;
        cy = 1.0f - ch - bottomOffset;
      }
      zone.rect = percentToPixels(cx, cy, cw, ch);
      m_zones.append(zone);
    }
  }

  // --- Custom zones ---
  const auto &customZones = m_markerSettings->customZones();
  for (const auto &ez : customZones) {
    EditorZone zone;
    zone.type = EditorZone::Custom;
    zone.name = ez.name;
    zone.rect = percentToPixels(ez.x, ez.y, ez.w, ez.h);
    m_zones.append(zone);

    // Seed counter from existing names to avoid duplicates
    if (ez.name.startsWith("Custom ")) {
      bool ok = false;
      int num = ez.name.mid(7).toInt(&ok);
      if (ok && num >= m_nextZoneId) {
        m_nextZoneId = num + 1;
      }
    }
  }
}

void ExclusionZoneEditor::saveZonesToSettings() {
  if (!m_markerSettings)
    return;

  // Save custom zones
  QVector<ExclusionZone> exZones;
  for (const auto &zone : m_zones) {
    if (zone.type != EditorZone::Custom)
      continue;
    ExclusionZone ez;
    ez.name = zone.name;
    pixelsToPercent(zone.rect, ez.x, ez.y, ez.w, ez.h);
    exZones.append(ez);
  }
  m_markerSettings->setCustomZones(exZones);

  // Save predefined zone overrides (if moved/resized from defaults)
  for (const auto &zone : m_zones) {
    if (zone.type != EditorZone::Predefined)
      continue;
    ExclusionZone ov;
    ov.name = zone.predefinedKey;
    pixelsToPercent(zone.rect, ov.x, ov.y, ov.w, ov.h);
    m_markerSettings->setPredefinedOverride(zone.predefinedKey, ov);
  }
}
