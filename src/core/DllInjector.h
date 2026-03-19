#pragma once

/**
 * @brief DLL Injector - Ported from LaunchBuddy DLLInjector.cs
 *
 * Injects addon DLLs into the GW2 process using LoadLibraryA.
 *
 * Original: https://github.com/TheCheatsrichter/Gw2_Launchbuddy
 */

#include <QObject>
#include <QString>
#include <QStringList>

#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
// clang-format on
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
