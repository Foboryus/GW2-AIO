#include "GW2WindowWatcher.h"
#include <QCoreApplication>
#include <QDebug>


#ifdef Q_OS_WIN
#include <tlhelp32.h>
#endif

GW2WindowWatcher *GW2WindowWatcher::s_instance = nullptr;

#ifdef Q_OS_WIN
HWINEVENTHOOK GW2WindowWatcher::s_hook = nullptr;
#endif

GW2WindowWatcher *GW2WindowWatcher::instance() {
  if (!s_instance) {
    s_instance = new GW2WindowWatcher(qApp);
  }
  return s_instance;
}

GW2WindowWatcher::GW2WindowWatcher(QObject *parent) : QObject(parent) {}

GW2WindowWatcher::~GW2WindowWatcher() { stopWatching(); }

void GW2WindowWatcher::watchForGW2(const AccountProfile &profile,
                                   const QSet<qint64> &existingPids) {
  qInfo() << "GW2WindowWatcher::watchForGW2 called, current state: m_watching="
          << m_watching;

  // Force stop any existing watch to ensure clean state (fixes relaunch issues)
  if (m_watching) {
    qWarning() << "GW2WindowWatcher: Forcing stop of existing watch for clean "
                  "relaunch";
    stopWatching();
  }

  m_pendingProfile = profile;
  m_existingPids = existingPids;
  m_watching = true;

#ifdef Q_OS_WIN
  // Install SetWinEventHook to detect window creation
  s_hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr,
                           WinEventCallback, 0, 0, WINEVENT_OUTOFCONTEXT);

  if (s_hook) {
    qInfo() << "GW2WindowWatcher: Hook installed successfully, watching for "
               "ArenaNet window";
    qInfo() << "GW2WindowWatcher: Ignoring existing PIDs:" << existingPids;
  } else {
    qWarning() << "GW2WindowWatcher: Failed to install hook, error:"
               << GetLastError();
    m_watching = false;
  }
#endif
}

void GW2WindowWatcher::stopWatching() {
  qInfo() << "GW2WindowWatcher::stopWatching called, m_watching=" << m_watching;

  if (!m_watching) {
    qInfo() << "GW2WindowWatcher: Already stopped, nothing to do";
    return;
  }

  // Store profile ID before clearing (for cancelled signal)
  QString pendingProfileId = m_pendingProfile.id;

  m_watching = false;
  m_existingPids.clear(); // Clear for next launch

#ifdef Q_OS_WIN
  if (s_hook) {
    BOOL result = UnhookWinEvent(s_hook);
    qInfo() << "GW2WindowWatcher: Hook uninstalled, result=" << result;
    s_hook = nullptr;
  }
#endif

  // Emit cancelled signal so waiters can exit gracefully
  // (e.g., when platform was closed before game launched)
  if (!pendingProfileId.isEmpty()) {
    qInfo() << "GW2WindowWatcher: Emitting watchCancelled for profile:"
            << pendingProfileId;
    emit watchCancelled(pendingProfileId);
  }
}

#ifdef Q_OS_WIN
void CALLBACK GW2WindowWatcher::WinEventCallback(HWINEVENTHOOK hook,
                                                 DWORD event, HWND hwnd,
                                                 LONG idObject, LONG idChild,
                                                 DWORD dwEventThread,
                                                 DWORD dwmsEventTime) {
  Q_UNUSED(hook);
  Q_UNUSED(event);
  Q_UNUSED(idChild);
  Q_UNUSED(dwEventThread);
  Q_UNUSED(dwmsEventTime);

  if (!s_instance)
    return;
  if (!s_instance->m_watching) {
    // Static log - watcher not active
    return;
  }
  if (idObject != OBJID_WINDOW || !hwnd)
    return;

  s_instance->handleWindowCreated(hwnd);
}

void GW2WindowWatcher::handleWindowCreated(HWND hwnd) {
  // Check window class - ArenaNet is GW2's window class
  wchar_t className[256];
  if (GetClassNameW(hwnd, className, 256) == 0)
    return;

  QString classStr = QString::fromWCharArray(className);

  // Log ArenaNet class windows specifically (reduce noise from other windows)
  if (classStr == "ArenaNet") {
    qInfo() << "=== GW2WindowWatcher: ArenaNet window event ===";

    // It's a GW2 window! Get the PID
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    qInfo() << "ArenaNet window PID:" << pid;

    if (pid == 0) {
      qWarning() << "GetWindowThreadProcessId returned 0";
      return;
    }

    qint64 qpid = static_cast<qint64>(pid);

    // Check if this is a NEW PID (not in existing list)
    qInfo() << "Checking if PID" << qpid
            << "is in existingPids:" << m_existingPids;
    if (m_existingPids.contains(qpid)) {
      qInfo() << "GW2WindowWatcher: Ignoring EXISTING GW2 PID:" << qpid;
      return;
    }

    qInfo() << "GW2WindowWatcher: NEW ArenaNet window detected! PID:" << qpid;

    // Call slot on main thread
    QMetaObject::invokeMethod(this, "onGW2Detected", Qt::QueuedConnection,
                              Q_ARG(qint64, qpid));
  }
}
#endif

void GW2WindowWatcher::onGW2Detected(qint64 pid) {
  qInfo() << "GW2WindowWatcher: Processing GW2 PID:" << pid;

  // Save the profile BEFORE stopping (stopWatching clears state)
  AccountProfile detectedProfile = m_pendingProfile;

  // Clear pending profile ID so stopWatching won't emit watchCancelled
  // (this is a SUCCESS case, not a cancellation)
  m_pendingProfile.id.clear();

  // Stop watching (we found our window)
  stopWatching();

#ifdef Q_OS_WIN
  // Apply process priority (Steam/Epic launches don't go through
  // CreateProcessW)
  if (detectedProfile.processPriority > 0) {
    HANDLE hProcess =
        OpenProcess(PROCESS_SET_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (hProcess) {
      DWORD priorityClass = NORMAL_PRIORITY_CLASS;
      const char *priorityName = "Normal";
      switch (detectedProfile.processPriority) {
      case 1:
        priorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
        priorityName = "Above Normal";
        break;
      case 2:
        priorityClass = HIGH_PRIORITY_CLASS;
        priorityName = "High";
        break;
      case 3:
        priorityClass = REALTIME_PRIORITY_CLASS;
        priorityName = "Realtime";
        break;
      }
      if (SetPriorityClass(hProcess, priorityClass)) {
        qInfo() << "GW2WindowWatcher: Set process priority to" << priorityName
                << "for PID:" << pid;
      } else {
        qWarning() << "GW2WindowWatcher: Failed to set priority for PID:" << pid
                   << "Error:" << GetLastError();
      }
      CloseHandle(hProcess);
    } else {
      qWarning() << "GW2WindowWatcher: Could not open process for PID:" << pid
                 << "Error:" << GetLastError();
    }
  }
#endif

  // Emit signal with PID and saved profile
  emit gw2WindowDetected(pid, detectedProfile);
}
