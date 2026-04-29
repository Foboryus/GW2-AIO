#pragma once

/**
 * @file RadialElement.h
 * @brief Data struct for a single radial wheel element (mount, novelty, marker)
 *
 * Header-only — all members are plain data, no complex logic.
 * RadialWheel owns a vector of these.
 *
 * DO NOT ADD:
 * - Complex methods (belongs in RadialWheel)
 * - UI code (belongs in RadialTabWidget)
 */

#include <QString>
#include <d3d11.h>
#include <wrl/client.h>

struct RadialElement {
  QString id;           // e.g., "mount_raptor", "novelty_chair"
  QString displayName;  // e.g., "Raptor", "Chair"
  bool enabled = true;  // Whether this element appears in the wheel
  int sortOrder = 0;    // Display order (lower = first)
  QString iconPath;     // QRC path (e.g., ":/radial/png/Raptor.png")

  // Per-element hover animation state (0.0 = unhovered, 1.0 = fully hovered)
  float hoverFadeIn = 0.0f;

  // GPU texture for this element's icon (loaded by RadialRenderer)
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> iconSRV;

  // Tint color (RGBA, default white = no tint)
  float colorR = 1.0f;
  float colorG = 1.0f;
  float colorB = 1.0f;
  float colorA = 1.0f;

  // Whether icon uses premultiplied alpha
  bool premultipliedAlpha = false;
};
