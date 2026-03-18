/**
 * @file main.cpp
 * @brief Launcher stub for GW2 AIO Manager
 *
 * A tiny pure-Win32 executable (no Qt dependency) that:
 * 1. Sets DLL search path to lib/ subfolder via SetDllDirectory
 * 2. Sets GW2AIO_ROOT environment variable for the real app
 * 3. Launches lib/GW2AIO_app.exe with forwarded command-line args
 * 4. Waits for the child process to exit
 *
 * This allows all DLLs to live in lib/ while the user-facing exe
 * sits alone in the root directory.
 */

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow) {
  (void)hInstance;
  (void)hPrevInstance;
  (void)nCmdShow;

  // --- Get directory of this stub exe ---
  wchar_t stubPath[MAX_PATH];
  DWORD len = GetModuleFileNameW(NULL, stubPath, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    MessageBoxW(NULL, L"Failed to determine application path.",
                L"GW2 AIO", MB_OK | MB_ICONERROR);
    return 1;
  }

  // Strip filename to get directory (keep trailing backslash)
  wchar_t *lastSlash = wcsrchr(stubPath, L'\\');
  if (lastSlash) {
    *(lastSlash + 1) = L'\0';
  }

  // --- Set DLL search path to lib/ subfolder ---
  wchar_t libDir[MAX_PATH];
  wcscpy_s(libDir, MAX_PATH, stubPath);
  wcscat_s(libDir, MAX_PATH, L"lib");
  SetDllDirectoryW(libDir);

  // --- Set root dir env var for AppConfig portable detection ---
  // Without this, QCoreApplication::applicationDirPath() returns lib/
  // and portable.txt detection would look in the wrong place.
  SetEnvironmentVariableW(L"GW2AIO_ROOT", stubPath);

  // --- Build path to real application exe ---
  wchar_t appExe[MAX_PATH];
  wcscpy_s(appExe, MAX_PATH, stubPath);
  wcscat_s(appExe, MAX_PATH, L"lib\\GW2AIO_app.exe");

  // Verify the real exe exists before trying to launch
  DWORD attrs = GetFileAttributesW(appExe);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    MessageBoxW(NULL,
                L"Failed to launch GW2 AIO Manager.\n"
                L"Ensure lib\\GW2AIO_app.exe exists.",
                L"GW2 AIO", MB_OK | MB_ICONERROR);
    return 1;
  }

  // --- Build full command line: "lib\GW2AIO_app.exe" <original args> ---
  // lpCmdLine does NOT include argv[0], so we prepend the exe path
  wchar_t cmdLine[4096];
  if (lpCmdLine[0] != L'\0') {
    _snwprintf_s(cmdLine, _countof(cmdLine), _TRUNCATE,
                 L"\"%s\" %s", appExe, lpCmdLine);
  } else {
    _snwprintf_s(cmdLine, _countof(cmdLine), _TRUNCATE,
                 L"\"%s\"", appExe);
  }

  // --- Launch the real application ---
  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  if (!CreateProcessW(appExe, cmdLine, NULL, NULL, FALSE,
                      0, NULL, NULL, &si, &pi)) {
    DWORD err = GetLastError();
    wchar_t msg[256];
    _snwprintf_s(msg, _countof(msg), _TRUNCATE,
                 L"Failed to launch GW2 AIO Manager.\n"
                 L"Error code: %lu", err);
    MessageBoxW(NULL, msg, L"GW2 AIO", MB_OK | MB_ICONERROR);
    return 1;
  }

  // Stub exits immediately — the real app runs independently.
  // No need to wait: keeping the stub alive wastes ~5 MB and killing
  // the stub wouldn't kill the real app anyway (misleading).
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  return 0;
}

#endif // _WIN32
