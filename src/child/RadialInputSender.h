/**
 * @file RadialInputSender.h
 * @brief Sends keybinds to GW2 via Win32 SendInput with hardware scan codes.
 *
 * GW2 uses DirectInput, which reads hardware scan codes from the OS input
 * queue. SendInput with KEYEVENTF_SCANCODE is the only method that works.
 * PostMessage/SendMessage with WM_KEYDOWN are window messages — DirectInput
 * ignores them entirely.
 *
 * Critical timing notes (from GW2 Key Binding and Manipulation research):
 * - GW2's engine polls input on a frame-tied loop (~16ms at 60fps, ~33ms at 30fps)
 * - If key down + key up happen within a single polling cycle, GW2 ignores it
 * - Minimum hold time: 50ms (safe for 30fps and above)
 * - All held keys (trigger + modifiers) MUST be released before sending the
 *   mount keybind to prevent ghost inputs
 * - Scan codes are layout-independent (QWERTY vs AZERTY doesn't matter)
 *
 * Usage:
 *   // Save cursor before showing radial
 *   RadialInputSender::saveCursorPosition();
 *
 *   // On selection — release trigger, send keybind, restore cursor
 *   RadialInputSender::sendKeybind(VK_NUMPAD1, 0, triggerVK);
 *   RadialInputSender::restoreCursorPosition();
 */

#ifndef RADIALINPUTSENDER_H
#define RADIALINPUTSENDER_H

// clang-format off
#include <windows.h>
// clang-format on

#include <QDebug>

class RadialInputSender {
public:
  // Modifier flags matching GW2 InputBinds XML mod bitmask
  // From GW2 docs: Alt=1 (001), Ctrl=2 (010), Shift=4 (100)
  static constexpr int kModAlt = 1;
  static constexpr int kModCtrl = 2;
  static constexpr int kModShift = 4;

  // Timing constants (frame-safe for GW2's DirectInput polling)
  // GW2 polls at frame rate: ~16ms@60fps, ~33ms@30fps.
  // These values ensure the input spans at least 2 polling cycles.
  static constexpr DWORD kModifierReleaseDelayMs = 20;  // After releasing held keys
  static constexpr DWORD kKeyHoldDurationMs = 50;        // Key down → key up
  static constexpr DWORD kPostSendDelayMs = 20;          // After key up before cursor restore

  /**
   * @brief Send a keybind to GW2 using hardware scan codes.
   * @param virtualKey Windows virtual key code for the mount/action (e.g., VK_NUMPAD1)
   * @param modifiers Modifier bitmask for the mount keybind (kModCtrl | kModShift | kModAlt)
   * @param triggerVK The radial trigger key that the user is still holding (0 to skip)
   *
   * Full sequence (matches GW2Radial's Input::SendKeybind pattern):
   * 1. Release all held keys (trigger key + modifier keys)
   * 2. Wait for GW2 to register the releases (one polling cycle)
   * 3. Press modifier keys required by the mount keybind (if any)
   * 4. Press the target key (key down)
   * 5. Hold for kKeyHoldDurationMs (ensures GW2 sees it across polling cycles)
   * 6. Release the target key (key up)
   * 7. Release modifier keys
   */
  static void sendKeybind(int virtualKey, int modifiers, int triggerVK = 0) {
    if (virtualKey == 0) {
      qWarning() << "RadialInputSender: Cannot send — virtualKey is 0 (not configured)";
      return;
    }

    // Convert VK to hardware scan code
    UINT scanCode = MapVirtualKeyW(
        static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
    if (scanCode == 0) {
      qWarning() << "RadialInputSender: MapVirtualKey failed for VK:" << virtualKey;
      return;
    }

    // Determine if this is an extended key (arrows, etc. — NOT numpad digits)
    DWORD extendedFlag = isExtendedKey(virtualKey) ? KEYEVENTF_EXTENDEDKEY : 0;

    qInfo() << "RadialInputSender: Sending VK:" << virtualKey
            << "scan:" << scanCode << "mod:" << modifiers
            << "trigger:" << triggerVK << "extended:" << (extendedFlag != 0);

    // Step 1: Release ALL currently held keys.
    releaseHeldKeys(triggerVK);

    // Step 2: Wait for GW2 to process the releases.
    Sleep(kModifierReleaseDelayMs);

    // Step 3: Press modifier keys if the mount keybind requires them
    if (modifiers & kModCtrl)
      sendKey(VK_CONTROL, MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC), 0, false);
    if (modifiers & kModShift)
      sendKey(VK_SHIFT, MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC), 0, false);
    if (modifiers & kModAlt)
      sendKey(VK_MENU, MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC), 0, false);

    // Step 4: Press the target key (send both VK + scan so GW2 sees
    // the correct VK regardless of NumLock state for numpad keys)
    sendKey(static_cast<UINT>(virtualKey), scanCode, extendedFlag, false);

    // Step 5: Hold for multiple polling cycles so GW2 reliably detects it
    Sleep(kKeyHoldDurationMs);

    // Step 6: Release the target key
    sendKey(static_cast<UINT>(virtualKey), scanCode, extendedFlag, true);

    // Step 7: Release modifier keys (reverse order)
    if (modifiers & kModAlt)
      sendKey(VK_MENU, MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC), 0, true);
    if (modifiers & kModShift)
      sendKey(VK_SHIFT, MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC), 0, true);
    if (modifiers & kModCtrl)
      sendKey(VK_CONTROL, MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC), 0, true);

    // Brief settle time before any cursor restore
    Sleep(kPostSendDelayMs);
  }

  /**
   * @brief Save the current cursor position and lock it in place.
   *
   * Called when the radial wheel activates. Uses ClipCursor to confine the
   * real OS cursor to a 1×1 pixel rect, preventing GW2 from seeing any
   * mouse delta (which would rotate the camera). The overlay uses the
   * virtual cursor position for hover detection instead.
   *
   * @param lockCamera If true, clip the cursor in place to prevent camera rotation.
   */
  static void saveCursorPosition(bool lockCamera = true) {
    GetCursorPos(&s_savedCursorPos);
    s_cursorSaved = true;
    s_virtualCursorPos = s_savedCursorPos;
    s_cameraLocked = lockCamera;

    if (lockCamera) {
      // Clip cursor to a 1×1 pixel rect at the saved position.
      // GW2 sees zero mouse delta → no camera rotation.
      RECT clipRect = {s_savedCursorPos.x, s_savedCursorPos.y,
                       s_savedCursorPos.x + 1, s_savedCursorPos.y + 1};
      ClipCursor(&clipRect);
    }
  }

  /**
   * @brief Restore the cursor to the saved position and release the clip.
   * Only restores if a position was previously saved. Resets the saved flag.
   */
  static void restoreCursorPosition() {
    if (!s_cursorSaved) return;

    // Release the cursor clip first
    if (s_cameraLocked) {
      ClipCursor(NULL);
      s_cameraLocked = false;
    }

    SetCursorPos(s_savedCursorPos.x, s_savedCursorPos.y);
    s_cursorSaved = false;
  }

  /**
   * @brief Get the virtual cursor position for overlay hover detection.
   *
   * When the camera is locked, the real cursor is clipped to a 1px rect.
   * The overlay must use this virtual position instead of GetCursorPos.
   * Call updateVirtualCursor() each frame before reading this.
   */
  static POINT virtualCursorPos() { return s_virtualCursorPos; }

  /**
   * @brief Update the virtual cursor by reading raw mouse deltas.
   *
   * Call once per frame while the wheel is active. Reads the real cursor
   * position (which ClipCursor constrains to a 1px area), computes the
   * delta from the clip point, accumulates it into the virtual cursor,
   * and resets the real cursor back to the clip point.
   *
   * @param screenW Screen width (for clamping)
   * @param screenH Screen height (for clamping)
   */
  static void updateVirtualCursor(int screenW, int screenH) {
    if (!s_cameraLocked || !s_cursorSaved) return;

    POINT current;
    GetCursorPos(&current);

    // Compute delta from the lock point
    int dx = current.x - s_savedCursorPos.x;
    int dy = current.y - s_savedCursorPos.y;

    if (dx != 0 || dy != 0) {
      // Accumulate into virtual cursor position
      s_virtualCursorPos.x += dx;
      s_virtualCursorPos.y += dy;

      // Clamp to screen bounds
      if (s_virtualCursorPos.x < 0) s_virtualCursorPos.x = 0;
      if (s_virtualCursorPos.y < 0) s_virtualCursorPos.y = 0;
      if (s_virtualCursorPos.x >= screenW) s_virtualCursorPos.x = screenW - 1;
      if (s_virtualCursorPos.y >= screenH) s_virtualCursorPos.y = screenH - 1;

      // Reset real cursor back to lock point
      SetCursorPos(s_savedCursorPos.x, s_savedCursorPos.y);
    }
  }

  /**
   * @brief Check if a cursor position has been saved.
   */
  static bool hasSavedCursorPosition() { return s_cursorSaved; }

  /**
   * @brief Check if the camera is currently locked (cursor clipped).
   */
  static bool isCameraLocked() { return s_cameraLocked; }

private:
  // Saved cursor position for restore after keybind
  static inline POINT s_savedCursorPos = {};
  static inline bool s_cursorSaved = false;

  // Virtual cursor — tracks where the user "would" be pointing
  // while the real cursor is clipped in place
  static inline POINT s_virtualCursorPos = {};
  static inline bool s_cameraLocked = false;

  /**
   * @brief Release all currently held keys that could interfere with input.
   * @param triggerVK The radial trigger key to also release (0 to skip)
   *
   * Releases: modifier keys (Ctrl/Shift/Alt) + the trigger key itself.
   * Only releases keys that are actually held (checked via GetAsyncKeyState).
   */
  static void releaseHeldKeys(int triggerVK) {
    // Release the trigger key first (most important — it's still held)
    if (triggerVK != 0 && (GetAsyncKeyState(triggerVK) & 0x8000)) {
      UINT triggerScan = MapVirtualKeyW(
          static_cast<UINT>(triggerVK), MAPVK_VK_TO_VSC);
      if (triggerScan != 0) {
        DWORD extFlag = isExtendedKey(triggerVK) ? KEYEVENTF_EXTENDEDKEY : 0;
        sendKey(static_cast<UINT>(triggerVK), triggerScan, extFlag, true);
      }
    }

    // Release modifier keys
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
      sendKey(VK_CONTROL, MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC), 0, true);
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
      sendKey(VK_SHIFT, MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC), 0, true);
    if (GetAsyncKeyState(VK_MENU) & 0x8000)
      sendKey(VK_MENU, MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC), 0, true);
  }

  /**
   * @brief Send a key event via SendInput with both VK and scan code.
   * @param vk Windows virtual key code (wParam in WM_KEYDOWN)
   * @param scanCode Hardware scan code (bits 16-23 of lParam in WM_KEYDOWN)
   * @param extraFlags Additional flags (e.g., KEYEVENTF_EXTENDEDKEY)
   * @param keyUp true for key release, false for key press
   *
   * Sends both wVk and wScan simultaneously. The system generates a
   * WM_KEYDOWN with wParam=wVk and lParam containing wScan in bits 16-23.
   * GW2 may read either field depending on its input system.
   */
  static void sendKey(UINT vk, UINT scanCode, DWORD extraFlags, bool keyUp) {
    INPUT ip = {};
    ip.type = INPUT_KEYBOARD;
    ip.ki.wVk = static_cast<WORD>(vk);
    ip.ki.wScan = static_cast<WORD>(scanCode);
    ip.ki.dwFlags = extraFlags;
    if (keyUp) {
      ip.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    ip.ki.time = 0;
    ip.ki.dwExtraInfo = 0;

    UINT sent = SendInput(1, &ip, sizeof(INPUT));
    if (sent == 0) {
      qWarning() << "RadialInputSender: SendInput failed, error:"
                  << GetLastError();
    }
  }

  /**
   * @brief Check if a virtual key code is an extended key.
   * Extended keys require KEYEVENTF_EXTENDEDKEY flag in SendInput.
   * These include numpad Enter, arrow keys, Insert, Delete, Home,
   * End, Page Up/Down, and right-hand Ctrl/Alt.
   */
  static bool isExtendedKey(int vk) {
    switch (vk) {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:    // Page Up
    case VK_NEXT:     // Page Down
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_NUMLOCK:
    case VK_SNAPSHOT: // Print Screen
    case VK_DIVIDE:   // Numpad /
      return true;
    default:
      return false;
    }
  }
};

#endif // RADIALINPUTSENDER_H
