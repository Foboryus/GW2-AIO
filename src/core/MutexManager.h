#pragma once

/**
 * @brief Mutex Manager - Ported from LaunchBuddy HandleManager.cs
 *
 * Allows closing the GW2 mutex to enable multi-boxing.
 * Uses NtQuerySystemInformation and DuplicateHandle APIs.
 *
 * DO NOT ADD:
 * - Inline implementations (use MutexManager.cpp)
 * - Profile management logic (belongs in ProfileManager)
 * - Launch logic (belongs in LaunchManager)
 *
 * Original: https://github.com/TheCheatsrichter/Gw2_Launchbuddy
 * License: GPL-3.0
 */

#include <QList>
#include <QObject>
#include <QString>


// clang-format off
// Windows headers MUST be in this order: windows.h before winternl.h before tlhelp32.h
#ifdef Q_OS_WIN
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#endif
// clang-format on

// GW2 Mutex name that prevents multiple instances
static const wchar_t *GW2_MUTEX_NAME = L"AN-Mutex-Window-Guild Wars 2";

class MutexManager : public QObject {
  Q_OBJECT

public:
  explicit MutexManager(QObject *parent = nullptr);

  bool closeMutex(DWORD processId);
  int closeAllMutexes(const QList<DWORD> &excludeProcessIds = {});
  QList<DWORD> findGW2Processes() const;

signals:
  void mutexClosed(DWORD processId);
  void error(const QString &message);

private:
#ifdef Q_OS_WIN
  // Use Extended handle info (class 64) — supports PIDs > 65535.
  // The older SYSTEM_HANDLE_TABLE_ENTRY_INFO (class 16) truncates PIDs
  // to USHORT which silently fails on modern Windows.
  struct SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX {
    PVOID Object;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR HandleValue;
    ULONG GrantedAccess;
    USHORT CreatorBackTraceIndex;
    USHORT ObjectTypeIndex;
    ULONG HandleAttributes;
    ULONG Reserved;
  };

  struct SYSTEM_HANDLE_INFORMATION_EX {
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
  };

  typedef NTSTATUS(WINAPI *NtQuerySystemInformationFunc)(
      ULONG SystemInformationClass, PVOID SystemInformation,
      ULONG SystemInformationLength, PULONG ReturnLength);

  typedef NTSTATUS(WINAPI *NtQueryObjectFunc)(HANDLE Handle,
                                              ULONG ObjectInformationClass,
                                              PVOID ObjectInformation,
                                              ULONG ObjectInformationLength,
                                              PULONG ReturnLength);

  NtQuerySystemInformationFunc m_NtQuerySystemInformation = nullptr;
  NtQueryObjectFunc m_NtQueryObject = nullptr;
  HMODULE m_ntdll = nullptr;

  bool initNtFunctions();
  QList<SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX> getProcessHandles(DWORD processId);
  QString getHandleName(HANDLE hProcess, ULONG_PTR handleValue);
  bool closeRemoteHandle(DWORD processId, ULONG_PTR handleValue);
#endif
};
