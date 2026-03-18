/**
 * @file HotkeyManager.cpp
 * @brief Global hotkey manager
 *
 * Registers system-wide hotkeys via Windows RegisterHotKey API.
 * Used by radial menus and per-profile Focus/Minimize shortcuts.
 *
 * DO NOT ADD:
 * - Menu display logic (belongs in RadialWidget)
 * - Menu state (belongs in RadialEngine)
 */

#include "HotkeyManager.h"

#include <QDebug>

HotkeyManager::HotkeyManager(QObject *parent) : QObject(parent) {
  qApp->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager() {
  unregisterAll();
  qApp->removeNativeEventFilter(this);
}

bool HotkeyManager::registerHotkey(const QString &id, const QString &key) {
#ifdef Q_OS_WIN
  // Unregister if already exists
  if (m_hotkeys.contains(id)) {
    unregisterHotkey(id);
  }

  UINT vkCode, modifiers;
  if (!parseKeyString(key, vkCode, modifiers)) {
    qWarning() << "Failed to parse hotkey:" << key;
    return false;
  }

  int hotkeyId = m_nextId++;

  if (!RegisterHotKey(nullptr, hotkeyId, modifiers, vkCode)) {
    qWarning() << "Failed to register hotkey:" << key;
    return false;
  }

  HotkeyInfo info{hotkeyId, key, vkCode, modifiers};
  m_hotkeys[id] = info;
  m_idToName[hotkeyId] = id;

  qInfo() << "Registered hotkey:" << id << "=" << key;
  return true;
#else
  Q_UNUSED(id);
  Q_UNUSED(key);
  return false;
#endif
}

void HotkeyManager::unregisterHotkey(const QString &id) {
#ifdef Q_OS_WIN
  if (!m_hotkeys.contains(id))
    return;

  HotkeyInfo info = m_hotkeys[id];
  UnregisterHotKey(nullptr, info.id);
  m_idToName.remove(info.id);
  m_hotkeys.remove(id);
#else
  Q_UNUSED(id);
#endif
}

void HotkeyManager::unregisterAll() {
#ifdef Q_OS_WIN
  for (const auto &id : m_hotkeys.keys()) {
    unregisterHotkey(id);
  }
#endif
}

bool HotkeyManager::isKeyDown(const QString &key) const {
#ifdef Q_OS_WIN
  UINT vk = keyToVk(key);
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
  Q_UNUSED(key);
  return false;
#endif
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool HotkeyManager::nativeEventFilter(const QByteArray &eventType,
                                      void *message, qintptr *result)
#else
bool HotkeyManager::nativeEventFilter(const QByteArray &eventType,
                                      void *message, long *result)
#endif
{
  Q_UNUSED(result);

#ifdef Q_OS_WIN
  if (eventType == "windows_generic_MSG") {
    MSG *msg = static_cast<MSG *>(message);

    if (msg->message == WM_HOTKEY) {
      int hotkeyId = static_cast<int>(msg->wParam);
      if (m_idToName.contains(hotkeyId)) {
        emit hotkeyPressed(m_idToName[hotkeyId]);
        return true;
      }
    }
  }
#else
  Q_UNUSED(eventType);
  Q_UNUSED(message);
#endif

  return false;
}

bool HotkeyManager::parseKeyString(const QString &key, UINT &vkCode,
                                   UINT &modifiers) const {
#ifdef Q_OS_WIN
  modifiers = 0;
  QString keyPart = key.toUpper();

  // Parse modifiers
  if (keyPart.contains("SHIFT+")) {
    modifiers |= MOD_SHIFT;
    keyPart.remove("SHIFT+");
  }
  if (keyPart.contains("CTRL+") || keyPart.contains("CONTROL+")) {
    modifiers |= MOD_CONTROL;
    keyPart.remove("CTRL+");
    keyPart.remove("CONTROL+");
  }
  if (keyPart.contains("ALT+")) {
    modifiers |= MOD_ALT;
    keyPart.remove("ALT+");
  }

  vkCode = keyToVk(keyPart.trimmed());
  return vkCode != 0;
#else
  Q_UNUSED(key);
  Q_UNUSED(vkCode);
  Q_UNUSED(modifiers);
  return false;
#endif
}

UINT HotkeyManager::keyToVk(const QString &key) const {
#ifdef Q_OS_WIN
  if (key.length() == 1) {
    QChar c = key[0].toUpper();
    if (c >= 'A' && c <= 'Z') {
      return static_cast<UINT>(c.unicode());
    }
    if (c >= '0' && c <= '9') {
      return static_cast<UINT>(c.unicode());
    }
  }

  // Special keys
  if (key == "SPACE")
    return VK_SPACE;
  if (key == "TAB")
    return VK_TAB;
  if (key == "ESCAPE" || key == "ESC")
    return VK_ESCAPE;
  if (key == "ENTER" || key == "RETURN")
    return VK_RETURN;
  if (key == "F1")
    return VK_F1;
  if (key == "F2")
    return VK_F2;
  if (key == "F3")
    return VK_F3;
  if (key == "F4")
    return VK_F4;
  if (key == "F5")
    return VK_F5;
  if (key == "F6")
    return VK_F6;
  if (key == "F7")
    return VK_F7;
  if (key == "F8")
    return VK_F8;
  if (key == "F9")
    return VK_F9;
  if (key == "F10")
    return VK_F10;
  if (key == "F11")
    return VK_F11;
  if (key == "F12")
    return VK_F12;
  if (key == "DELETE")
    return VK_DELETE;
  if (key == "HOME")
    return VK_HOME;
  if (key == "END")
    return VK_END;
  if (key == "INSERT")
    return VK_INSERT;
  if (key == "PAGEUP")
    return VK_PRIOR;
  if (key == "PAGEDOWN")
    return VK_NEXT;

  return 0;
#else
  Q_UNUSED(key);
  return 0;
#endif
}
