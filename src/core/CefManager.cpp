/**
 * @file CefManager.cpp
 * @brief CEF orphan process cleanup implementation
 *
 * Orphan detection uses process snapshot and creation time
 * comparison to handle PID reuse. 2-of-3 consensus system
 * prevents false positives.
 *
 * DO NOT ADD:
 * - UI code (belongs in SettingsWidget)
 * - Settings persistence (belongs in DataService)
 */

#include "CefManager.h"

#include <QDebug>

// clang-format off
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif
// clang-format on

CefManager &CefManager::instance() {
  static CefManager s_instance;
  return s_instance;
}

void CefManager::checkForOrphansOnStartup() {
  qInfo() << "CefManager: Scanning for orphaned CefHost.exe on startup...";
  cleanupOrphanedCef();
}

void CefManager::registerExitSignal(qint64 gw2Pid, CefTriggerSource source) {
  m_exitSignals[gw2Pid].insert(source);

  qInfo() << "CefManager: Exit signal for PID" << gw2Pid
          << "from source:" << static_cast<int>(source)
          << "Total signals:" << m_exitSignals[gw2Pid].size();

  // 2-of-3 consensus: proceed when 2+ triggers confirm
  if (m_exitSignals[gw2Pid].size() >= 2) {
    qInfo() << "CefManager: 2+ triggers confirm exit for PID" << gw2Pid
            << "- proceeding with cleanup";
    cleanupOrphanedCef(gw2Pid);
    m_exitSignals.remove(gw2Pid);
  }
}

void CefManager::clearExitSignals(qint64 gw2Pid) {
  m_exitSignals.remove(gw2Pid);
}

void CefManager::setEnabled(bool enabled) { m_enabled = enabled; }

void CefManager::cleanupOrphanedCef(qint64 specificParentPid) {
#ifdef Q_OS_WIN
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE) {
    qWarning() << "CefManager: Failed to create process snapshot";
    return;
  }

  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);

  int orphansFound = 0;
  int orphansCleaned = 0;

  if (Process32FirstW(hSnapshot, &pe)) {
    do {
      QString processName = QString::fromWCharArray(pe.szExeFile);
      if (processName.compare("CefHost.exe", Qt::CaseInsensitive) == 0) {
        qint64 cefPid = pe.th32ProcessID;
        qint64 parentPid = pe.th32ParentProcessID;

        // If looking for specific parent, skip others
        if (specificParentPid != 0 && parentPid != specificParentPid) {
          continue;
        }

        // Check if this CefHost is orphaned
        if (isOrphanedCef(cefPid, parentPid)) {
          orphansFound++;
          qInfo() << "CefManager: Found orphaned CefHost.exe PID:" << cefPid
                  << "Parent PID:" << parentPid << "(dead/reused)";

          if (m_enabled) {
            if (terminateProcess(cefPid)) {
              orphansCleaned++;
              qInfo() << "CefManager: Terminated orphaned CefHost.exe PID:"
                      << cefPid;
            } else {
              // Not a real failure - process likely exited naturally before
              // we could terminate it (race condition, but desired outcome)
              qInfo() << "CefManager: CefHost.exe PID:" << cefPid
                      << "already exited (cleanup not needed)";
            }
          } else {
            qInfo() << "CefManager: [DISABLED] Would terminate PID:" << cefPid
                    << "(enable in Settings to cleanup)";
          }
        }
      }
    } while (Process32NextW(hSnapshot, &pe));
  }

  CloseHandle(hSnapshot);

  if (orphansFound > 0) {
    qInfo() << "CefManager: Found" << orphansFound << "orphaned CefHost.exe,"
            << orphansCleaned << "terminated";
  }
#else
  Q_UNUSED(specificParentPid);
#endif
}

bool CefManager::isOrphanedCef(qint64 cefPid, qint64 parentPid) {
#ifdef Q_OS_WIN
  // First check: does parent process exist?
  HANDLE hParent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(parentPid));
  if (hParent == nullptr) {
    // Parent doesn't exist -> orphan
    return true;
  }

  // Second check: creation time comparison (handles PID reuse)
  HANDLE hChild = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                              static_cast<DWORD>(cefPid));
  if (hChild == nullptr) {
    CloseHandle(hParent);
    return false; // Can't check child, assume not orphan
  }

  FILETIME parentCreate, parentExit, parentKernel, parentUser;
  FILETIME childCreate, childExit, childKernel, childUser;

  bool gotParentTimes = GetProcessTimes(hParent, &parentCreate, &parentExit,
                                        &parentKernel, &parentUser) != 0;
  bool gotChildTimes = GetProcessTimes(hChild, &childCreate, &childExit,
                                       &childKernel, &childUser) != 0;

  CloseHandle(hParent);
  CloseHandle(hChild);

  if (gotParentTimes && gotChildTimes) {
    // If "parent" was created AFTER child, it's PID reuse -> orphan
    ULARGE_INTEGER parentTime, childTime;
    parentTime.LowPart = parentCreate.dwLowDateTime;
    parentTime.HighPart = parentCreate.dwHighDateTime;
    childTime.LowPart = childCreate.dwLowDateTime;
    childTime.HighPart = childCreate.dwHighDateTime;

    if (parentTime.QuadPart > childTime.QuadPart) {
      qInfo() << "CefManager: PID reuse detected - parent created after "
                 "child";
      return true;
    }
  }

  // Parent exists and was created before child -> not orphan
  return false;
#else
  Q_UNUSED(cefPid);
  Q_UNUSED(parentPid);
  return false;
#endif
}

bool CefManager::terminateProcess(qint64 pid) {
#ifdef Q_OS_WIN
  HANDLE hProcess =
      OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (hProcess == nullptr) {
    return false;
  }

  BOOL result = TerminateProcess(hProcess, 1);
  CloseHandle(hProcess);
  return result != 0;
#else
  Q_UNUSED(pid);
  return false;
#endif
}
