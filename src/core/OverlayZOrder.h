#pragma once

/**
 * @file OverlayZOrder.h
 * @brief Cross-process overlay z-order management via window title conventions
 *
 * Each AIO overlay child process sets a deterministic window title encoding
 * its z-order layer (e.g., "GW2AIO_L300_Radial"). Siblings discover each
 * other via EnumWindows and insert themselves at the correct z-position.
 *
 * Layer gaps of 100 allow new children to be sandwiched between existing
 * layers without modifying any existing code.
 *
 * Header-only — no .cpp needed.
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <string>
#include <vector>

namespace OverlayZOrder {

// ============================================================================
// Layer Constants (bottom → top, gaps of 100 for future insertion)
// ============================================================================

constexpr int kLayer3D         = 100;  // 3D markers/trails (Child3DOverlay)
constexpr int kLayerMinimap    = 200;  // Minimap/MapUI (ChildMinimap)
constexpr int kLayerRadial     = 300;  // Radial menu (ChildRadial)
constexpr int kLayerHUD        = 400;  // Diamond/pause/settings (ChildOverlay)
constexpr int kLayerCompositor = 500;  // Compositor (always on top of all overlays)
// Main AIO window: HWND_TOPMOST (not managed here)

// ============================================================================
// Title Convention: "GW2AIO_L<layer>_<suffix>"
// ============================================================================

static constexpr wchar_t kTitlePrefix[] = L"GW2AIO_L";

/**
 * @brief Build a window title encoding the z-order layer.
 * @param layer  Numeric layer (e.g., 300)
 * @param suffix Human-readable suffix (e.g., L"Radial")
 * @return Title string like L"GW2AIO_L300_Radial"
 */
inline std::wstring buildTitle(int layer, const wchar_t *suffix) {
  return std::wstring(kTitlePrefix) + std::to_wstring(layer) + L"_" + suffix;
}

/**
 * @brief Parse the layer number from an HWND's title.
 * @return Layer number, or -1 if not an AIO overlay title.
 */
inline int parseLayer(HWND hwnd) {
  wchar_t title[128] = {};
  int len = GetWindowTextW(hwnd, title, 128);
  if (len <= 0) {
    return -1;
  }

  // Must start with "GW2AIO_L"
  constexpr int prefixLen = 8; // wcslen(L"GW2AIO_L")
  if (len < prefixLen || wcsncmp(title, kTitlePrefix, prefixLen) != 0) {
    return -1;
  }

  // Parse digits after prefix until '_' or end
  int layer = 0;
  for (int i = prefixLen; i < len; ++i) {
    if (title[i] == L'_') {
      break;
    }
    if (title[i] >= L'0' && title[i] <= L'9') {
      layer = layer * 10 + (title[i] - L'0');
    } else {
      return -1; // Invalid character
    }
  }

  return layer > 0 ? layer : -1;
}

// ============================================================================
// Z-Order Insertion
// ============================================================================

/**
 * @brief Find the HWND to insert after for correct z-layering.
 *
 * Enumerates visible windows, finds the highest-layer AIO sibling that is
 * BELOW `myLayer`. Returns that HWND so the caller can:
 *   SetWindowPos(myHwnd, insertAfter, ...)
 *
 * If no lower sibling exists, returns nullptr (caller should insert above GW2).
 *
 * @param gw2Hwnd   The GW2 game window HWND (base of the stack)
 * @param myLayer   This overlay's layer number
 * @param myHwnd    This overlay's own HWND (excluded from search)
 * @return HWND to insert after, or nullptr if no lower sibling found
 */
inline HWND findInsertAfter(HWND gw2Hwnd, int myLayer, HWND myHwnd) {
  struct EnumData {
    int myLayer;
    HWND myHwnd;
    HWND bestHwnd;   // Highest-layer sibling below myLayer
    int bestLayer;    // Its layer number
  };

  EnumData data = {};
  data.myLayer = myLayer;
  data.myHwnd = myHwnd;
  data.bestHwnd = nullptr;
  data.bestLayer = -1;

  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *d = reinterpret_cast<EnumData *>(lParam);

        // Skip self
        if (hwnd == d->myHwnd) {
          return TRUE;
        }

        // Must be visible
        if (!IsWindowVisible(hwnd)) {
          return TRUE;
        }

        int layer = parseLayer(hwnd);
        if (layer < 0) {
          return TRUE; // Not an AIO overlay
        }

        // We want the highest layer that is still below our layer
        if (layer < d->myLayer && layer > d->bestLayer) {
          d->bestLayer = layer;
          d->bestHwnd = hwnd;
        }

        return TRUE;
      },
      reinterpret_cast<LPARAM>(&data));

  return data.bestHwnd;
}

} // namespace OverlayZOrder
