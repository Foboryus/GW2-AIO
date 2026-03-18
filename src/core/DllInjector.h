#pragma once

/**
 * @brief DLL Injector - Ported from LaunchBuddy DLLInjector.cs
 *
 * Injects addon DLLs into the GW2 process using LoadLibraryA.
 *
 * Original: https://github.com/TheCheatsrichter/Gw2_Launchbuddy
 */

#include <QDebug>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>


#ifdef Q_OS_WIN
#include <windows.h>
#endif

enum class DllInjectionResult {
  Success,
  DllNotFound,
  ProcessNotFound,
  OpenProcessFailed,
  VirtualAllocFailed,
  WriteMemoryFailed,
  CreateThreadFailed
};

class DllInjector : public QObject {
  Q_OBJECT

public:
  explicit DllInjector(QObject *parent = nullptr);

  /**
   * @brief Inject a DLL into a running process
   * @param processId Target process ID
   * @param dllPath Full path to the DLL
   * @return Injection result
   */
  DllInjectionResult inject(DWORD processId, const QString &dllPath);

  /**
   * @brief Inject multiple DLLs into a process
   * @param processId Target process ID
   * @param dllPaths List of DLL paths
   * @return Number of successfully injected DLLs
   */
  int injectMultiple(DWORD processId, const QStringList &dllPaths);

  /**
   * @brief Get string description of result
   */
  static QString resultToString(DllInjectionResult result);

signals:
  void injectionSucceeded(const QString &dllPath);
  void injectionFailed(const QString &dllPath, DllInjectionResult result);

private:
#ifdef Q_OS_WIN
  bool doInject(DWORD processId, const char *dllPath);
#endif
};

// Implementation
inline DllInjector::DllInjector(QObject *parent) : QObject(parent) {}

inline QString DllInjector::resultToString(DllInjectionResult result) {
  switch (result) {
  case DllInjectionResult::Success:
    return "Success";
  case DllInjectionResult::DllNotFound:
    return "DLL file not found";
  case DllInjectionResult::ProcessNotFound:
    return "Process not found";
  case DllInjectionResult::OpenProcessFailed:
    return "Failed to open process";
  case DllInjectionResult::VirtualAllocFailed:
    return "Failed to allocate memory";
  case DllInjectionResult::WriteMemoryFailed:
    return "Failed to write memory";
  case DllInjectionResult::CreateThreadFailed:
    return "Failed to create remote thread";
  }
  return "Unknown error";
}

#ifdef Q_OS_WIN

inline DllInjectionResult DllInjector::inject(DWORD processId,
                                              const QString &dllPath) {
  // Check if DLL exists
  if (!QFile::exists(dllPath)) {
    emit injectionFailed(dllPath, DllInjectionResult::DllNotFound);
    return DllInjectionResult::DllNotFound;
  }

  if (processId == 0) {
    emit injectionFailed(dllPath, DllInjectionResult::ProcessNotFound);
    return DllInjectionResult::ProcessNotFound;
  }

  // Convert to native path for Windows API
  std::string pathStr = dllPath.toStdString();

  if (doInject(processId, pathStr.c_str())) {
    qInfo() << "Successfully injected:" << dllPath;
    emit injectionSucceeded(dllPath);
    return DllInjectionResult::Success;
  }

  emit injectionFailed(dllPath, DllInjectionResult::CreateThreadFailed);
  return DllInjectionResult::CreateThreadFailed;
}

inline bool DllInjector::doInject(DWORD processId, const char *dllPath) {
  // Required access flags
  const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                       PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                       PROCESS_VM_READ;

  // Open the target process
  HANDLE hProcess = OpenProcess(access, FALSE, processId);
  if (!hProcess) {
    qWarning() << "OpenProcess failed, error:" << GetLastError();
    return false;
  }

  // Get LoadLibraryA address
  HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
  if (!hKernel32) {
    CloseHandle(hProcess);
    return false;
  }

  LPVOID loadLibraryAddr = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryA");
  if (!loadLibraryAddr) {
    CloseHandle(hProcess);
    return false;
  }

  // Allocate memory in target process for the DLL path
  size_t pathLen = strlen(dllPath) + 1;
  LPVOID remoteMemory = VirtualAllocEx(
      hProcess, nullptr, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remoteMemory) {
    qWarning() << "VirtualAllocEx failed, error:" << GetLastError();
    CloseHandle(hProcess);
    return false;
  }

  // Write the DLL path to the allocated memory
  SIZE_T bytesWritten = 0;
  if (!WriteProcessMemory(hProcess, remoteMemory, dllPath, pathLen,
                          &bytesWritten)) {
    qWarning() << "WriteProcessMemory failed, error:" << GetLastError();
    VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return false;
  }

  // Create a remote thread to call LoadLibraryA with our DLL path
  HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
                                      (LPTHREAD_START_ROUTINE)loadLibraryAddr,
                                      remoteMemory, 0, nullptr);
  if (!hThread) {
    qWarning() << "CreateRemoteThread failed, error:" << GetLastError();
    VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return false;
  }

  // Wait for the thread to complete (5 second timeout)
  DWORD waitResult = WaitForSingleObject(hThread, 5000);
  if (waitResult == WAIT_TIMEOUT) {
    qWarning() << "DLL injection timed out after 5 seconds";
  } else if (waitResult == WAIT_FAILED) {
    qWarning() << "WaitForSingleObject failed, error:" << GetLastError();
  }

  // Check if LoadLibrary succeeded by getting thread exit code
  DWORD exitCode = 0;
  GetExitCodeThread(hThread, &exitCode);
  bool loadSuccess = (exitCode != 0);

  // Clean up
  VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
  CloseHandle(hThread);
  CloseHandle(hProcess);

  if (!loadSuccess) {
    qWarning() << "LoadLibrary returned 0 - DLL load may have failed";
  }

  return loadSuccess;
}

inline int DllInjector::injectMultiple(DWORD processId,
                                       const QStringList &dllPaths) {
  int successCount = 0;

  for (const QString &path : dllPaths) {
    if (inject(processId, path) == DllInjectionResult::Success) {
      successCount++;
    }
  }

  return successCount;
}

#else
// Non-Windows stubs
inline DllInjectionResult DllInjector::inject(DWORD, const QString &) {
  return DllInjectionResult::ProcessNotFound;
}
inline int DllInjector::injectMultiple(DWORD, const QStringList &) { return 0; }
#endif
