#pragma once

/**
 * @file RadialWheel.h
 * @brief Radial wheel state management — hover, animation, selection
 *
 * Manages a list of RadialElements, determines which one is hovered
 * based on mouse position (polar coordinates), and drives fade animations.
 *
 * This is the logical state — rendering is handled by RadialRenderer.
 *
 * DO NOT ADD:
 * - Inline implementations (use RadialWheel.cpp)
 * - GPU/D3D11 code (belongs in RadialRenderer)
 * - UI code (belongs in RadialTabWidget)
 */

#include "RadialElement.h"

#include <QElapsedTimer>
#include <QString>
#include <QVector>

class RadialWheel {
public:
  RadialWheel();
  ~RadialWheel() = default;

  // --- Lifecycle ---

  /**
   * @brief Activate the wheel (show it, start fade-in)
   * @param centerX NDC x of wheel center (0.0 = left, 1.0 = right)
   * @param centerY NDC y of wheel center (0.0 = top, 1.0 = bottom)
   */
  void activate(float centerX, float centerY);

  /**
   * @brief Deactivate the wheel — returns ID of hovered element (or empty)
   */
  QString deactivate();

  /**
   * @brief Check if wheel is currently displayed
   */
  bool isActive() const { return m_isActive; }

  // --- Per-Frame Update ---

  /**
   * @brief Advance animations and update hover state
   * @param deltaMs Milliseconds since last tick
   * @param mouseX Mouse X in screen pixels
   * @param mouseY Mouse Y in screen pixels
   * @param screenW Screen width in pixels
   * @param screenH Screen height in pixels
   */
  void tick(float deltaMs, int mouseX, int mouseY, int screenW, int screenH);

  // --- State Accessors (for RadialRenderer constant buffer fill) ---

  float wheelFadeIn() const { return m_wheelFadeIn; }
  float animationTimer() const { return m_animationTimer; }
  float centerScale() const { return m_centerScale; }
  float globalOpacity() const { return m_globalOpacity; }
  int hoveredIndex() const { return m_hoveredIndex; }
  float centerX() const { return m_centerX; }
  float centerY() const { return m_centerY; }

  /**
   * @brief Get visible (enabled) elements
   */
  const QVector<RadialElement *> &visibleElements() const {
    return m_visibleElements;
  }

  /**
   * @brief Get all elements (for configuration)
   */
  QVector<RadialElement> &elements() { return m_elements; }
  const QVector<RadialElement> &elements() const { return m_elements; }

  /**
   * @brief Fill the hoverFadeIns array for the shader constant buffer
   * @param out Array of RADIAL_MAX_ELEMENTS floats
   */
  void fillHoverFadeIns(float *out, int maxCount) const;

  // --- Configuration ---

  void setCenterScale(float scale) { m_centerScale = scale; }
  void setGlobalOpacity(float opacity) { m_globalOpacity = opacity; }
  void setAnimationSpeed(float speed) { m_animationSpeed = speed; }

  /**
   * @brief Rebuild the visible elements list from enabled state
   * Call after changing element enabled flags.
   */
  void rebuildVisibleElements();

private:
  void updateHover(int mouseX, int mouseY, int screenW, int screenH);

  QVector<RadialElement> m_elements;
  QVector<RadialElement *> m_visibleElements;

  bool m_isActive = false;
  float m_wheelFadeIn = 0.0f;       // 0→1 wipe animation
  float m_animationTimer = 0.0f;    // Monotonic for noise
  float m_centerX = 0.5f;           // NDC wheel center
  float m_centerY = 0.5f;
  float m_centerScale = 0.18f;      // Dead zone radius (fraction)
  float m_globalOpacity = 1.0f;     // Master opacity
  float m_animationSpeed = 1.0f;    // Fade-in speed multiplier
  int m_hoveredIndex = -1;          // Currently hovered element (-1 = none)

  // Hover animation timing
  static constexpr float kHoverFadeSpeed = 8.0f;   // Units per second
  static constexpr float kWheelFadeSpeed = 4.0f;    // Units per second
};
