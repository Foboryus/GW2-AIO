/**
 * @file MutexManager.cpp
 * @brief Mutex Manager - Ported from LaunchBuddy HandleManager.cs
 *
 * Allows closing the GW2 mutex to enable multi-boxing.
 * Uses NtQuerySystemInformation and DuplicateHandle APIs.
 *
 * DO NOT ADD:
 * - Profile management logic (belongs in ProfileManager)
 * - Launch logic (belongs in LaunchManager)
 * - UI code
 */

#include "MutexManager.h"

#include <QDebug>

MutexManager::MutexManager(QObject *parent) : QObject(parent) {
#ifdef Q_OS_WIN
  initNtFunctions();
#endif
}

#ifdef Q_OS_WIN

bool MutexManager::initNtFunctions() {
  m_ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!m_ntdll) {
    emit error("Failed to get ntdll.dll handle");
    return false;
  }

  m_NtQuerySystemInformation = (NtQuerySystemInformationFunc)GetProcAddress(
      m_ntdll, "NtQuerySystemInformation");
  m_NtQueryObject = (NtQueryObjectFunc)GetProcAddress(m_ntdll, "NtQueryObject");

  // Enable SeDebugPrivilege - required to enumerate handles from other
  // processes
  HANDLE hToken;
  if (OpenProcessToken(GetCurrentProcess(),
                       TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
      tp.PrivilegeCount = 1;
      tp.Privileges[0].Luid = luid;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

      if (AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr,
                                nullptr)) {
        DWORD err = GetLastError();
        if (err == ERROR_SUCCESS) {
          qInfo() << "SeDebugPrivilege enabled successfully";
        } else if (err == ERROR_NOT_ALL_ASSIGNED) {
          qWarning() << "SeDebugPrivilege not available - run as administrator "
                        "for multibox";
        }
      } else {
        qWarning() << "Failed to adjust token privileges:" << GetLastError();
      }
    } else {
      qWarning() << "Failed to lookup SeDebugPrivilege:" << GetLastError();
    }
    CloseHandle(hToken);
  } else {
    qWarning() << "Failed to open process token:" << GetLastError();
  }

  return m_NtQuerySystemInformation && m_NtQueryObject;
}

QList<DWORD> MutexManager::findGW2Processes() const {
  QList<DWORD> pids;

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return pids;
  }

  PROCESSENTRY32W pe32;
  pe32.dwSize = sizeof(pe32);

  if (Process32FirstW(snapshot, &pe32)) {
    do {
      QString name = QString::fromWCharArray(pe32.szExeFile);
      if (name.compare("Gw2-64.exe", Qt::CaseInsensitive) == 0 ||
          name.compare("Gw2.exe", Qt::CaseInsensitive) == 0) {
        pids.append(pe32.th32ProcessID);
      }
    } while (Process32NextW(snapshot, &pe32));
  }

  CloseHandle(snapshot);
  return pids;
}

QList<MutexManager::SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX>
MutexManager::getProcessHandles(DWORD processId) {
  QList<SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX> handles;

  if (!m_NtQuerySystemInformation) {
    return handles;
  }

  // Start with 64KB buffer, grow as needed
  ULONG bufferSize = 0x10000;
  ULONG returnLength = 0;
  PVOID buffer = nullptr;
  NTSTATUS status;

  // Keep growing buffer until it's big enough
  do {
    if (buffer) {
      VirtualFree(buffer, 0, MEM_RELEASE);
    }
    bufferSize *= 2;
    buffer = VirtualAlloc(nullptr, bufferSize, MEM_COMMIT, PAGE_READWRITE);
    if (!buffer) {
      return handles;
    }

    // SystemExtendedHandleInformation = 64 (supports PIDs > 65535)
    status = m_NtQuerySystemInformation(64, buffer, bufferSize, &returnLength);
  } while (status == 0xC0000004); // STATUS_INFO_LENGTH_MISMATCH

  if (status != 0) { // Not STATUS_SUCCESS
    VirtualFree(buffer, 0, MEM_RELEASE);
    return handles;
  }

  // Parse the handle list
  auto *info = static_cast<SYSTEM_HANDLE_INFORMATION_EX *>(buffer);
  qInfo() << "Total system handles:" << info->NumberOfHandles;
  for (ULONG_PTR i = 0; i < info->NumberOfHandles; i++) {
    if (info->Handles[i].UniqueProcessId == static_cast<ULONG_PTR>(processId)) {
      handles.append(info->Handles[i]);
    }
  }

  VirtualFree(buffer, 0, MEM_RELEASE);
  return handles;
}

QString MutexManager::getHandleName(HANDLE hProcess, ULONG_PTR handleValue) {
  if (!m_NtQueryObject) {
    return QString();
  }

  // Duplicate the handle into our process
  HANDLE hDup = nullptr;
  if (!DuplicateHandle(hProcess, (HANDLE)(uintptr_t)handleValue,
                       GetCurrentProcess(), &hDup, 0, FALSE,
                       DUPLICATE_SAME_ACCESS)) {
    return QString();
  }

  // Query for object name
  ULONG bufferSize = 0x1000;
  PVOID buffer = VirtualAlloc(nullptr, bufferSize, MEM_COMMIT, PAGE_READWRITE);
  if (!buffer) {
    CloseHandle(hDup);
    return QString();
  }

  ULONG returnLength = 0;
  // ObjectNameInformation = 1
  NTSTATUS status = m_NtQueryObject(hDup, 1, buffer, bufferSize, &returnLength);

  QString name;
  if (status == 0) { // STATUS_SUCCESS
    // UNICODE_STRING is at start of buffer
    auto *nameInfo = static_cast<UNICODE_STRING *>(buffer);
    if (nameInfo->Length > 0 && nameInfo->Buffer) {
      name = QString::fromWCharArray(nameInfo->Buffer,
                                     nameInfo->Length / sizeof(WCHAR));
    }
  }

  VirtualFree(buffer, 0, MEM_RELEASE);
  CloseHandle(hDup);

  return name;
}

bool MutexManager::closeRemoteHandle(DWORD processId, ULONG_PTR handleValue) {
  HANDLE hProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, processId);
  if (!hProcess) {
    return false;
  }

  // Close the remote handle by duplicating with DUPLICATE_CLOSE_SOURCE
  HANDLE hDummy = nullptr;
  BOOL success =
      DuplicateHandle(hProcess, (HANDLE)(uintptr_t)handleValue, nullptr,
                      &hDummy, 0, FALSE, DUPLICATE_CLOSE_SOURCE);

  CloseHandle(hProcess);
  return success != 0;
}

bool MutexManager::closeMutex(DWORD processId) {
  qInfo() << "Attempting to close GW2 mutex for process" << processId;

  HANDLE hProcess = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION,
                                FALSE, processId);
  if (!hProcess) {
    qWarning() << "Failed to open process" << processId
               << "- Error:" << GetLastError();
    emit error(QString("Failed to open process %1").arg(processId));
    return false;
  }

  QList<SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX> handles = getProcessHandles(processId);
  qInfo() << "Found" << handles.size() << "handles for process" << processId;

  bool found = false;
  int mutexLikeHandles = 0;

  for (const auto &entry : handles) {
    QString name = getHandleName(hProcess, entry.HandleValue);

    // Log mutex-like handles for debugging
    if (name.contains("Mutex", Qt::CaseInsensitive) ||
        name.contains("AN-", Qt::CaseInsensitive)) {
      mutexLikeHandles++;
      qInfo() << "  Found mutex/AN handle:" << name;
    }

    // Check if this is the GW2 mutex
    if (name.contains("AN-Mutex-Window-Guild Wars 2", Qt::CaseInsensitive)) {
      qInfo() << "Found GW2 mutex, closing...";
      if (closeRemoteHandle(processId, entry.HandleValue)) {
        found = true;
        emit mutexClosed(processId);
        qInfo() << "Successfully closed GW2 mutex for process" << processId;
      } else {
        qWarning() << "Failed to close mutex handle";
      }
      break;
    }
  }

  qInfo() << "Total mutex-like handles found:" << mutexLikeHandles;

  CloseHandle(hProcess);

  if (!found) {
    qInfo() << "GW2 mutex not found for process" << processId;
  }

  return found;
}

int MutexManager::closeAllMutexes(const QList<DWORD> &excludeProcessIds) {
  int closed = 0;
  QList<DWORD> pids = findGW2Processes();

  for (DWORD pid : pids) {
    if (!excludeProcessIds.contains(pid)) {
      if (closeMutex(pid)) {
        closed++;
      }
    }
  }

  return closed;
}

#else
// Non-Windows stubs
bool MutexManager::closeMutex(DWORD) { return false; }
int MutexManager::closeAllMutexes(const QList<DWORD> &) { return 0; }
QList<DWORD> MutexManager::findGW2Processes() const { return {}; }
#endif
