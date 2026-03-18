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
  struct SYSTEM_HANDLE_TABLE_ENTRY_INFO {
    USHORT UniqueProcessId;
    USHORT CreatorBackTraceIndex;
    UCHAR ObjectTypeIndex;
    UCHAR HandleAttributes;
    USHORT HandleValue;
    PVOID Object;
    ULONG GrantedAccess;
  };

  struct SYSTEM_HANDLE_INFORMATION {
    ULONG NumberOfHandles;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO Handles[1];
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
  QList<SYSTEM_HANDLE_TABLE_ENTRY_INFO> getProcessHandles(DWORD processId);
  QString getHandleName(HANDLE hProcess, USHORT handleValue);
  bool closeRemoteHandle(DWORD processId, USHORT handleValue);
#endif
};
