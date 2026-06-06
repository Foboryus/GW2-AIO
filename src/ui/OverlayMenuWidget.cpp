/**
 * @file OverlayMenuWidget.cpp
 * @brief QPainter-based in-game overlay menu implementation
 *
 * Renders a Blish-style corner icon (top-left) and an expandable panel.
 * All rendering via QPainter with manual hit-testing — no Qt widgets.
 *
 * DO NOT ADD:
 * - Qt widgets (QPushButton, QTreeWidget, etc.)
 * - Marker rendering (belongs in MarkerRenderer/GLMarkerRenderer)
 */

#include "OverlayMenuWidget.h"

#include "features/markers/ImageCache.h"
#include "features/markers/MarkerManager.h"
#include "features/markers/MarkerModels.h"
#include "features/markers/MarkerSettingsManager.h"

#include "core/ThemeManager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <functional>

// Helper: read live overlay tokens from current theme
static const ThemeData::OverlayTokens &overlayTokens() {
  return ThemeManager::instance().activeTheme().overlay;
}

// ============================================================================
// Constructor
// ============================================================================

OverlayMenuWidget::OverlayMenuWidget(QWidget *parent) : QWidget(parent) {
  setMouseTracking(true);
  setAttribute(Qt::WA_TransparentForMouseEvents, false);
}

// ============================================================================
// Public API
// ============================================================================

void OverlayMenuWidget::setMarkerManager(MarkerManager *manager) {
  m_markerManager = manager;
  rebuildTree();

  // Rebuild tree when packs are reloaded (e.g., after download or manual
  // reload)
  if (m_markerManager) {
    connect(m_markerManager, &MarkerManager::packsLoaded, this,
            [this]() {
              rebuildTree();
              update();
            });
  }
}

void OverlayMenuWidget::setMarkerSettings(MarkerSettingsManager *settings) {
  m_markerSettings = settings;
  // Rebuild overlay tree when profile editor changes settings
  if (m_markerSettings) {
    connect(m_markerSettings, &MarkerSettingsManager::settingsChanged, this,
            [this]() {
              if (!m_suppressRebuild) {
                rebuildTree();
                update();
              }
            });
  }
}

void OverlayMenuWidget::setRadialEnabled(bool enabled) {
  if (m_radialEnabled == enabled) return;
  m_radialEnabled = enabled;
  m_radialSettings.radialEnabled = enabled;
  update();
}

void OverlayMenuWidget::setRadialSettings(const RadialSettings &settings) {
  m_radialSettings = settings;
  m_radialEnabled = settings.radialEnabled;
  update();
}

void OverlayMenuWidget::setShouldBeVisible(bool visible) {
  if (m_shouldBeVisible == visible) {
    return;
  }
  m_shouldBeVisible = visible;
  qInfo() << "[DEVLOG] OverlayMenuWidget: visibility changed to:" << visible
          << "iconRect:" << m_iconRect;
  update(); // Trigger fade animation
}

void OverlayMenuWidget::setCombatHidden(bool hidden) {
  if (m_combatHidden == hidden) {
    return;
  }
  m_combatHidden = hidden;
  update(); // Trigger panel fade
}

void OverlayMenuWidget::setGameFocused(bool focused) {
  if (m_gameFocused == focused) {
    return;
  }
  m_gameFocused = focused;
  update(); // Trigger icon swap
}

void OverlayMenuWidget::setMenuOpen(bool open) {
  if (m_isMenuOpen == open) {
    return;
  }
  m_isMenuOpen = open;
  m_scrollOffset = 0;
  m_hoverIndex = -1;

  if (open) {
    rebuildTree();
  }

  emit menuToggled(open);
  update();
}

bool OverlayMenuWidget::isMenuOpen() const { return m_isMenuOpen; }

bool OverlayMenuWidget::isPointOverInteractiveArea(const QPointF &pos) const {
  // Check if over the diamond icon (always interactive)
  if (m_iconRect.isValid() && m_iconRect.contains(pos)) {
    return true;
  }
  // Check if over the open panel
  if (m_isMenuOpen && m_panelRect.isValid() && m_panelRect.contains(pos)) {
    return true;
  }
  return false;
}

void OverlayMenuWidget::rebuildTree() {
  m_treeRoots.clear();
  m_flatItems.clear();

  if (!m_markerManager) {
    return;
  }

  const auto &packs = m_markerManager->packs();
  for (const MarkerPack &pack : packs) {
    // Build set of category paths that have markers/trails.
    // Include all ancestor prefixes so parent categories get hasContent=true
    // when any descendant has content.
    QSet<QString> contentPaths;
    auto addPathAndAncestors = [&contentPaths](const QString &type) {
      if (type.isEmpty())
        return;
      QStringList parts = type.split('.');
      QString path;
      for (const QString &part : parts) {
        path = path.isEmpty() ? part : path + "." + part;
        contentPaths.insert(path);
      }
    };
    for (const Marker &m : pack.markers) {
      addPathAndAncestors(m.type);
    }
    for (const Trail &t : pack.trails) {
      addPathAndAncestors(t.type);
    }

    OverlayMenuItem packItem;
    packItem.id = pack.id;
    packItem.label = pack.name.isEmpty() ? pack.id : pack.name;
    packItem.isPack = true;
    packItem.isExpanded = false;
    packItem.depth = 0;

    // Read enable state from settings
    if (m_markerSettings) {
      packItem.isEnabled = m_markerSettings->isPackEnabled(pack.id);
    }

    // Add categories recursively
    for (const MarkerCategory &cat : pack.categories) {
      addCategoryToModel(packItem.children, pack.id, cat, 1, contentPaths);
    }

    m_treeRoots.append(packItem);
  }

  flattenTree();
}

// ============================================================================
// Paint
// ============================================================================

void OverlayMenuWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);

  // Step main opacity toward target each frame (driven by MumbleLink tick)
  if (m_shouldBeVisible && m_opacity < 1.0) {
    m_opacity = qMin(1.0, m_opacity + kFadeInStep);
    update(); // Keep animating until target reached
  } else if (!m_shouldBeVisible && m_opacity > 0.0) {
    m_opacity = qMax(0.0, m_opacity - kFadeOutStep);
    update(); // Keep animating until fully hidden
  }

  // Step combat panel opacity (independent of main)
  if (!m_combatHidden && m_panelCombatOpacity < 1.0) {
    m_panelCombatOpacity = qMin(1.0, m_panelCombatOpacity + kFadeInStep);
    update();
  } else if (m_combatHidden && m_panelCombatOpacity > 0.0) {
    m_panelCombatOpacity = qMax(0.0, m_panelCombatOpacity - kFadeOutStep);
    update();
  }

  // Fully hidden — skip all painting for performance
  if (m_opacity <= 0.0) {
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  // Diamond/paused icon always draws at main opacity
  painter.setOpacity(m_opacity);
  drawCornerIcon(painter);

  // When game not focused, only draw the paused icon — no panel, no interaction
  if (!m_gameFocused) {
    return;
  }

  // Panel draws at main × combat opacity (combat hides panel only)
  if (m_isMenuOpen && m_panelCombatOpacity > 0.0) {
    painter.setOpacity(m_opacity * m_panelCombatOpacity);
    drawPanel(painter);
  }
}

// ============================================================================
// Corner Icon
// ============================================================================

void OverlayMenuWidget::drawCornerIcon(QPainter &painter) {
  const auto &tok = overlayTokens();

  // Position: after GW2's vanilla icon row
  m_iconRect = QRectF(kIconX, kIconY, kIconSize, kIconSize);

  if (!m_gameFocused) {
    // --- PAUSED STATE: bright red pause icon ---
    painter.setBrush(QColor(40, 10, 10, 200));
    painter.setPen(QPen(QColor("#FF3333"), 1.5));
    painter.drawRoundedRect(m_iconRect, 4, 4);

    // Two vertical bars (pause symbol)
    QRectF inner = m_iconRect.adjusted(8, 6, -8, -6);
    qreal barW = inner.width() * 0.3;
    QRectF leftBar(inner.left(), inner.top(), barW, inner.height());
    QRectF rightBar(inner.right() - barW, inner.top(), barW, inner.height());
    painter.setBrush(QColor("#FF3333"));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(leftBar, 1.5, 1.5);
    painter.drawRoundedRect(rightBar, 1.5, 1.5);
    return;
  }

  // --- NORMAL STATE: gold diamond icon ---
  // Background
  bool isHovered = m_iconRect.contains(mapFromGlobal(QCursor::pos()));
  painter.setBrush(QColor(isHovered ? tok.iconHoverBg : tok.iconBg));
  painter.setPen(QPen(QColor(tok.panelBorder), 1.0));
  painter.drawRoundedRect(m_iconRect, 4, 4);

  // Draw a simple marker icon (diamond shape)
  QRectF inner = m_iconRect.adjusted(6, 4, -6, -4);
  QPainterPath diamond;
  diamond.moveTo(inner.center().x(), inner.top());
  diamond.lineTo(inner.right(), inner.center().y());
  diamond.lineTo(inner.center().x(), inner.bottom());
  diamond.lineTo(inner.left(), inner.center().y());
  diamond.closeSubpath();

  painter.setBrush(QColor(tok.panelBorder));
  painter.setPen(Qt::NoPen);
  painter.drawPath(diamond);
}

// ============================================================================
// Panel
// ============================================================================

void OverlayMenuWidget::drawPanel(QPainter &painter) {
  const auto &tok = overlayTokens();

  // Panel position: use drag position if set, otherwise default below icon
  float panelX, panelY;
  if (m_panelPos.x() >= 0) {
    panelX = m_panelPos.x();
    panelY = m_panelPos.y();
  } else {
    panelX = kIconX;
    panelY = m_iconRect.bottom() + 4;
  }
  float panelHeight = qMin(static_cast<float>(kMaxPanelHeight),
                           static_cast<float>(height()) - panelY - 4);

  m_panelRect = QRectF(panelX, panelY, kPanelWidth, panelHeight);

  // Panel background with rounded corners
  painter.setBrush(QColor(tok.panelBg));
  painter.setPen(QPen(QColor(tok.panelBorder), 1.5));
  painter.drawRoundedRect(m_panelRect, 6, 6);

  // Header — rounded top corners only (clip-based approach)
  m_panelHeaderRect = QRectF(m_panelRect.left(), m_panelRect.top(), kPanelWidth,
                             kPanelHeaderHeight);
  painter.save();
  painter.setClipRect(m_panelHeaderRect);
  painter.setBrush(QColor(tok.headerBg));
  painter.setPen(Qt::NoPen);
  // Draw rounded rect larger than header — bottom corners clip away
  painter.drawRoundedRect(m_panelHeaderRect.adjusted(0, 0, 0, 6), 6, 6);
  painter.restore();

  // Header text
  painter.setPen(QColor(tok.panelBorder));
  QFont headerFont("Segoe UI", 11, QFont::Bold);
  painter.setFont(headerFont);
  painter.drawText(m_panelHeaderRect.adjusted(10, 0, 0, 0), Qt::AlignVCenter,
                   "Marker Packs");

  // Close button (X) — top-right of header
  {
    qreal btnSize = kCloseButtonSize;
    qreal btnX = m_panelHeaderRect.right() - btnSize - 8;
    qreal btnY = m_panelHeaderRect.center().y() - btnSize / 2.0;
    m_closeButtonRect = QRectF(btnX, btnY, btnSize, btnSize);

    // Draw X lines
    bool hovered = m_closeButtonRect.adjusted(-2, -2, 2, 2)
                       .contains(mapFromGlobal(QCursor::pos()));
    painter.setPen(QPen(QColor(hovered ? "#FF6B6B" : tok.textSecondary), 2.0,
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(
        QPointF(m_closeButtonRect.left() + 3, m_closeButtonRect.top() + 3),
        QPointF(m_closeButtonRect.right() - 3, m_closeButtonRect.bottom() - 3));
    painter.drawLine(
        QPointF(m_closeButtonRect.right() - 3, m_closeButtonRect.top() + 3),
        QPointF(m_closeButtonRect.left() + 3, m_closeButtonRect.bottom() - 3));
  }

  // Separator line
  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(
      QPointF(m_panelRect.left() + 4, m_panelHeaderRect.bottom()),
      QPointF(m_panelRect.right() - 4, m_panelHeaderRect.bottom()));

  // Tab bar (Packs | Settings)
  QRectF tabArea(m_panelRect.left(), m_panelHeaderRect.bottom() + 1,
                 kPanelWidth, kTabBarHeight);
  drawTabBar(painter, tabArea);

  // Content area below tab bar
  qreal contentTop = tabArea.bottom() + 1;
  QRectF contentArea(m_panelRect.left(), contentTop, kPanelWidth,
                     m_panelRect.bottom() - contentTop);

  if (m_activeTab == Tab::Packs) {
    // Clip to content area and draw tree
    painter.save();
    painter.setClipRect(contentArea);

    int yOffset = static_cast<int>(contentArea.top()) - m_scrollOffset;

    for (int i = 0; i < m_flatItems.size(); ++i) {
      if (yOffset > contentArea.bottom()) {
        break;
      }
      if (yOffset + kItemHeight > contentArea.top()) {
        drawTreeItem(painter, *m_flatItems[i], yOffset, i);
      }
      yOffset += kItemHeight;
    }

    // Update max scroll
    int totalHeight = m_flatItems.size() * kItemHeight;
    m_maxScroll = qMax(0, totalHeight - static_cast<int>(contentArea.height()));

    painter.restore();
  } else if (m_activeTab == Tab::Settings) {
    drawSettingsPage(painter, contentArea);
  } else if (m_activeTab == Tab::Radial) {
    drawRadialPage(painter, contentArea);
  }
}

// ============================================================================
// Tab Bar
// ============================================================================

void OverlayMenuWidget::drawTabBar(QPainter &painter, const QRectF &tabArea) {
  const auto &tok = overlayTokens();

  qreal thirdWidth = tabArea.width() / 3.0;
  m_packsTabRect =
      QRectF(tabArea.left(), tabArea.top(), thirdWidth, tabArea.height());
  m_settingsTabRect = QRectF(tabArea.left() + thirdWidth, tabArea.top(),
                             thirdWidth, tabArea.height());
  m_radialTabRect = QRectF(tabArea.left() + thirdWidth * 2, tabArea.top(),
                           thirdWidth, tabArea.height());

  QFont tabFont("Segoe UI", 9, QFont::DemiBold);
  painter.setFont(tabFont);

  // Helper to draw a single tab
  auto drawTab = [&](const QRectF &rect, Tab tab, const QString &label) {
    if (m_activeTab == tab) {
      painter.fillRect(rect, QColor(tok.headerBg));
      painter.setPen(QColor(tok.panelBorder));
    } else {
      painter.setPen(QColor(tok.textSecondary));
    }
    painter.drawText(rect, Qt::AlignCenter, label);
  };

  drawTab(m_packsTabRect, Tab::Packs, "Packs");
  drawTab(m_settingsTabRect, Tab::Settings, "Settings");
  drawTab(m_radialTabRect, Tab::Radial, "Radial");

  // Active tab underline indicator
  QRectF activeRect;
  if (m_activeTab == Tab::Packs)
    activeRect = m_packsTabRect;
  else if (m_activeTab == Tab::Settings)
    activeRect = m_settingsTabRect;
  else
    activeRect = m_radialTabRect;

  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(tok.panelBorder));
  painter.drawRect(QRectF(activeRect.left() + 8, activeRect.bottom() - 2,
                          activeRect.width() - 16, 2));

  // Bottom separator
  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(tabArea.left() + 4, tabArea.bottom()),
                   QPointF(tabArea.right() - 4, tabArea.bottom()));
}

// ============================================================================
// Settings Page
// ============================================================================

void OverlayMenuWidget::drawSettingsPage(QPainter &painter,
                                         const QRectF &contentArea) {
  const auto &tok = overlayTokens();
  painter.save();
  painter.setClipRect(contentArea);

  // Apply settings scroll offset
  painter.translate(0, -m_settingsScrollOffset);

  qreal y = contentArea.top() + 12;
  qreal labelX = contentArea.left() + 12;
  qreal sliderX = contentArea.left() + 12;
  qreal sliderW = contentArea.width() - 24;

  QFont labelFont("Segoe UI", 9, QFont::DemiBold);
  QFont valueFont("Segoe UI", 8);
  QFont sectionFont("Segoe UI", 10, QFont::Bold);

  // Helper lambda to draw a toggle row (label + circle on right)
  qreal distToggleSize = 14;
  auto drawToggleRow = [&](const QString &label, bool enabled, QRectF &hitRect) {
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX + 4, y + 13), label);

    hitRect =
        QRectF(contentArea.right() - distToggleSize - 16,
               y + (20 - distToggleSize) / 2.0, distToggleSize, distToggleSize);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(enabled ? tok.toggleOn : tok.toggleOff));
    painter.drawEllipse(hitRect);

    if (enabled) {
      painter.setPen(QPen(Qt::white, 1.5));
      QPointF p1(hitRect.left() + 3, hitRect.center().y());
      QPointF p2(hitRect.center().x() - 1, hitRect.bottom() - 3);
      QPointF p3(hitRect.right() - 3, hitRect.top() + 3);
      painter.drawLine(p1, p2);
      painter.drawLine(p2, p3);
    }
    y += 22;
  };

  // --- Rendering Layer Toggles (section header) ---
  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QPointF(labelX, y + 12), "General Rendering");
  y += 22;

  bool mainOn = m_markerSettings ? m_markerSettings->renderingEnabled() : true;
  {

    drawToggleRow("Markers & Trails", mainOn, m_mainRenderToggleRect);

    // Sub-toggles only visible when Main is ON
    if (mainOn) {
      bool show3d =
          m_markerSettings ? m_markerSettings->render3dEnabled() : true;
      bool showMap =
          m_markerSettings ? m_markerSettings->renderMapEnabled() : true;
      bool showMinimap =
          m_markerSettings ? m_markerSettings->renderMinimapEnabled() : true;
      bool showBigMap =
          m_markerSettings ? m_markerSettings->renderBigMapEnabled() : true;

      drawToggleRow("  3D World", show3d, m_3dRenderToggleRect);
      drawToggleRow("  Map Markers", showMap, m_mapRenderToggleRect);

      // Sub-toggles under Map Markers — double-indented, greyed when parent OFF
      if (showMap) {
        drawToggleRow("      Minimap", showMinimap, m_minimapRenderToggleRect);
        drawToggleRow("      Big Map (M)", showBigMap, m_bigMapRenderToggleRect);
      } else {
        // Greyed-out sub-toggles (draw but non-interactive)
        painter.save();
        painter.setOpacity(painter.opacity() * 0.4);
        drawToggleRow("      Minimap", showMinimap, m_minimapRenderToggleRect);
        drawToggleRow("      Big Map (M)", showBigMap, m_bigMapRenderToggleRect);
        painter.restore();
        // Clear hit rects so clicks don't register
        m_minimapRenderToggleRect = QRectF();
        m_bigMapRenderToggleRect = QRectF();
      }

      drawToggleRow("  Radial Menu", m_radialEnabled, m_radialToggleRect);
    } else {
      m_3dRenderToggleRect = QRectF();
      m_mapRenderToggleRect = QRectF();
      m_minimapRenderToggleRect = QRectF();
      m_bigMapRenderToggleRect = QRectF();
      m_radialToggleRect = QRectF();
    }
  }

  // --- Everything below hidden when master is OFF (grandfather toggle) ---
  if (mainOn) {

  // --- Section separator ---
  y += 4;
  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(labelX, y), QPointF(contentArea.right() - 12, y));
  y += 8;

  // --- 3D World section title ---
  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QPointF(labelX, y + 14), "3D World");
  y += 22;

  // --- Overlay Opacity ---
  painter.setFont(labelFont);
  painter.setPen(QColor(tok.textPrimary));
  painter.drawText(QPointF(labelX, y + 12), "Opacity");

  // Value text (right-aligned)
  qreal overlayVal =
      m_markerSettings ? m_markerSettings->overlayOpacity() : 1.0;
  painter.setFont(valueFont);
  painter.setPen(QColor(tok.textSecondary));
  QString overlayPct = QString::number(qRound(overlayVal * 100)) + "%";
  painter.drawText(QRectF(sliderX, y, sliderW, 16),
                   Qt::AlignRight | Qt::AlignVCenter, overlayPct);

  y += 20;
  m_overlaySliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
  drawSlider(painter, m_overlaySliderRect, overlayVal, QString());
  y += kSliderHeight;

  // --- Marker Scale slider ---
  y += 4;
  {
    qreal scaleVal = m_markerSettings ? m_markerSettings->markerScale() : 1.0;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Marker Scale");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString scaleStr = QString::number(static_cast<double>(scaleVal), 'f', 1) +
                       QString::fromUtf8("\xc3\x97");
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, scaleStr);

    y += 20;
    qreal scaleSliderVal = (scaleVal - 0.5) / 2.5;
    m_markerScaleSliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_markerScaleSliderRect, scaleSliderVal, QString());
    y += kSliderHeight;
  }

  // --- Render Distance slider ---
  y += 4;
  {
    qreal renderDistVal =
        m_markerSettings ? m_markerSettings->maxRenderDistance() : 200.0;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Render Distance");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString distStr = QString::number(qRound(renderDistVal)) + "m";
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, distStr);

    y += 20;
    qreal distSliderVal = (renderDistVal - 50.0) / 450.0;
    m_renderDistSliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_renderDistSliderRect, distSliderVal, QString());
    y += kSliderHeight;
  }

  // --- Show Distance toggle ---
  y += 8;
  bool distEnabled =
      m_markerSettings ? m_markerSettings->showDistance() : false;
  drawToggleRow("Show Distance", distEnabled, m_distanceToggleRect);

  // --- Distance Font Size slider (only when Show Distance is on) ---
  if (distEnabled) {
    y += 4;
    int fontSizeVal =
        m_markerSettings ? m_markerSettings->distanceFontSize() : 12;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Font Size");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString sizeStr = QString::number(fontSizeVal) + "px";
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, sizeStr);

    y += 20;
    qreal fontSliderVal = (fontSizeVal - 8.0) / 16.0;
    m_distFontSliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_distFontSliderRect, fontSliderVal, QString());
    y += kSliderHeight;

    // --- Label Offset slider ---
    y += 4;
    int labelOffsetVal =
        m_markerSettings ? m_markerSettings->distanceLabelOffset() : 20;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Label Offset");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString offsetStr = QString::number(labelOffsetVal) + "px";
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, offsetStr);

    y += 20;
    qreal offsetSliderVal = labelOffsetVal / 50.0;
    m_distLabelOffsetSliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_distLabelOffsetSliderRect, offsetSliderVal,
               QString());
    y += kSliderHeight;
  } else {
    m_distFontSliderRect = QRectF();
    m_distLabelOffsetSliderRect = QRectF();
  }

  // --- Height Filter toggle ---
  y += 8;
  bool hfEnabled =
      m_markerSettings ? m_markerSettings->heightFilterEnabled() : true;
  drawToggleRow("Height Filter", hfEnabled, m_heightFilterToggleRect);

  // --- Height Range slider (only when filter is on) ---
  if (hfEnabled) {
    y += 4;
    float hfRange =
        m_markerSettings ? m_markerSettings->heightFilterRange() : 20.0f;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Height Range");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString hfStr = QString::number(qRound(hfRange)) + "m";
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, hfStr);

    y += 20;
    qreal hfSliderVal = (hfRange - 5.0) / 45.0;
    m_heightRangeSliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_heightRangeSliderRect, hfSliderVal, QString());
    y += kSliderHeight;
  } else {
    m_heightRangeSliderRect = QRectF();
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // Minimap / Map section
  // ═══════════════════════════════════════════════════════════════════════════
  y += 8;
  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(labelX, y), QPointF(contentArea.right() - 12, y));
  y += 8;

  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QPointF(labelX, y + 14), "Map");
  y += 22;

  // --- Trail Opacity slider ---
  {
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Trail Opacity");

    qreal minimapVal =
        m_markerSettings ? m_markerSettings->minimapOpacity() : 0.8;
    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString minimapPct = QString::number(qRound(minimapVal * 100)) + "%";
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, minimapPct);

    y += 20;
    m_minimapSliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_minimapSliderRect, minimapVal, QString());
    y += kSliderHeight;
  }

  // --- Trail Width slider ---
  y += 4;
  {
    float twVal =
        m_markerSettings ? m_markerSettings->minimapTrailWidth() : 1.0f;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Trail Width");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString twStr = QString::number(static_cast<double>(twVal), 'f', 1) +
                    QString::fromUtf8("\xc3\x97");
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, twStr);

    y += 20;
    qreal twSliderVal = (twVal - 1.0) / 9.0;
    m_trailWidthSliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_trailWidthSliderRect, twSliderVal, QString());
    y += kSliderHeight;
  }

  // --- Marker Size slider (minimap/bigmap) ---
  y += 4;
  {
    qreal mmScale =
        m_markerSettings ? m_markerSettings->minimapMarkerScale() : 1.0;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Marker Size");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString mmStr =
        QString::number(static_cast<double>(mmScale), 'f', 1) +
        QString::fromUtf8("\xc3\x97");
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, mmStr);

    y += 20;
    qreal mmSliderVal = (mmScale - 0.5) / 2.5;
    m_minimapMarkerScaleSliderRect =
        QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_minimapMarkerScaleSliderRect, mmSliderVal,
               QString());
    y += kSliderHeight;
  }

  // --- Marker Opacity slider (minimap/bigmap) ---
  y += 4;
  {
    qreal mmOpacity =
        m_markerSettings ? m_markerSettings->minimapMarkerOpacity() : 1.0;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Marker Opacity");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString moStr = QString::number(qRound(mmOpacity * 100)) + "%";
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, moStr);

    y += 20;
    m_minimapMarkerOpacitySliderRect =
        QRectF(sliderX, y, sliderW, kSliderHeight);
    drawSlider(painter, m_minimapMarkerOpacitySliderRect, mmOpacity,
               QString());
    y += kSliderHeight;
  }

  // =========================================================================
  // Exclusion Zones Section
  // =========================================================================
  y += 8;

  // Section separator
  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(contentArea.left() + 8, y),
                   QPointF(contentArea.right() - 8, y));
  y += 8;

  // Section title
  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QPointF(labelX, y + 14), "Exclusion Zones");
  y += 22;


  // Master toggle
  bool exEnabled =
      m_markerSettings ? m_markerSettings->exclusionEnabled() : true;
  drawToggleRow("Enabled", exEnabled, m_exclusionToggleRect);

  // Individual zone toggles (dimmed if master is off)
  if (exEnabled) {
    bool mmOn =
        m_markerSettings ? m_markerSettings->minimapZoneEnabled() : true;
    drawToggleRow("  Minimap", mmOn, m_minimapToggleRect);

    bool sbOn =
        m_markerSettings ? m_markerSettings->skillBarZoneEnabled() : true;
    drawToggleRow("  Skill Bar", sbOn, m_skillBarToggleRect);

    bool chOn = m_markerSettings ? m_markerSettings->chatZoneEnabled() : true;
    drawToggleRow("  Chat", chOn, m_chatToggleRect);

    // --- Fade Edge Slider ---
    y += 4;
    float fadeVal =
        m_markerSettings ? m_markerSettings->exclusionFadeEdge() : 0.02f;
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QPointF(labelX, y + 12), "Fade Edge");

    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    // Display as percentage (0.02 -> "2%", 0.05 -> "5%")
    QString fadePct = QString::number(qRound(fadeVal * 100.0f)) + "%";
    painter.drawText(QRectF(sliderX, y, sliderW, 16),
                     Qt::AlignRight | Qt::AlignVCenter, fadePct);

    y += 20;
    m_fadeEdgeSliderRect = QRectF(sliderX, y, sliderW, kSliderHeight);
    // Normalize fadeEdge to 0..1 for slider (0 maps to 0%, 0.05 maps to 100%)
    drawSlider(painter, m_fadeEdgeSliderRect,
               static_cast<qreal>(fadeVal) / 0.05, QString());

    // --- Edit Custom Zones Button ---
    y += kSliderHeight + 12;
    m_editZonesButtonRect = QRectF(sliderX, y, sliderW, 26);

    bool btnHovered =
        m_editZonesButtonRect.contains(mapFromGlobal(QCursor::pos()));
    painter.setBrush(QColor(btnHovered ? tok.headerBg : tok.itemHoverBg));
    painter.setPen(QPen(QColor(tok.panelBorder), 1.0));
    painter.drawRoundedRect(m_editZonesButtonRect, 4, 4);

    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(m_editZonesButtonRect, Qt::AlignCenter,
                     "Edit Custom Zones");
    y += 28; // Advance past the button (26px height + 2px gap)
  } else {
    // Clear toggle rects when section is hidden
    m_minimapToggleRect = QRectF();
    m_skillBarToggleRect = QRectF();
    m_chatToggleRect = QRectF();
    m_fadeEdgeSliderRect = QRectF();
    m_editZonesButtonRect = QRectF();
  }

  } else {
    // Master OFF — clear all hit rects so nothing is clickable
    m_overlaySliderRect = QRectF();
    m_markerScaleSliderRect = QRectF();
    m_renderDistSliderRect = QRectF();
    m_distanceToggleRect = QRectF();
    m_distFontSliderRect = QRectF();
    m_distLabelOffsetSliderRect = QRectF();
    m_heightFilterToggleRect = QRectF();
    m_heightRangeSliderRect = QRectF();
    m_minimapSliderRect = QRectF();
    m_trailWidthSliderRect = QRectF();
    m_minimapMarkerScaleSliderRect = QRectF();
    m_minimapMarkerOpacitySliderRect = QRectF();
    m_exclusionToggleRect = QRectF();
    m_minimapToggleRect = QRectF();
    m_skillBarToggleRect = QRectF();
    m_chatToggleRect = QRectF();
    m_fadeEdgeSliderRect = QRectF();
    m_editZonesButtonRect = QRectF();
  }

  // =========================================================================
  // Overlay Section (panel behavior, not rendering)
  // =========================================================================

  y += 16;

  // Section separator
  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(contentArea.left() + 8, y),
                   QPointF(contentArea.right() - 8, y));
  y += 8;

  // Section title
  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QPointF(labelX, y + 14), "Overlay");
  y += 24;

  {
    bool combatHide =
        m_markerSettings ? m_markerSettings->hideInCombat() : false;
    drawToggleRow("Hide in Combat", combatHide, m_combatToggleRect);

    bool showBigMap =
        m_markerSettings ? m_markerSettings->showInBigMap() : true;
    drawToggleRow("Show in Big Map", showBigMap, m_showInBigMapToggleRect);
  }

  // =========================================================================
  // Details Tracker Section
  // =========================================================================

  y += 16;

  // Section separator
  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(contentArea.left() + 8, y),
                   QPointF(contentArea.right() - 8, y));
  y += 8;

  // Section title
  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QPointF(labelX, y + 14), "Tools");
  y += 24;

  // Details Tracker button
  m_detailsTrackerButtonRect = QRectF(sliderX, y, sliderW, 26);
  bool dtBtnHovered =
      m_detailsTrackerButtonRect.contains(mapFromGlobal(QCursor::pos()));
  painter.setBrush(QColor(dtBtnHovered ? tok.headerBg : tok.itemHoverBg));
  painter.setPen(QPen(
      QColor(m_detailsTrackerVisible ? tok.toggleOn : tok.panelBorder), 1.0));
  painter.drawRoundedRect(m_detailsTrackerButtonRect, 4, 4);

  painter.setFont(labelFont);
  painter.setPen(QColor(tok.textPrimary));
  painter.drawText(m_detailsTrackerButtonRect, Qt::AlignCenter,
                   m_detailsTrackerVisible ? "Hide Details Tracker"
                                           : "Show Details Tracker");

  // Compute max scroll based on total content height vs visible area
  qreal totalContentHeight = y - contentArea.top() + 12; // 12 = bottom padding
  qreal visibleHeight = contentArea.height();
  m_settingsMaxScroll = qMax(0, static_cast<int>(totalContentHeight - visibleHeight));

  painter.restore();
}

// ============================================================================
// Radial Settings Page
// ============================================================================

void OverlayMenuWidget::drawRadialPage(QPainter &painter,
                                       const QRectF &contentArea) {
  const auto &tok = overlayTokens();
  painter.save();
  painter.setClipRect(contentArea);

  // Apply scroll offset
  painter.translate(0, -m_radialScrollOffset);

  qreal x = contentArea.left() + 12;
  qreal y = contentArea.top() + 8;
  qreal w = contentArea.width() - 24;

  QFont labelFont("Segoe UI", 9, QFont::DemiBold);
  QFont valueFont("Segoe UI", 8);
  QFont sectionFont("Segoe UI", 10, QFont::Bold);

  // -- Toggle row lambda (same pattern as Settings page) --
  auto drawToggleRow = [&](const QString &label, bool enabled,
                           QRectF &toggleRect) {
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QRectF(x, y, w - 24, kItemHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, label);
    toggleRect = QRectF(x + w - 20, y + (kItemHeight - 16) / 2.0, 16, 16);
    drawToggleIndicator(painter, toggleRect, enabled);
    y += kItemHeight;
  };

  // ==========================
  // Mount Wheel Section
  // ==========================
  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QRectF(x, y, w, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   "Mount Wheel");
  y += 24;

  // Section separator
  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(x, y), QPointF(x + w, y));
  y += 6;

  // Mount wheel master toggle
  drawToggleRow("Enable Mount Wheel", m_radialSettings.mountWheelEnabled,
                m_mountWheelToggleRect);

  // Per-mount toggles
  if (m_radialSettings.mountWheelEnabled) {
    const QStringList mountOrder = {"raptor",  "springer", "skimmer",
                                    "jackal",  "griffon",  "beetle",
                                    "warclaw",  "skyscale", "turtle",
                                    "skiff"};
    const QMap<QString, QString> mountNames = {
        {"raptor", "Raptor"},     {"springer", "Springer"},
        {"skimmer", "Skimmer"},   {"jackal", "Jackal"},
        {"griffon", "Griffon"},   {"beetle", "Roller Beetle"},
        {"warclaw", "Warclaw"},   {"skyscale", "Skyscale"},
        {"turtle", "Siege Turtle"}, {"skiff", "Skiff"}};

    int mountIdx = 0;
    for (const auto &key : mountOrder) {
      if (!m_radialSettings.mounts.contains(key))
        continue;
      bool enabled = m_radialSettings.mounts[key].enabled;
      QString label = "  " + mountNames.value(key, key);

      // Ensure enough toggle rects
      if (mountIdx >= m_mountToggleRects.size())
        m_mountToggleRects.resize(mountIdx + 1);

      drawToggleRow(label, enabled, m_mountToggleRects[mountIdx]);
      ++mountIdx;
    }
  }

  y += 8;

  // ==========================
  // Display Section
  // ==========================
  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QRectF(x, y, w, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   "Display");
  y += 24;

  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(x, y), QPointF(x + w, y));
  y += 6;

  // Wheel Scale slider (0.5 - 2.0)
  {
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QRectF(x, y, w * 0.5, kItemHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, "Wheel Scale");
    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    QString valStr = QString::number(m_radialSettings.wheelScale, 'f', 2);
    painter.drawText(QRectF(x + w * 0.5, y, w * 0.5, kItemHeight),
                     Qt::AlignRight | Qt::AlignVCenter, valStr);
    y += kItemHeight;

    m_radialScaleSliderRect = QRectF(x, y, w, kSliderHeight);
    qreal normalized =
        (m_radialSettings.wheelScale - 0.5) / 1.5; // 0.5-2.0 → 0-1
    drawSlider(painter, m_radialScaleSliderRect, normalized, "");
    y += kSliderHeight + 8;
  }

  // Opacity slider (0.0 - 1.0)
  {
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QRectF(x, y, w * 0.5, kItemHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, "Opacity");
    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    int pct = static_cast<int>(m_radialSettings.opacity * 100);
    painter.drawText(QRectF(x + w * 0.5, y, w * 0.5, kItemHeight),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(pct) + "%");
    y += kItemHeight;

    m_radialOpacitySliderRect = QRectF(x, y, w, kSliderHeight);
    drawSlider(painter, m_radialOpacitySliderRect, m_radialSettings.opacity,
               "");
    y += kSliderHeight + 8;
  }

  // Animation Time slider (50 - 500ms)
  {
    painter.setFont(labelFont);
    painter.setPen(QColor(tok.textPrimary));
    painter.drawText(QRectF(x, y, w * 0.5, kItemHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, "Animation");
    painter.setFont(valueFont);
    painter.setPen(QColor(tok.textSecondary));
    painter.drawText(
        QRectF(x + w * 0.5, y, w * 0.5, kItemHeight),
        Qt::AlignRight | Qt::AlignVCenter,
        QString::number(m_radialSettings.animationTimeMs) + "ms");
    y += kItemHeight;

    m_radialAnimSliderRect = QRectF(x, y, w, kSliderHeight);
    qreal normalized =
        static_cast<qreal>(m_radialSettings.animationTimeMs - 50) / 450.0;
    drawSlider(painter, m_radialAnimSliderRect, qBound(0.0, normalized, 1.0),
               "");
    y += kSliderHeight + 8;
  }

  y += 4;

  // ==========================
  // Interaction Section
  // ==========================
  painter.setFont(sectionFont);
  painter.setPen(QColor(tok.panelBorder));
  painter.drawText(QRectF(x, y, w, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   "Interaction");
  y += 24;

  painter.setPen(QPen(QColor(tok.panelBorder), 0.5));
  painter.drawLine(QPointF(x, y), QPointF(x + w, y));
  y += 6;

  drawToggleRow("No-Hold Mode", m_radialSettings.noHoldMode,
                m_noHoldToggleRect);
  drawToggleRow("Reset Cursor", m_radialSettings.resetCursorAfterKeybind,
                m_resetCursorToggleRect);
  drawToggleRow("Fast Mount Swap", m_radialSettings.fastMountSwap,
                m_fastMountSwapToggleRect);

  // Compute max scroll
  qreal totalContentHeight = y - contentArea.top() + 12;
  qreal visibleHeight = contentArea.height();
  m_radialMaxScroll =
      qMax(0, static_cast<int>(totalContentHeight - visibleHeight));

  painter.restore();
}

void OverlayMenuWidget::drawSlider(QPainter &painter, const QRectF &sliderRect,
                                   qreal value, const QString &label) {
  Q_UNUSED(label);
  const auto &tok = overlayTokens();

  // Track background
  qreal trackY = sliderRect.center().y() - kSliderTrackHeight / 2.0;
  QRectF trackRect(sliderRect.left(), trackY, sliderRect.width(),
                   kSliderTrackHeight);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(tok.toggleOff));
  painter.drawRoundedRect(trackRect, 3, 3);

  // Filled portion
  qreal fillWidth = sliderRect.width() * qBound(0.0, value, 1.0);
  QRectF fillRect(trackRect.left(), trackY, fillWidth, kSliderTrackHeight);
  painter.setBrush(QColor(tok.panelBorder));
  painter.drawRoundedRect(fillRect, 3, 3);

  // Thumb
  qreal thumbX = sliderRect.left() + fillWidth;
  qreal thumbRadius = 7;
  painter.setBrush(QColor(tok.panelBorder));
  painter.setPen(QPen(QColor(tok.textPrimary), 1));
  painter.drawEllipse(QPointF(thumbX, sliderRect.center().y()), thumbRadius,
                      thumbRadius);
}

// ============================================================================
// Tree Item Rendering
// ============================================================================

void OverlayMenuWidget::drawTreeItem(QPainter &painter,
                                     const OverlayMenuItem &item, int &yOffset,
                                     int visibleIndex) {
  const auto &tok = overlayTokens();
  QRectF itemRect(m_panelRect.left(), yOffset, kPanelWidth, kItemHeight);

  // Pack header: subtle background tint for visual distinction
  if (item.isPack) {
    painter.fillRect(itemRect, QColor(tok.headerBg));
  }

  // Hover highlight
  if (visibleIndex == m_hoverIndex) {
    painter.fillRect(itemRect, QColor(tok.itemHoverBg));
  }

  // Indentation (base 22px gives room for expander arrow at depth 0)
  int indent = kIndentStep * item.depth + 22;

  // Expander arrow (for items with children)
  if (!item.children.isEmpty()) {
    constexpr int arrowSize = 10;
    QRectF expanderRect(m_panelRect.left() + indent - 16,
                        yOffset + kItemHeight / 2.0 - arrowSize / 2.0,
                        arrowSize, arrowSize);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(tok.expanderColor));

    QPainterPath arrow;
    if (item.isExpanded) {
      // Down arrow
      arrow.moveTo(expanderRect.left(), expanderRect.top());
      arrow.lineTo(expanderRect.right(), expanderRect.top());
      arrow.lineTo(expanderRect.center().x(), expanderRect.bottom());
    } else {
      // Right arrow
      arrow.moveTo(expanderRect.left(), expanderRect.top());
      arrow.lineTo(expanderRect.right(), expanderRect.center().y());
      arrow.lineTo(expanderRect.left(), expanderRect.bottom());
    }
    arrow.closeSubpath();
    painter.drawPath(arrow);
  }

  // Toggle indicator (right side) — only for items that have content
  bool showToggle = !item.isSeparator && item.hasContent;
  if (showToggle) {
    QRectF toggleRect(m_panelRect.right() - kToggleWidth - 10,
                      yOffset + (kItemHeight - kToggleWidth) / 2.0, kToggleWidth,
                      kToggleWidth);
    drawToggleIndicator(painter, toggleRect, item.isEnabled);
  }

  // Label text
  if (item.isSeparator) {
    // Separator: bold gold label (section header — stands out as decoration)
    painter.setFont(QFont("Segoe UI", 8, QFont::Bold));
    painter.setPen(QColor(tok.panelBorder)); // Gold
  } else if (!item.hasContent) {
    // Content-less info category: dimmed label
    painter.setFont(QFont("Segoe UI", 9));
    painter.setPen(QColor(tok.textSecondary));
  } else {
    // Normal category or pack
    painter.setFont(item.isPack ? QFont("Segoe UI", 10, QFont::DemiBold)
                                : QFont("Segoe UI", 9));
    painter.setPen(QColor(item.isEnabled ? tok.textPrimary : tok.textSecondary));
  }

  // Full width for items without toggle, reduced width for toggle items
  int rightMargin = showToggle ? (kToggleWidth + 20) : 10;
  QRectF textRect(m_panelRect.left() + indent, yOffset,
                  kPanelWidth - indent - rightMargin, kItemHeight);
  painter.drawText(textRect, Qt::AlignVCenter | Qt::TextSingleLine, item.label);
}

void OverlayMenuWidget::drawToggleIndicator(QPainter &painter,
                                            const QRectF &rect, bool enabled) {
  const auto &tok = overlayTokens();
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(enabled ? tok.toggleOn : tok.toggleOff));
  painter.drawEllipse(rect);

  // Checkmark or X
  painter.setPen(QPen(Qt::white, 1.5));
  if (enabled) {
    // Checkmark
    QPointF p1(rect.left() + 3, rect.center().y());
    QPointF p2(rect.center().x() - 1, rect.bottom() - 3);
    QPointF p3(rect.right() - 3, rect.top() + 3);
    painter.drawLine(p1, p2);
    painter.drawLine(p2, p3);
  }
}

// ============================================================================
// Mouse Events
// ============================================================================

void OverlayMenuWidget::mousePressEvent(QMouseEvent *event) {
  QPointF pos = event->position();

  // Icon click — toggle menu
  if (m_iconRect.contains(pos)) {
    setMenuOpen(!m_isMenuOpen);
    return;
  }

  // Panel interactions
  if (m_isMenuOpen && m_panelRect.contains(pos)) {
    // Close button
    if (m_closeButtonRect.isValid() &&
        m_closeButtonRect.adjusted(-4, -4, 4, 4).contains(pos)) {
      setMenuOpen(false);
      return;
    }

    // Panel header drag
    if (m_panelHeaderRect.contains(pos)) {
      m_isDraggingPanel = true;
      m_panelDragStart = pos - m_panelRect.topLeft();
      return;
    }

    // Tab bar clicks
    if (m_packsTabRect.contains(pos)) {
      if (m_activeTab != Tab::Packs) {
        m_activeTab = Tab::Packs;
        m_scrollOffset = 0;
        update();
      }
      return;
    }
    if (m_settingsTabRect.contains(pos)) {
      if (m_activeTab != Tab::Settings) {
        m_activeTab = Tab::Settings;
        update();
      }
      return;
    }
    if (m_radialTabRect.contains(pos)) {
      if (m_activeTab != Tab::Radial) {
        m_activeTab = Tab::Radial;
        update();
      }
      return;
    }

    // Settings page slider interactions
    // Hit-test rects are in scrolled (translated) coordinates,
    // so offset the mouse Y by the settings scroll offset.
    if (m_activeTab == Tab::Settings) {
      QPointF scrolledPos(pos.x(), pos.y() + m_settingsScrollOffset);
      if (m_overlaySliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 0;
        qreal value = qBound(0.0,
                             (pos.x() - m_overlaySliderRect.left()) /
                                 m_overlaySliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setOverlayOpacity(value);
        }
        update();
        return;
      }
      if (m_minimapSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 1;
        qreal value = qBound(0.0,
                             (pos.x() - m_minimapSliderRect.left()) /
                                 m_minimapSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setMinimapOpacity(value);
        }
        update();
        return;
      }

      // Rendering layer toggles
      if (m_mainRenderToggleRect.isValid() &&
          m_mainRenderToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setRenderingEnabled(
              !m_markerSettings->renderingEnabled());
        }
        update();
        return;
      }
      if (m_3dRenderToggleRect.isValid() &&
          m_3dRenderToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setRender3dEnabled(
              !m_markerSettings->render3dEnabled());
        }
        update();
        return;
      }
      if (m_mapRenderToggleRect.isValid() &&
          m_mapRenderToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setRenderMapEnabled(
              !m_markerSettings->renderMapEnabled());
        }
        update();
        return;
      }
      if (m_minimapRenderToggleRect.isValid() &&
          m_minimapRenderToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setRenderMinimapEnabled(
              !m_markerSettings->renderMinimapEnabled());
        }
        update();
        return;
      }
      if (m_bigMapRenderToggleRect.isValid() &&
          m_bigMapRenderToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setRenderBigMapEnabled(
              !m_markerSettings->renderBigMapEnabled());
        }
        update();
        return;
      }
      if (m_radialToggleRect.isValid() &&
          m_radialToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        m_radialEnabled = !m_radialEnabled;
        emit radialToggleChanged(m_radialEnabled);
        update();
        return;
      }

      // Show Distance toggle
      if (m_distanceToggleRect.isValid() &&
          m_distanceToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setShowDistance(!m_markerSettings->showDistance());
        }
        update();
        return;
      }

      // Hide in Combat toggle
      if (m_combatToggleRect.isValid() &&
          m_combatToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setHideInCombat(!m_markerSettings->hideInCombat());
        }
        update();
        return;
      }

      // Show in Big Map toggle (sub-toggle of Hide in Combat)
      if (m_showInBigMapToggleRect.isValid() &&
          m_showInBigMapToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setShowInBigMap(
              !m_markerSettings->showInBigMap());
        }
        update();
        return;
      }

      // Exclusion zone toggles
      if (m_exclusionToggleRect.isValid() &&
          m_exclusionToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setExclusionEnabled(
              !m_markerSettings->exclusionEnabled());
        }
        update();
        return;
      }
      if (m_minimapToggleRect.isValid() &&
          m_minimapToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setMinimapZoneEnabled(
              !m_markerSettings->minimapZoneEnabled());
        }
        update();
        return;
      }
      if (m_skillBarToggleRect.isValid() &&
          m_skillBarToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setSkillBarZoneEnabled(
              !m_markerSettings->skillBarZoneEnabled());
        }
        update();
        return;
      }
      if (m_chatToggleRect.isValid() &&
          m_chatToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setChatZoneEnabled(
              !m_markerSettings->chatZoneEnabled());
        }
        update();
        return;
      }

      // Fade edge slider
      if (m_fadeEdgeSliderRect.isValid() &&
          m_fadeEdgeSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 2;
        qreal value = qBound(0.0,
                             (pos.x() - m_fadeEdgeSliderRect.left()) /
                                 m_fadeEdgeSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setExclusionFadeEdge(
              static_cast<float>(value * 0.05));
        }
        update();
        return;
      }

      // Font size slider (only when distance is enabled)
      if (m_distFontSliderRect.isValid() &&
          m_distFontSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 3;
        qreal value = qBound(0.0,
                             (pos.x() - m_distFontSliderRect.left()) /
                                 m_distFontSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setDistanceFontSize(
              8 + static_cast<int>(qRound(value * 16.0)));
        }
        update();
        return;
      }

      // Label Offset slider (only when distance is enabled)
      if (m_distLabelOffsetSliderRect.isValid() &&
          m_distLabelOffsetSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 5;
        qreal value = qBound(0.0,
                             (pos.x() - m_distLabelOffsetSliderRect.left()) /
                                 m_distLabelOffsetSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setDistanceLabelOffset(
              static_cast<int>(qRound(value * 50.0)));
        }
        update();
        return;
      }

      // Marker Scale slider
      if (m_markerScaleSliderRect.isValid() &&
          m_markerScaleSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 4;
        qreal value = qBound(0.0,
                             (pos.x() - m_markerScaleSliderRect.left()) /
                                 m_markerScaleSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setMarkerScale(0.5 + value * 2.5);
        }
        update();
        return;
      }

      // Render Distance slider
      if (m_renderDistSliderRect.isValid() &&
          m_renderDistSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 6;
        qreal value = qBound(0.0,
                             (pos.x() - m_renderDistSliderRect.left()) /
                                 m_renderDistSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setMaxRenderDistance(50.0 + value * 450.0);
        }
        update();
        return;
      }

      // Height Filter toggle
      if (m_heightFilterToggleRect.isValid() &&
          m_heightFilterToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        if (m_markerSettings) {
          m_markerSettings->setHeightFilterEnabled(
              !m_markerSettings->heightFilterEnabled());
        }
        update();
        return;
      }

      // Height Range slider
      if (m_heightRangeSliderRect.isValid() &&
          m_heightRangeSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 7;
        qreal value = qBound(0.0,
                             (pos.x() - m_heightRangeSliderRect.left()) /
                                 m_heightRangeSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setHeightFilterRange(
              static_cast<float>(5.0 + value * 45.0));
        }
        update();
        return;
      }

      // Trail Width slider
      if (m_trailWidthSliderRect.isValid() &&
          m_trailWidthSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 8;
        qreal value = qBound(0.0,
                             (pos.x() - m_trailWidthSliderRect.left()) /
                                 m_trailWidthSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setMinimapTrailWidth(
              static_cast<float>(1.0 + value * 9.0));
        }
        update();
        return;
      }

      // Minimap Marker Size slider
      if (m_minimapMarkerScaleSliderRect.isValid() &&
          m_minimapMarkerScaleSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 9;
        qreal value = qBound(0.0,
                             (pos.x() - m_minimapMarkerScaleSliderRect.left()) /
                                 m_minimapMarkerScaleSliderRect.width(),
                             1.0);
        if (m_markerSettings) {
          m_markerSettings->setMinimapMarkerScale(0.5 + value * 2.5);
        }
        update();
        return;
      }

      // Minimap Marker Opacity slider
      if (m_minimapMarkerOpacitySliderRect.isValid() &&
          m_minimapMarkerOpacitySliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 10;
        qreal value =
            qBound(0.0,
                   (pos.x() - m_minimapMarkerOpacitySliderRect.left()) /
                       m_minimapMarkerOpacitySliderRect.width(),
                   1.0);
        if (m_markerSettings) {
          m_markerSettings->setMinimapMarkerOpacity(value);
        }
        update();
        return;
      }

      // Edit Custom Zones button
      if (m_editZonesButtonRect.isValid() &&
          m_editZonesButtonRect.contains(scrolledPos)) {
        emit editExclusionZonesRequested();
        update();
        return;
      }

      // Details Tracker button
      if (m_detailsTrackerButtonRect.isValid() &&
          m_detailsTrackerButtonRect.contains(scrolledPos)) {
        m_detailsTrackerVisible = !m_detailsTrackerVisible;
        emit detailsTrackerToggled(m_detailsTrackerVisible);
        update();
        return;
      }

      return; // Consume click in settings area
    }

    // Radial tab: toggle and slider interactions
    if (m_activeTab == Tab::Radial) {
      QPointF scrolledPos(pos.x(), pos.y() + m_radialScrollOffset);

      // Mount wheel master toggle
      if (m_mountWheelToggleRect.isValid() &&
          m_mountWheelToggleRect.adjusted(-8, -4, 8, 4)
              .contains(scrolledPos)) {
        m_radialSettings.mountWheelEnabled =
            !m_radialSettings.mountWheelEnabled;
        emit radialSettingsChanged(m_radialSettings);
        update();
        return;
      }

      // Per-mount toggles
      const QStringList mountOrder = {"raptor",  "springer", "skimmer",
                                      "jackal",  "griffon",  "beetle",
                                      "warclaw", "skyscale", "turtle",
                                      "skiff"};
      int mountIdx = 0;
      for (const auto &key : mountOrder) {
        if (!m_radialSettings.mounts.contains(key))
          continue;
        if (mountIdx < m_mountToggleRects.size() &&
            m_mountToggleRects[mountIdx].isValid() &&
            m_mountToggleRects[mountIdx]
                .adjusted(-8, -4, 8, 4)
                .contains(scrolledPos)) {
          m_radialSettings.mounts[key].enabled =
              !m_radialSettings.mounts[key].enabled;
          emit radialSettingsChanged(m_radialSettings);
          update();
          return;
        }
        ++mountIdx;
      }

      // No-Hold Mode toggle
      if (m_noHoldToggleRect.isValid() &&
          m_noHoldToggleRect.adjusted(-8, -4, 8, 4).contains(scrolledPos)) {
        m_radialSettings.noHoldMode = !m_radialSettings.noHoldMode;
        emit radialSettingsChanged(m_radialSettings);
        update();
        return;
      }

      // Reset Cursor toggle
      if (m_resetCursorToggleRect.isValid() &&
          m_resetCursorToggleRect.adjusted(-8, -4, 8, 4)
              .contains(scrolledPos)) {
        m_radialSettings.resetCursorAfterKeybind =
            !m_radialSettings.resetCursorAfterKeybind;
        emit radialSettingsChanged(m_radialSettings);
        update();
        return;
      }

      // Fast Mount Swap toggle
      if (m_fastMountSwapToggleRect.isValid() &&
          m_fastMountSwapToggleRect.adjusted(-8, -4, 8, 4)
              .contains(scrolledPos)) {
        m_radialSettings.fastMountSwap =
            !m_radialSettings.fastMountSwap;
        emit radialSettingsChanged(m_radialSettings);
        update();
        return;
      }

      // Wheel Scale slider (drag index 20)
      if (m_radialScaleSliderRect.isValid() &&
          m_radialScaleSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 20;
        qreal value = qBound(
            0.0,
            (pos.x() - m_radialScaleSliderRect.left()) /
                m_radialScaleSliderRect.width(),
            1.0);
        m_radialSettings.wheelScale = 0.5 + value * 1.5; // 0.5-2.0
        emit radialSettingsChanged(m_radialSettings);
        update();
        return;
      }

      // Opacity slider (drag index 21)
      if (m_radialOpacitySliderRect.isValid() &&
          m_radialOpacitySliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 21;
        qreal value = qBound(
            0.0,
            (pos.x() - m_radialOpacitySliderRect.left()) /
                m_radialOpacitySliderRect.width(),
            1.0);
        m_radialSettings.opacity = value;
        emit radialSettingsChanged(m_radialSettings);
        update();
        return;
      }

      // Animation slider (drag index 22)
      if (m_radialAnimSliderRect.isValid() &&
          m_radialAnimSliderRect.contains(scrolledPos)) {
        m_isDraggingSlider = true;
        m_dragSliderIndex = 22;
        qreal value = qBound(
            0.0,
            (pos.x() - m_radialAnimSliderRect.left()) /
                m_radialAnimSliderRect.width(),
            1.0);
        m_radialSettings.animationTimeMs = 50 + static_cast<int>(value * 450);
        emit radialSettingsChanged(m_radialSettings);
        update();
        return;
      }

      return; // Consume click in radial area
    }

    // Packs tab: tree interactions
    int idx = hitTestTreeItem(pos);
    if (idx >= 0 && idx < m_flatItems.size()) {
      OverlayMenuItem *item = m_flatItems[idx];

      // Check if click is on expander area
      if (hitTestExpander(pos, idx)) {
        item->isExpanded = !item->isExpanded;
        flattenTree();
        update();
        return;
      }

      // Check if click is on toggle area (only for items with content)
      if (hitTestToggle(pos, idx) && item->hasContent &&
          !item->isSeparator) {
        item->isEnabled = !item->isEnabled;

        // Suppress rebuildTree during our own settings changes
        m_suppressRebuild = true;

        // Persist the change
        if (m_markerSettings) {
          if (item->isPack) {
            m_markerSettings->setPackEnabled(item->id, item->isEnabled);
          } else {
            QString packId = findPackIdForItem(idx);
            if (!packId.isEmpty()) {
              m_markerSettings->setCategoryEnabled(packId, item->id,
                                                   item->isEnabled);
            }
          }
        }

        m_suppressRebuild = false;

        // Propagate to runtime visibility
        if (m_markerManager) {
          if (item->isPack) {
            // Pack toggle: update all descendant categories.
            // When re-enabling (item->isEnabled == true), read each
            // category's persisted state — don't blindly set all to true.
            // When disabling, force all to false (pack is master switch).
            int onCount = 0, offCount = 0;
            std::function<void(QList<OverlayMenuItem> &)> updateChildren;
            updateChildren = [&](QList<OverlayMenuItem> &children) {
              for (auto &child : children) {
                if (!child.isPack) {
                  bool catEnabled = false;
                  if (item->isEnabled && m_markerSettings) {
                    // Use DirectEnabled (no ancestor cascade) — only check
                    // this category's OWN explicit override. Ancestor cascade
                    // is handled by isCategoryVisible() at render time.
                    // Using the cascading version would write false for ALL
                    // descendants of a disabled parent, blocking individual
                    // child toggles from working.
                    catEnabled = m_markerSettings->isCategoryDirectEnabled(
                        item->id, child.id);
                  }
                  // Update runtime cache
                  m_markerManager->updateCategoryVisibility(
                      child.id, catEnabled);
                  // Update tree item visual to match persisted state
                  child.isEnabled = catEnabled;
                  if (catEnabled)
                    ++onCount;
                  else
                    ++offCount;
                }
                updateChildren(child.children);
              }
            };
            updateChildren(item->children);
            qInfo() << "OverlayMenu: Pack" << item->id
                    << (item->isEnabled ? "ENABLED" : "DISABLED")
                    << "— categories ON:" << onCount << "OFF:" << offCount;
          } else {
            m_markerManager->updateCategoryVisibility(
                item->id, item->isEnabled);
            qInfo() << "OverlayMenu: Category" << item->id
                    << (item->isEnabled ? "ON" : "OFF");
          }
        }

        update();
        return;
      }

      // Click on the item row — toggle expand (if has children) or toggle
      // enable
      if (!item->children.isEmpty()) {
        item->isExpanded = !item->isExpanded;
        flattenTree();
      } else if (item->hasContent && !item->isSeparator) {
        // Leaf item with content — toggle enable
        item->isEnabled = !item->isEnabled;

        m_suppressRebuild = true;
        if (m_markerSettings) {
          QString packId = findPackIdForItem(idx);
          if (item->isPack) {
            m_markerSettings->setPackEnabled(item->id, item->isEnabled);
          } else if (!packId.isEmpty()) {
            m_markerSettings->setCategoryEnabled(packId, item->id,
                                                 item->isEnabled);
          }
        }
        m_suppressRebuild = false;

        // Propagate to runtime visibility
        if (m_markerManager &&
            !item->isPack) {
          m_markerManager->updateCategoryVisibility(
              item->id, item->isEnabled);
        }
      }
      update();
      return;
    }
    return; // Click inside panel but not on an item — consume event
  }

  // Click outside panel and icon — close menu
  if (m_isMenuOpen) {
    setMenuOpen(false);
  }
}

void OverlayMenuWidget::mouseMoveEvent(QMouseEvent *event) {
  if (!m_isMenuOpen) {
    // Only need to track hover over icon
    update();
    return;
  }

  QPointF pos = event->position();

  // Panel dragging
  if (m_isDraggingPanel) {
    m_panelPos = pos - m_panelDragStart;
    // Clamp to widget bounds
    m_panelPos.setX(qBound(0.0, m_panelPos.x(), (double)width() - kPanelWidth));
    m_panelPos.setY(
        qBound(0.0, m_panelPos.y(), (double)height() - kMaxPanelHeight));
    update();
    return;
  }

  // Slider dragging
  if (m_isDraggingSlider) {
    // Radial tab sliders (indices 20-22)
    if (m_dragSliderIndex >= 20 && m_dragSliderIndex <= 22) {
      QRectF sliderRect;
      if (m_dragSliderIndex == 20)
        sliderRect = m_radialScaleSliderRect;
      else if (m_dragSliderIndex == 21)
        sliderRect = m_radialOpacitySliderRect;
      else
        sliderRect = m_radialAnimSliderRect;

      qreal value = qBound(
          0.0, (pos.x() - sliderRect.left()) / sliderRect.width(), 1.0);

      if (m_dragSliderIndex == 20) {
        m_radialSettings.wheelScale = 0.5 + value * 1.5;
      } else if (m_dragSliderIndex == 21) {
        m_radialSettings.opacity = value;
      } else {
        m_radialSettings.animationTimeMs = 50 + static_cast<int>(value * 450);
      }
      emit radialSettingsChanged(m_radialSettings);
      update();
      return;
    }

    // Marker settings sliders (indices 0-10)
    if (m_markerSettings) {
    QRectF sliderRect;
    if (m_dragSliderIndex == 0)
      sliderRect = m_overlaySliderRect;
    else if (m_dragSliderIndex == 1)
      sliderRect = m_minimapSliderRect;
    else if (m_dragSliderIndex == 3)
      sliderRect = m_distFontSliderRect;
    else if (m_dragSliderIndex == 4)
      sliderRect = m_markerScaleSliderRect;
    else if (m_dragSliderIndex == 5)
      sliderRect = m_distLabelOffsetSliderRect;
    else
      sliderRect = m_fadeEdgeSliderRect;

    qreal value =
        qBound(0.0, (pos.x() - sliderRect.left()) / sliderRect.width(), 1.0);
    if (m_dragSliderIndex == 0) {
      m_markerSettings->setOverlayOpacity(value);
    } else if (m_dragSliderIndex == 1) {
      m_markerSettings->setMinimapOpacity(value);
    } else if (m_dragSliderIndex == 3) {
      m_markerSettings->setDistanceFontSize(
          8 + static_cast<int>(qRound(value * 16.0)));
    } else if (m_dragSliderIndex == 4) {
      m_markerSettings->setMarkerScale(0.5 + value * 2.5);
    } else if (m_dragSliderIndex == 5) {
      m_markerSettings->setDistanceLabelOffset(
          static_cast<int>(qRound(value * 50.0)));
    } else if (m_dragSliderIndex == 6) {
      sliderRect = m_renderDistSliderRect;
      value =
          qBound(0.0, (pos.x() - sliderRect.left()) / sliderRect.width(), 1.0);
      m_markerSettings->setMaxRenderDistance(50.0 + value * 450.0);
    } else if (m_dragSliderIndex == 7) {
      sliderRect = m_heightRangeSliderRect;
      value =
          qBound(0.0, (pos.x() - sliderRect.left()) / sliderRect.width(), 1.0);
      m_markerSettings->setHeightFilterRange(
          static_cast<float>(5.0 + value * 45.0));
    } else if (m_dragSliderIndex == 8) {
      sliderRect = m_trailWidthSliderRect;
      value =
          qBound(0.0, (pos.x() - sliderRect.left()) / sliderRect.width(), 1.0);
      m_markerSettings->setMinimapTrailWidth(
          static_cast<float>(1.0 + value * 9.0));
    } else if (m_dragSliderIndex == 9) {
      sliderRect = m_minimapMarkerScaleSliderRect;
      value =
          qBound(0.0, (pos.x() - sliderRect.left()) / sliderRect.width(), 1.0);
      m_markerSettings->setMinimapMarkerScale(0.5 + value * 2.5);
    } else if (m_dragSliderIndex == 10) {
      sliderRect = m_minimapMarkerOpacitySliderRect;
      value =
          qBound(0.0, (pos.x() - sliderRect.left()) / sliderRect.width(), 1.0);
      m_markerSettings->setMinimapMarkerOpacity(value);
    } else {
      m_markerSettings->setExclusionFadeEdge(static_cast<float>(value * 0.05));
    }
    update();
    return;
    }
  }

  // Track hover over tree items (Packs tab only)
  if (m_activeTab == Tab::Packs) {
    int prevHover = m_hoverIndex;
    m_hoverIndex = hitTestTreeItem(pos);
    if (m_hoverIndex != prevHover) {
      update();
    }
  }
}

void OverlayMenuWidget::mouseReleaseEvent(QMouseEvent *event) {
  Q_UNUSED(event);
  m_isDraggingPanel = false;
  m_isDraggingSlider = false;
  m_dragSliderIndex = -1;
}

void OverlayMenuWidget::wheelEvent(QWheelEvent *event) {
  if (!m_isMenuOpen || !m_panelRect.contains(event->position())) {
    return;
  }

  int delta = event->angleDelta().y();

  if (m_activeTab == Tab::Settings) {
    m_settingsScrollOffset =
        qBound(0, m_settingsScrollOffset - delta / 4, m_settingsMaxScroll);
  } else if (m_activeTab == Tab::Radial) {
    m_radialScrollOffset =
        qBound(0, m_radialScrollOffset - delta / 4, m_radialMaxScroll);
  } else {
    m_scrollOffset = qBound(0, m_scrollOffset - delta / 4, m_maxScroll);
  }
  update();
  event->accept();
}

// ============================================================================
// Hit Testing
// ============================================================================

int OverlayMenuWidget::hitTestTreeItem(const QPointF &pos) const {
  if (!m_panelRect.contains(pos)) {
    return -1;
  }

  // Tree starts below header + tab bar
  float treeTop = m_panelRect.top() + kPanelHeaderHeight + kTabBarHeight + 2;
  float relY = pos.y() - treeTop + m_scrollOffset;

  if (relY < 0) {
    return -1;
  }

  int index = static_cast<int>(relY / kItemHeight);
  if (index >= 0 && index < m_flatItems.size()) {
    return index;
  }
  return -1;
}

bool OverlayMenuWidget::hitTestToggle(const QPointF &pos, int itemIndex) const {
  Q_UNUSED(itemIndex);
  // Toggle is on the right side of the panel
  float toggleLeft = m_panelRect.right() - kToggleWidth - 10;
  return pos.x() >= toggleLeft;
}

bool OverlayMenuWidget::hitTestExpander(const QPointF &pos,
                                        int itemIndex) const {
  if (itemIndex < 0 || itemIndex >= m_flatItems.size()) {
    return false;
  }
  const OverlayMenuItem *item = m_flatItems[itemIndex];
  if (item->children.isEmpty()) {
    return false;
  }

  int indent = kIndentStep * item->depth + 8;
  float expanderLeft = m_panelRect.left() + indent - 18;
  float expanderRight = expanderLeft + 16;
  return pos.x() >= expanderLeft && pos.x() <= expanderRight;
}

// ============================================================================
// Tree Flattening
// ============================================================================

void OverlayMenuWidget::flattenTree() {
  m_flatItems.clear();
  for (OverlayMenuItem &root : m_treeRoots) {
    flattenItem(root);
  }
}

void OverlayMenuWidget::flattenItem(OverlayMenuItem &item) {
  m_flatItems.append(&item);
  if (item.isExpanded) {
    for (OverlayMenuItem &child : item.children) {
      flattenItem(child);
    }
  }
}

// ============================================================================
// Helpers
// ============================================================================

void OverlayMenuWidget::addCategoryToModel(
    QList<OverlayMenuItem> &parentChildren, const QString &packId,
    const MarkerCategory &cat, int depth,
    const QSet<QString> &contentPaths) {
  // Separator categories (e.g., "[-CORE GAME-]") are visual section dividers.
  // Keep them as styled section headers but promote their children to siblings.
  if (cat.isSeparator) {
    // Deduplicate: skip if separator with same label already exists at depth
    QString sepLabel =
        cat.displayName.isEmpty() ? cat.name : cat.displayName;
    bool duplicate = false;
    for (const auto &existing : parentChildren) {
      if (existing.isSeparator && existing.label == sepLabel) {
        duplicate = true;
        break;
      }
    }

    if (!duplicate) {
      // Add the separator as a visual-only section header
      OverlayMenuItem sepItem;
      sepItem.id = cat.fullName;
      sepItem.label = sepLabel;
      sepItem.isPack = false;
      sepItem.isExpanded = false;
      sepItem.depth = depth;
      sepItem.isSeparator = true;
      sepItem.hasContent = false; // No toggle for separators
      parentChildren.append(sepItem);
    }

    // Promote separator's children to be siblings (not nested under separator)
    for (const MarkerCategory &child : cat.children) {
      addCategoryToModel(parentChildren, packId, child, depth, contentPaths);
    }
    return;
  }

  OverlayMenuItem item;
  item.id = cat.fullName;
  item.label = cat.displayName.isEmpty() ? cat.name : cat.displayName;
  item.iconPath = cat.iconPath;
  item.isPack = false;
  item.isExpanded = false;
  item.depth = depth;
  item.isSeparator = false;
  item.hasContent = contentPaths.contains(cat.fullName);

  // Show each item's OWN persisted state (no ancestor cascade).
  // The rendering path handles ancestor cascade at read time.
  // Matches Profile Editor behavior (MarkerPackCard.cpp:243).
  if (item.hasContent && !item.isSeparator && m_markerSettings) {
    item.isEnabled =
        m_markerSettings->isCategoryDirectEnabled(packId, cat.fullName);
  }

  for (const MarkerCategory &child : cat.children) {
    addCategoryToModel(item.children, packId, child, depth + 1, contentPaths);
  }

  parentChildren.append(item);
}

QString OverlayMenuWidget::findPackIdForItem(int flatIndex) const {
  // Walk backward through flat items to find the parent pack
  for (int i = flatIndex; i >= 0; --i) {
    if (m_flatItems[i]->isPack) {
      return m_flatItems[i]->id;
    }
  }
  return QString();
}

// ============================================================================
// Theme Change Handler
// ============================================================================

void OverlayMenuWidget::changeEvent(QEvent *event) {
  if (event->type() == QEvent::StyleChange) {
    update(); // Force repaint with new theme tokens
  }
  QWidget::changeEvent(event);
}
