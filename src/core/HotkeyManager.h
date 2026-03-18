#pragma once

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QMap>
#include <QObject>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief Global hotkey manager
 *
 * Registers system-wide hotkeys via Windows RegisterHotKey API.
 * Used by radial menus and per-profile Focus/Minimize shortcuts.
 *
 * DO NOT ADD:
 * - Inline implementations (use HotkeyManager.cpp)
 */
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
  Q_OBJECT

public:
  explicit HotkeyManager(QObject *parent = nullptr);
  ~HotkeyManager();

  /**
   * @brief Register a global hotkey
   * @param id Unique identifier for this hotkey
   * @param key Key string (e.g., "V", "Shift+V")
   * @return true if registration succeeded
   */
  bool registerHotkey(const QString &id, const QString &key);

  /**
   * @brief Unregister a hotkey
   */
  void unregisterHotkey(const QString &id);

  /**
   * @brief Unregister all hotkeys
   */
  void unregisterAll();

  /**
   * @brief Check if a key is currently held down
   */
  bool isKeyDown(const QString &key) const;

signals:
  void hotkeyPressed(const QString &id);
  void hotkeyReleased(const QString &id);

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  bool nativeEventFilter(const QByteArray &eventType, void *message,
                         qintptr *result) override;
#else
  bool nativeEventFilter(const QByteArray &eventType, void *message,
                         long *result) override;
#endif

private:
  struct HotkeyInfo {
    int id;
    QString key;
    UINT vkCode;
    UINT modifiers;
  };

  int m_nextId = 1;
  QMap<QString, HotkeyInfo> m_hotkeys;
  QMap<int, QString> m_idToName;

  bool parseKeyString(const QString &key, UINT &vkCode, UINT &modifiers) const;
  UINT keyToVk(const QString &key) const;
};
