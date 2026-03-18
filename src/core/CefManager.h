#pragma once

/**
 * @file CefManager.h
 * @brief CEF (Chromium Embedded Framework) orphan process cleanup
 *
 * GW2 spawns CefHost.exe for its launcher/login UI. When GW2 crashes,
 * these become orphaned - still running but parent dead.
 *
 * Detection triggers (2-of-3 consensus for runtime):
 * 1. Startup scan for orphans
 * 2. ProfileManager signal (PID marked dead)
 * 3. Named Pipe EXITING signal
 * 4. Focus regain validation
 *
 * DO NOT ADD:
 * - UI code (belongs in SettingsWidget)
 * - Inline implementations (use CefManager.cpp)
 */

#include <QMap>
#include <QSet>

enum class CefTriggerSource {
  ProfileManager,
  NamedPipe,
  FocusValidation,
  ProcessMonitor
};

class CefManager {
public:
  static CefManager &instance();

  /// @brief Scan for orphaned CefHost.exe on startup
  void checkForOrphansOnStartup();

  /// @brief Register a GW2 exit signal from one of the triggers
  void registerExitSignal(qint64 gw2Pid, CefTriggerSource source);

  /// @brief Clear exit signals for a PID
  void clearExitSignals(qint64 gw2Pid);

  /// @brief Enable or disable automatic CEF cleanup
  void setEnabled(bool enabled);
  bool isEnabled() const { return m_enabled; }

private:
  CefManager() = default;

  void cleanupOrphanedCef(qint64 specificParentPid = 0);
  bool isOrphanedCef(qint64 cefPid, qint64 parentPid);
  bool terminateProcess(qint64 pid);

  QMap<qint64, QSet<CefTriggerSource>> m_exitSignals;
  bool m_enabled = true;
};
