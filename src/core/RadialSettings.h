#pragma once

/**
 * @file RadialSettings.h
 * @brief Per-profile radial menu configuration
 *
 * Stores all radial menu settings for a single profile.
 * Persisted as a separate JSON file per profile in radialConfigDir.
 *
 * This replaces the old features/radial/RadialConfig which was a
 * non-per-profile, non-StorageBackend design. The old stubs in
 * features/radial/ will be superseded by the child-based D3D11
 * implementation.
 *
 * DO NOT ADD:
 * - Inline implementations beyond trivial struct accessors
 * - Rendering logic (belongs in RadialWheel/RadialController)
 * - UI code (belongs in RadialTabWidget)
 */

#include <QJsonObject>
#include <QMap>
#include <QString>

/**
 * @brief Configuration for a single radial element (mount, novelty, marker)
 */
struct RadialElementConfig {
  bool enabled = true;   // Element visible in wheel
  int sortOrder = 0;     // Display order (lower = first)
  int scanCode = 0;      // Windows virtual key code for this element's keybind
  int modifiers = 0;     // Modifier flags (Shift=1, Ctrl=2, Alt=4)

  QJsonObject toJson() const;
  static RadialElementConfig fromJson(const QJsonObject &obj);
};

/**
 * @brief Center behavior when no element is hovered on release
 */
enum class RadialCenterBehavior {
  Nothing = 0,       // Do nothing
  Previous = 1,      // Repeat previous selection
  Favorite = 2,      // Use configured favorite
  PassToGame = 3,    // Pass the key to the game
  MountDismount = 4  // Send generic Mount/Dismount keybind
};

/**
 * @brief Complete radial menu settings for one profile
 */
struct RadialSettings {
  static constexpr int CurrentVersion = 1;
  int schemaVersion = CurrentVersion;

  // --- Global ---
  bool radialEnabled = true;    // Master kill switch
  QString iconStyle = "svg";    // "svg" (AIO default) or "png_mit" (GW2Radial art)

  // --- Mount wheel ---
  bool mountWheelEnabled = true;
  int mountHotkey = 0x58;          // VK_X  (Ctrl+X default)
  int mountHotkeyModifiers = 2;          // GW2 bitmask: Ctrl
  QMap<QString, RadialElementConfig> mounts;

  // --- Novelty wheel ---
  bool noveltyWheelEnabled = true;
  int noveltyHotkey = 0x4E;        // VK_N  (Ctrl+N default)
  int noveltyHotkeyModifiers = 2;          // GW2 bitmask: Ctrl
  QMap<QString, RadialElementConfig> novelties;

  // --- Marker wheel ---
  bool markerWheelEnabled = true;
  int markerHotkey = 0x4D;         // VK_M  (Ctrl+M default)
  int markerHotkeyModifiers = 2;          // GW2 bitmask: Ctrl
  QMap<QString, RadialElementConfig> markers;

  // --- Display ---
  float wheelScale = 1.0f;       // 0.5 – 2.0
  float centerScale = 0.35f;     // Center region fraction
  float opacity = 1.0f;          // 0.0 – 1.0
  int animationTimeMs = 150;     // Fade-in duration
  int displayDelayMs = 0;        // Delay before showing (for bypass)

  // --- Interaction ---
  RadialCenterBehavior centerBehavior = RadialCenterBehavior::MountDismount;
  bool noHoldMode = false;             // Click to select (no hold required)
  bool clickSelectMode = false;        // Click on element to select
  bool resetCursorAfterKeybind = true;
  bool lockCameraWhenOverlayed = false;

  // --- Queuing ---
  bool enableQueuing = false;      // Queue mount if unusable
  int maxQueueWaitMs = 5000;       // Timeout for queued input
  int conditionalDelayMs = 500;    // Delay before sending queued keybind

  // --- Smart features ---
  bool smartRadialMounts = false;  // Filter by owned mounts (API integration)
  bool fastMountSwap = true;      // Dismount current mount before mounting new
  int dismountScanCode = 0;       // Generic "Mount/Dismount" keybind (GW2 action 152)
  int dismountModifiers = 0;      // Modifiers for dismount key

  // --- Serialization ---
  QJsonObject toJson() const;
  static RadialSettings fromJson(const QJsonObject &obj);

  /**
   * @brief Create default settings with all elements populated
   */
  static RadialSettings defaults();
};
