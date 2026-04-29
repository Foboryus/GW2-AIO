/**
 * @file RadialWheel.cpp
 * @brief Radial wheel state management implementation
 *
 * Handles polar coordinate hover detection, per-element fade animations,
 * and wheel lifecycle (activate/deactivate with selection return).
 *
 * Port of GW2Radial Wheel.cpp hover/animation logic (~200 lines).
 */

#include "RadialWheel.h"

#include <QDebug>
#include <QtMath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Constructor
// ============================================================================

RadialWheel::RadialWheel() = default;

// ============================================================================
// Lifecycle
// ============================================================================

void RadialWheel::activate(float centerX, float centerY) {
  if (m_isActive) {
    return;
  }

  m_isActive = true;
  m_wheelFadeIn = 0.0f;
  m_centerX = centerX;
  m_centerY = centerY;
  m_hoveredIndex = -1;

  // Reset all element hover states
  for (auto &elem : m_elements) {
    elem.hoverFadeIn = 0.0f;
  }

  rebuildVisibleElements();

  qInfo() << "RadialWheel: Activated at" << centerX << centerY
          << "with" << m_visibleElements.size() << "elements";
}

QString RadialWheel::deactivate() {
  if (!m_isActive) {
    return QString();
  }

  m_isActive = false;

  QString selectedId;
  if (m_hoveredIndex >= 0 && m_hoveredIndex < m_visibleElements.size()) {
    selectedId = m_visibleElements[m_hoveredIndex]->id;
    qInfo() << "RadialWheel: Selected" << selectedId;
  } else {
    qInfo() << "RadialWheel: Deactivated (no selection)";
  }

  m_wheelFadeIn = 0.0f;
  m_hoveredIndex = -1;
  return selectedId;
}

// ============================================================================
// Per-Frame Update
// ============================================================================

void RadialWheel::tick(float deltaMs, int mouseX, int mouseY, int screenW,
                       int screenH) {
  if (!m_isActive) {
    return;
  }

  float deltaSec = deltaMs / 1000.0f;

  // Advance wheel fade-in
  if (m_wheelFadeIn < 1.0f) {
    m_wheelFadeIn = qMin(1.0f, m_wheelFadeIn + deltaSec * kWheelFadeSpeed);
  }

  // Advance animation timer (monotonic, for noise)
  m_animationTimer += deltaSec;

  // Update hover state
  updateHover(mouseX, mouseY, screenW, screenH);

  // Animate per-element hover fades
  for (int i = 0; i < m_visibleElements.size(); ++i) {
    auto *elem = m_visibleElements[i];
    if (i == m_hoveredIndex) {
      // Fade in
      elem->hoverFadeIn =
          qMin(1.0f, elem->hoverFadeIn + deltaSec * kHoverFadeSpeed);
    } else {
      // Fade out
      elem->hoverFadeIn =
          qMax(0.0f, elem->hoverFadeIn - deltaSec * kHoverFadeSpeed);
    }
  }
}

// ============================================================================
// Hover Detection (Polar Coordinates)
// ============================================================================

void RadialWheel::updateHover(int mouseX, int mouseY, int screenW,
                              int screenH) {
  if (m_visibleElements.isEmpty()) {
    m_hoveredIndex = -1;
    return;
  }

  // Match GW2Radial Wheel::UpdateHover() coordinate system exactly:
  // 1. Normalize mouse to [0,1]
  // 2. Subtract wheel center
  // 3. Correct Y by (height/width) to make circular in screen space
  float mx = static_cast<float>(mouseX) / static_cast<float>(screenW);
  float my = static_cast<float>(mouseY) / static_cast<float>(screenH);
  mx -= m_centerX;
  my -= m_centerY;

  // Aspect correction on Y axis (GW2Radial line 132)
  my *= static_cast<float>(screenH) / static_cast<float>(screenW);

  float radiusSq = mx * mx + my * my;

  // Dead zone — center circle (matches GW2Radial's centerScale threshold)
  float deadZoneRadius = m_centerScale * 0.125f * 0.8f;
  if (radiusSq < deadZoneRadius * deadZoneRadius) {
    m_hoveredIndex = -1;
    return;
  }

  // Angle calculation — GW2Radial uses atan2(-y, -x) - PI/2
  // This negation + offset aligns element 0 at the top (12 o'clock)
  float mouseAngle = qAtan2(-my, -mx) - 0.5f * static_cast<float>(M_PI);
  if (mouseAngle < 0.0f) {
    mouseAngle += 2.0f * static_cast<float>(M_PI);
  }

  // Element index — offset by half-element to center sectors (GW2Radial line 148)
  float elementAngle =
      2.0f * static_cast<float>(M_PI) /
      static_cast<float>(m_visibleElements.size());
  int index = static_cast<int>((mouseAngle - elementAngle / 2.0f) /
                                    elementAngle +
                                1) %
              static_cast<int>(m_visibleElements.size());

  m_hoveredIndex = index;
}

// ============================================================================
// Visible Elements
// ============================================================================

void RadialWheel::rebuildVisibleElements() {
  m_visibleElements.clear();
  for (auto &elem : m_elements) {
    if (elem.enabled) {
      m_visibleElements.append(&elem);
    }
  }

  // Sort by sortOrder
  std::sort(m_visibleElements.begin(), m_visibleElements.end(),
            [](const RadialElement *a, const RadialElement *b) {
              return a->sortOrder < b->sortOrder;
            });
}

// ============================================================================
// Constant Buffer Helpers
// ============================================================================

void RadialWheel::fillHoverFadeIns(float *out, int maxCount) const {
  // Zero-fill
  for (int i = 0; i < maxCount; ++i) {
    out[i] = 0.0f;
  }

  // Fill from visible elements
  for (int i = 0; i < m_visibleElements.size() && i < maxCount; ++i) {
    out[i] = m_visibleElements[i]->hoverFadeIn;
  }
}
