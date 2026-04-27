#include "LaunchManager.h"
#include "CefManager.h"
#include "GFXManager.h"
#include "GW2WindowWatcher.h"
#include "MumbleLink.h"
#include "ui/PlatformLaunchDialog.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <cstring>
#include <thread>

#ifdef Q_OS_WIN
#include <shellapi.h>
#include <tlhelp32.h>
#include <windows.h>

#endif

LaunchManager::LaunchManager(QObject *parent) : QObject(parent) {
  m_mutexManager = new MutexManager(this);
  m_dllInjector = new DllInjector(this);
  // Note: m_localDatManager is injected externally via setLocalDatManager()

  connect(m_mutexManager, &MutexManager::mutexClosed, this, [](DWORD pid) {
    qInfo() << "Mutex closed for process" << pid << "- multi-boxing enabled";
  });

  // Connect DLL injection signals
  connect(m_dllInjector, &DllInjector::injectionSucceeded, this,
          [this](const QString &dll) {
            emit dllInjected(dll, true);
            qInfo() << "DLL injection SUCCESS:" << dll;
          });

  connect(m_dllInjector, &DllInjector::injectionFailed, this,
          [this](const QString &dll, DllInjectionResult result) {
            Q_UNUSED(result);
            emit dllInjected(dll, false);
            qInfo() << "DLL injection FAILED:" << dll;
          });

  // Connect GW2 window detection (event-based, NO TIMERS)
  connect(GW2WindowWatcher::instance(), &GW2WindowWatcher::gw2WindowDetected,
          this, &LaunchManager::onGW2WindowDetected);
}

void LaunchManager::setGw2Path(const QString &path) {
  if (m_gw2Path != path) {
    m_gw2Path = path;

    // Find executable
    QDir dir(path);
    if (dir.exists("Gw2-64.exe")) {
      m_gw2Executable = dir.filePath("Gw2-64.exe");
    } else if (dir.exists("gw2-64.exe")) {
      m_gw2Executable = dir.filePath("gw2-64.exe");
    } else if (dir.exists("Gw2.exe")) {
      m_gw2Executable = dir.filePath("Gw2.exe");
    }

    // LocalDatManager no longer needs GW2 path — AppData path is fixed

    emit gw2PathChanged();
  }
}

QProcess *LaunchManager::launchGW2(const LaunchProfile &profile) {
  if (m_gw2Executable.isEmpty()) {
    qWarning() << "GW2 executable not found";
    emit launchError("GW2 executable not found. Set the game path first.");
    return nullptr;
  }

  // Close mutex for multi-boxing if there are already running instances
  if (m_multiBoxEnabled && isGW2Running()) {
    qInfo() << "Multi-boxing: closing mutex for existing instances";
    closeMutexForMultiBox();
    QThread::msleep(500);
  }

  QStringList args = buildArguments(profile);

  // Add shareArchive for multi-boxing — ALL instances need this, including the
  // first. GW2 opens Gw2.dat with an EXCLUSIVE lock without -shareArchive,
  // blocking all subsequent instances from opening it. With -shareArchive, GW2
  // opens read-only (shared), allowing multibox. Trade-off: can't patch with
  // -shareArchive. AIO's pre-launch update check ensures game is patched before
  // launching.
  if (m_multiBoxEnabled && !args.contains("-shareArchive")) {
    args.append("-shareArchive");
  }

  // Add custom mumble link if multi-boxing (only if not already set)
  if (m_multiBoxEnabled && m_runningProcesses.size() > 0 &&
      !args.contains("-mumble")) {
    QString mumbleName = generateMumbleLinkName();
    args.append("-mumble");
    args.append(mumbleName);
    qInfo() << "Using custom mumble link:" << mumbleName;
  }

  QString argsString = args.join(" ");
  qInfo() << "Launching GW2:" << m_gw2Executable << argsString;

#ifdef Q_OS_WIN
  // Use CreateProcess - more reliable for getting process handle
  // Child process inherits admin privileges from launcher
  STARTUPINFOW si = {0};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {0};

  // Build command line: "executable" args
  QString cmdLine = QString("\"%1\" %2").arg(m_gw2Executable, argsString);
  std::wstring cmdLineW = cmdLine.toStdWString();
  std::wstring workDirW = m_gw2Path.toStdWString();

  BOOL success = CreateProcessW(
      nullptr,                              // lpApplicationName
      const_cast<LPWSTR>(cmdLineW.c_str()), // lpCommandLine (modifiable)
      nullptr,                              // lpProcessAttributes
      nullptr,                              // lpThreadAttributes
      FALSE,                                // bInheritHandles
      0,                                    // dwCreationFlags
      nullptr,                              // lpEnvironment
      workDirW.c_str(),                     // lpCurrentDirectory
      &si,                                  // lpStartupInfo
      &pi                                   // lpProcessInformation
  );

  if (success) {
    DWORD pid = pi.dwProcessId;

    // Apply process priority from profile
    if (profile.processPriority > 0) {
      DWORD priorityClass = NORMAL_PRIORITY_CLASS;
      const char *priorityName = "Normal";
      switch (profile.processPriority) {
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
      if (SetPriorityClass(pi.hProcess, priorityClass)) {
        qInfo() << "Set process priority to" << priorityName
                << "for PID:" << pid;
      } else {
        qWarning() << "Failed to set process priority for PID:" << pid
                   << "Error:" << GetLastError();
      }
    }

    // Create a QProcess wrapper to track the process
    auto *process = new QProcess(this);
    m_runningProcesses.append(process);
    m_instanceCounter++;

    // Store the PID for later reference
    m_processPids[process] = static_cast<qint64>(pid);

    // Store process handle for monitoring (will be closed by
    // startProcessMonitor)
    m_processHandles[static_cast<qint64>(pid)] = pi.hProcess;

    // Only close thread handle - process handle kept for monitoring
    CloseHandle(pi.hThread);

    emit gw2Launched(static_cast<qint64>(pid));
    qInfo() << "GW2 launched successfully! PID:" << pid;
    return process;
  } else {
    DWORD error = GetLastError();
    QString errorMsg = QString("Failed to launch GW2 (error %1)").arg(error);
    qWarning() << errorMsg;
    emit launchError(errorMsg);
    return nullptr;
  }
#else
  // Fallback for non-Windows
  auto *process = new QProcess(this);
  process->setWorkingDirectory(m_gw2Path);
  process->start(m_gw2Executable, args);
  if (process->waitForStarted(5000)) {
    m_runningProcesses.append(process);
    emit gw2Launched(process->processId());
    return process;
  }
  delete process;
  return nullptr;
#endif
}

QProcess *LaunchManager::launchWithProfile(AccountProfile &profile) {
  // NOTE: lastLoginTime is now set on character select (profileCharacterSelectReached)
  // rather than on launch. This ensures credential freshness is only confirmed
  // when GW2 actually reaches the character select screen.

  // === Pre-launch: Apply custom GFX settings (ALL profile types) ===
  // GFX patches GFXSettings.Gw2-64.exe.xml on disk before GW2 reads it.
  // Must happen BEFORE the Steam/Epic/Standalone branching.
  if (profile.useCustomGfx && !profile.gfxSettingsPath.isEmpty()) {
    qInfo() << "Applying GFX settings:" << profile.gfxSettingsPath;
    GFXManager gfxManager;
    if (gfxManager.applyGfxSettings(profile.gfxSettingsPath)) {
      qInfo() << "GFX settings applied successfully";
    } else {
      qWarning() << "Failed to apply GFX settings";
    }
  }

  // === Activate profile folder (junction) for Standalone only ===
  // Standalone profiles need the junction for credential isolation via
  // Local.dat. Steam/Epic profiles must NOT use the junction because:
  //   - Their auth is platform-handled (Finding #9: Local.dat irrelevant for
  //   auth)
  //   - The profile folder contains a stale Local.dat
  //   - Combined with -shareArchive (set in Steam/Epic Launch Options for
  //   multibox),
  //     stale Local.dat + read-only archive = "Download failed (5)" (Finding
  //     #14)
  //   - CredentialRefreshManager skips Steam/Epic profiles (only refreshes
  //   Standalone)
  if (m_localDatManager) {
    if (profile.accountProvider == AccountProvider::Standalone) {
      qInfo() << "Activating profile folder (Standalone):" << profile.id;
      if (m_localDatManager->activateProfile(profile.id)) {
        qInfo() << "Profile folder activated via junction";
      } else {
        qWarning() << "Failed to activate profile folder";
      }
    } else {
      qInfo() << "Skipping junction for"
              << (profile.accountProvider == AccountProvider::Steam ? "Steam"
                                                                    : "Epic")
              << "profile:" << profile.nickname
              << "(platform handles auth, junction not needed)";
    }
  }

  // === Steam/Epic: Launch via platform URL protocol ===
  if (profile.accountProvider == AccountProvider::Steam) {
    qInfo() << "Launching GW2 via Steam";

    // Kill mutex if GW2 is already running (required for multi-boxing)
    if (isGW2Running()) {
      qInfo() << "GW2 already running - killing mutex for Steam multi-box";
      closeMutexForMultiBox();
      QThread::msleep(500); // Brief delay after mutex kill
    }

    // If Steam NOT running, show parentless dialog
    if (!isSteamRunning()) {
      qInfo() << "Steam not running - showing platform dialog";

      // Parentless dialog (nullptr parent) - centered on screen, always on top
      PlatformLaunchDialog dialog(PlatformLaunchDialog::Platform::Steam,
                                  nullptr);

      if (dialog.exec() != QDialog::Accepted) {
        qInfo() << "User cancelled Steam launch";
        return nullptr;
      }

      qInfo() << "User confirmed Steam is ready - waiting 2s before launch";
      QThread::msleep(2000); // 2 second delay after Continue
    } else {
      qInfo() << "Steam already running - launching silently";
    }

    // Record existing PIDs BEFORE launching so we can find the new one
    QList<qint64> existingPids = getRunningGW2Pids();
    qInfo() << "Existing GW2 PIDs before Steam launch:" << existingPids;

    bool launched =
        QDesktopServices::openUrl(QUrl("steam://rungameid/1284210"));

    if (launched) {
      qInfo() << "Steam launch URL opened - using event-based GW2 detection";

      // Convert existing PIDs to QSet for the watcher
      QSet<qint64> existingPidSet;
      for (qint64 pid : existingPids) {
        existingPidSet.insert(pid);
      }

      // Start watching for GW2 window (event-based, SetWinEventHook)
      GW2WindowWatcher::instance()->watchForGW2(profile, existingPidSet);
    }
    // Phase 7: store persistent mumble name for overlay lookup
    if (!profile.mumbleLinkName.isEmpty()) {
      m_profileMumbleNames[profile.id] = profile.mumbleLinkName;
      qInfo() << "[DEV] LaunchManager: stored mumbleLinkName for Steam overlay:"
              << profile.mumbleLinkName;
    }
    return nullptr;
  }

  if (profile.accountProvider == AccountProvider::Epic) {
    qInfo() << "Launching GW2 via Epic Games";

    // Kill mutex if GW2 is already running (required for multi-boxing)
    if (isGW2Running()) {
      qInfo() << "GW2 already running - killing mutex for Epic multi-box";
      closeMutexForMultiBox();
      QThread::msleep(500); // Brief delay after mutex kill
    }

    // If Epic NOT running, show parentless dialog
    if (!isEpicRunning()) {
      qInfo() << "Epic Games not running - showing platform dialog";

      // Parentless dialog (nullptr parent) - centered on screen, always on top
      PlatformLaunchDialog dialog(PlatformLaunchDialog::Platform::Epic,
                                  nullptr);

      if (dialog.exec() != QDialog::Accepted) {
        qInfo() << "User cancelled Epic launch";
        return nullptr;
      }

      qInfo() << "User confirmed Epic is ready - waiting 2s before launch";
      QThread::msleep(2000); // 2 second delay after Continue
    } else {
      qInfo() << "Epic Games already running - launching silently";
    }

    // Epic URL protocol launch
    QString epicUrl =
        "com.epicgames.launcher://apps/"
        "10cab3b738244873bacb8ec7cef8128c%3Aada1dbe6d6d64aebb788713ec8d709c0%"
        "3A8d87562b481d44dd938c6a34a87d7355?action=launch&silent=true";

    // Record existing PIDs BEFORE launching so we can find the new one
    QList<qint64> existingPids = getRunningGW2Pids();
    qInfo() << "Existing GW2 PIDs before Epic launch:" << existingPids;

    bool launched = QDesktopServices::openUrl(QUrl(epicUrl));

    if (launched) {
      qInfo() << "Epic launch URL opened - using event-based GW2 detection";

      // Convert existing PIDs to QSet for the watcher
      QSet<qint64> existingPidSet;
      for (qint64 pid : existingPids) {
        existingPidSet.insert(pid);
      }

      // Start watching for GW2 window
      GW2WindowWatcher::instance()->watchForGW2(profile, existingPidSet);
    } else {
      qWarning() << "Failed to open Epic URL";
      emit launchError(
          "Failed to launch Epic Games. Is Epic Launcher installed?");
    }
    // Phase 7: store persistent mumble name for overlay lookup
    if (!profile.mumbleLinkName.isEmpty()) {
      m_profileMumbleNames[profile.id] = profile.mumbleLinkName;
      qInfo() << "[DEV] LaunchManager: stored mumbleLinkName for Epic overlay:"
              << profile.mumbleLinkName;
    }
    return nullptr;
  }

  // === Standalone: Direct launch (original behavior) ===
  // Junction already activated above (before Steam/Epic branching)

  // Convert AccountProfile to LaunchProfile
  LaunchProfile lp = profile.toLaunchProfile();

  // Phase 7: profile.toLaunchProfile() already adds -mumble from
  // profile.mumbleLinkName (set at profile creation, persistent).
  // Just store it for overlay lookup.
  if (!profile.mumbleLinkName.isEmpty()) {
    m_profileMumbleNames[profile.id] = profile.mumbleLinkName;
    qInfo() << "[DEV] LaunchManager: stored mumbleLinkName for overlay:"
            << profile.mumbleLinkName << "profile:" << profile.id;
  }

  // Note: Steam/Epic accounts are handled via URL protocol above
  // This code path only runs for Standalone accounts

  // Start window watcher for standalone too — so onGW2WindowDetected fires
  // and profileWindowConfirmed signal is emitted (needed for junction timing)
  {
    QList<qint64> existingPids = getRunningGW2Pids();
    QSet<qint64> existingPidSet;
    for (qint64 pid : existingPids) {
      existingPidSet.insert(pid);
    }
    GW2WindowWatcher::instance()->watchForGW2(profile, existingPidSet);
  }

  // === 2. Launch the game ===
  QProcess *proc = launchGW2(lp);

  if (proc) {
    // Get actual PID from our map (proc->processId() returns 0 for
    // CreateProcess)
    qint64 pid = m_processPids.value(proc, 0);
    qInfo() << "launchWithProfile: Using PID from map:" << pid;

    // Track process → profileId for cleanup in onProcessFinished
    m_processToProfileId[proc] = profile.id;

    // Emit profileLaunched for running state tracking (direct launches)
    emit profileLaunched(profile.id, pid);

    // Track PID → Path for crash detection
    QString gw2Path =
        profile.customGw2Path.isEmpty() ? m_gw2Path : profile.customGw2Path;
    m_pidPaths[pid] = gw2Path;

    // Start process monitor for pre-injection crash detection (event-based, no
    // timers)
    HANDLE hProcess = m_processHandles.take(pid); // Take ownership
    if (hProcess) {
      startProcessMonitor(hProcess, pid, profile, gw2Path);
    }

    // === 3. Inject DLLs after a delay ===
    if (!profile.injectedDlls.isEmpty()) {
      // Wait for game to initialize before injecting
      QTimer::singleShot(3000, this, [this, pid, profile]() {
        injectDlls(pid, profile.injectedDlls);
      });
    }

    // === 4. Session tracking and window positioning via helper DLL ===
    // Helper DLL is injected into GW2, monitors when game is loaded,
    // and signals via Named Pipe. Also signals when game exits for playtime
    // tracking.
    qInfo() << "Window settings - useCustomWindow:" << profile.useCustomWindow
            << "size:" << profile.windowWidth << "x" << profile.windowHeight
            << "pos:" << profile.windowX << "," << profile.windowY;

    // Always create pipe server for playtime tracking (and window positioning
    // if enabled)
    QString pipeName = QString("\\\\.\\pipe\\GW2AIO_%1").arg(pid);
    qInfo() << "Creating Named Pipe for session tracking:" << pipeName;

    // Store whether to position window (capture before thread starts)
    bool shouldPositionWindow = profile.useCustomWindow;

    // Note: gw2Path and m_pidPaths[pid] already set above when starting process
    // monitor

    // REVIEW BEFORE BETA: Audit finding #2 — this pipe server is near-identical
    // to the Steam/Epic one at ~L1134. Deferred dedup due to different security
    // models (default DACL here vs NULL DACL for Steam/Epic). See
    // docs/audit-report-phase1.md for details.
    // Start pipe server in background thread - stays alive for entire game
    // session
    std::thread([this, pid, profile, pipeName, shouldPositionWindow,
                 gw2Path]() {
#ifdef Q_OS_WIN
      bool shouldContinue = true;

      while (shouldContinue) {
        // Create named pipe
        HANDLE hPipe = CreateNamedPipeW(
            pipeName.toStdWString().c_str(), PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 256, 256,
            INFINITE, // No timeout - wait as long as needed
            nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
          qWarning() << "Failed to create pipe:" << GetLastError();
          return;
        }

        qInfo() << "Waiting for helper DLL connection (PID:" << pid << ")...";

        // Wait for connection from helper DLL
        if (ConnectNamedPipe(hPipe, nullptr) ||
            GetLastError() == ERROR_PIPE_CONNECTED) {
          char buffer[256];
          DWORD bytesRead;

          if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead,
                       nullptr) &&
              bytesRead > 0) {
            buffer[bytesRead] = '\0';
            qInfo() << "Received signal from helper DLL (PID:" << pid
                    << "):" << buffer;

            if (strcmp(buffer, "LOADED") == 0) {
              // Mark as successfully loaded (GPU active) and sync build ID
              QMetaObject::invokeMethod(
                  this,
                  [this, pid, gw2Path]() {
                    m_loadedPids.insert(pid);
                    emit profileLoaded(gw2Path); // Triggers build ID sync
                  },
                  Qt::QueuedConnection);

              // Signal received - position window ONLY if enabled
              if (shouldPositionWindow) {
                QMetaObject::invokeMethod(
                    this,
                    [this, pid, profile]() {
                      qInfo()
                          << "Game loaded! Positioning window via DLL signal";
                      setWindowPosition(pid, profile.windowX, profile.windowY,
                                        profile.windowWidth,
                                        profile.windowHeight);
                    },
                    Qt::QueuedConnection);
              } else {
                qInfo()
                    << "Game loaded (no window positioning for this profile)";
              }

              // Rename window title to include profile name
              QMetaObject::invokeMethod(
                  this,
                  [this, pid, profile]() {
                    setWindowTitle(pid, profile.nickname);
                  },
                  Qt::QueuedConnection);

              // Notify after window is positioned + renamed — children
              // depend on final window geometry.
              QMetaObject::invokeMethod(
                  this,
                  [this, profileId = profile.id]() {
                    emit profileCharacterSelectReached(profileId);
                  },
                  Qt::QueuedConnection);

            } else if (strcmp(buffer, "EXITING") == 0) {
              // Playtime tracking cancelled - just log and exit
              qInfo() << "Game exiting, profile:" << profile.nickname;

              // Trigger CEF orphan detection (NamedPipe trigger)
              CefManager::instance().registerExitSignal(
                  pid, CefTriggerSource::NamedPipe);

              shouldContinue = false;
            }
          }
        } else {
          // Connection failed - check if process still exists
          HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE,
                                        static_cast<DWORD>(pid));
          if (hProcess == nullptr) {
            qInfo() << "Process" << pid
                    << "no longer exists, ending pipe server";
            shouldContinue = false;
          } else {
            CloseHandle(hProcess);
          }
        }

        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
      }

      // Note: Crash detection and cleanup handled by startProcessMonitor

      qInfo() << "Pipe server ended for PID:" << pid;
#endif
    }).detach();

    // Inject helper DLL (after a short delay for pipe to be ready)
    QTimer::singleShot(
        1000, this, [this, pid, profile, shouldPositionWindow]() {
          QString helperDllPath =
              QCoreApplication::applicationDirPath() + "/GW2AIOHelper.dll";
          if (QFile::exists(helperDllPath)) {
            qInfo() << "Injecting helper DLL:" << helperDllPath;
            m_dllInjector->inject(static_cast<DWORD>(pid), helperDllPath);
          } else {
            qWarning() << "Helper DLL not found:" << helperDllPath;
            // DOCUMENTED EXCEPTION to "NO TIMERS for window positioning" rule:
            // This 45s fallback only fires when the helper DLL is missing
            // (build misconfiguration or manual deletion). Under normal
            // operation, the DLL signals via named pipe which is event-based.
            // Without the DLL there is no alternative signal source.
            // Fallback to 45 second timer for window positioning
            if (shouldPositionWindow) {
              QTimer::singleShot(45000, this, [this, pid, profile]() {
                qInfo() << "Fallback: Positioning window after 45s timeout";
                setWindowPosition(pid, profile.windowX, profile.windowY,
                                  profile.windowWidth, profile.windowHeight);
              });
            }
          }
        });
  }

  return proc;
}

void LaunchManager::injectDlls(qint64 pid, const QStringList &dlls) {
  qInfo() << "Injecting" << dlls.size() << "DLLs into PID" << pid;

  for (const QString &dllPath : dlls) {
    QFileInfo info(dllPath);
    if (!info.exists()) {
      qWarning() << "DLL not found:" << dllPath;
      emit dllInjected(dllPath, false);
      continue;
    }

    DllInjectionResult result =
        m_dllInjector->inject(static_cast<DWORD>(pid), dllPath);

    emit dllInjected(dllPath, result == DllInjectionResult::Success);
  }
}

void LaunchManager::setWindowPosition(qint64 pid, int x, int y, int width,
                                      int height) {
#ifdef Q_OS_WIN
  qInfo() << "Setting window position for PID" << pid << "to" << x << "," << y
          << "size" << width << "x" << height;

  // Find the main game window for this process
  // Prefer "ArenaNet" class windows (game) over splash/launcher
  struct EnumData {
    DWORD pid;
    HWND hwnd;
    RECT bestRect;
    int bestArea;
    bool bestIsArenaNet; // Track if current best has ArenaNet class
  } data = {static_cast<DWORD>(pid), nullptr, {0, 0, 0, 0}, 0, false};

  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *data = reinterpret_cast<EnumData *>(lParam);
        DWORD windowPid;
        GetWindowThreadProcessId(hwnd, &windowPid);

        if (windowPid == data->pid) {
          // Check if it's a main window (visible, has title)
          if (IsWindowVisible(hwnd)) {
            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);
            QString windowTitle = QString::fromWCharArray(title);

            // Get window class name to distinguish game from splash
            wchar_t className[256];
            GetClassNameW(hwnd, className, 256);
            QString windowClass = QString::fromWCharArray(className);

            // GW2 game window uses ArenaNet_Dx_Window_Class
            // Splash/launcher uses different class
            bool isArenaNet =
                windowClass.contains("ArenaNet", Qt::CaseInsensitive);

            // GW2 window has "Guild Wars 2" in the title
            if (windowTitle.contains("Guild Wars 2", Qt::CaseInsensitive)) {
              RECT rect;
              GetWindowRect(hwnd, &rect);
              int windowWidth = rect.right - rect.left;
              int windowHeight = rect.bottom - rect.top;
              int area = windowWidth * windowHeight;

              qInfo() << "Found GW2 window:" << windowTitle
                      << "Class:" << windowClass << "Size:" << windowWidth
                      << "x" << windowHeight << "IsArenaNet:" << isArenaNet;

              // Minimum size check (not a tiny popup)
              if (windowWidth >= 640 && windowHeight >= 480) {
                // Priority: ArenaNet class > larger window
                // 1. If this is ArenaNet and current best is not -> always take
                // it
                // 2. If both are ArenaNet (or both not) -> take larger
                // 3. If current best is ArenaNet and this is not -> keep
                // current

                bool shouldReplace = false;

                if (isArenaNet && !data->bestIsArenaNet) {
                  // This is game window, current is splash -> always replace
                  shouldReplace = true;
                  qInfo() << "  -> Preferring ArenaNet class over current best";
                } else if (!isArenaNet && data->bestIsArenaNet) {
                  // This is splash, current is game -> never replace
                  shouldReplace = false;
                } else {
                  // Same category -> prefer larger
                  shouldReplace = (area > data->bestArea);
                }

                if (shouldReplace || data->hwnd == nullptr) {
                  data->hwnd = hwnd;
                  data->bestRect = rect;
                  data->bestArea = area;
                  data->bestIsArenaNet = isArenaNet;
                }
              }
            }
          }
        }
        return TRUE; // Continue enumeration to find all windows
      },
      reinterpret_cast<LPARAM>(&data));

  if (data.hwnd) {
    qInfo() << "Targeting GW2 window - Area:" << data.bestArea
            << "IsArenaNet:" << data.bestIsArenaNet;

    // First, make sure window is not maximized
    ShowWindow(data.hwnd, SW_RESTORE);

    // Set window style to allow resize (remove fixed style if any)
    LONG style = GetWindowLong(data.hwnd, GWL_STYLE);
    style |= WS_SIZEBOX | WS_CAPTION;
    SetWindowLong(data.hwnd, GWL_STYLE, style);

    // Move and resize the window
    // Use MoveWindow for more reliable resize
    BOOL result = MoveWindow(data.hwnd, x, y, width, height, TRUE);

    if (result) {
      qInfo() << "Window positioned successfully using MoveWindow";
      emit windowPositioned(pid, x, y);
    } else {
      // Fallback to SetWindowPos
      SetWindowPos(data.hwnd, nullptr, x, y, width, height,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      qInfo() << "Window positioned using SetWindowPos fallback";
      emit windowPositioned(pid, x, y);
    }
  } else {
    qWarning() << "Could not find GW2 game window for PID" << pid
               << "(may still be loading)";
  }
#else
  Q_UNUSED(pid);
  Q_UNUSED(x);
  Q_UNUSED(y);
  Q_UNUSED(width);
  Q_UNUSED(height);
#endif
}

QList<QProcess *>
LaunchManager::launchMultiple(const QList<LaunchProfile> &profiles) {
  QList<QProcess *> processes;

  for (const LaunchProfile &profile : profiles) {
    // Force -shareArchive for multi-boxing
    LaunchProfile modifiedProfile = profile;
    if (!modifiedProfile.arguments.contains(LaunchArguments::SHARE_ARCHIVE)) {
      modifiedProfile.arguments.append(LaunchArguments::SHARE_ARCHIVE);
    }

    QProcess *proc = launchGW2(modifiedProfile);
    if (proc) {
      processes.append(proc);

      // Wait a bit between launches
      QThread::msleep(2000);
    }
  }

  return processes;
}

bool LaunchManager::closeMutexForMultiBox() {
  if (!m_mutexManager) {
    return false;
  }

  QList<qint64> pids = getRunningGW2Pids();
  bool success = false;

  for (qint64 pid : pids) {
    if (m_mutexManager->closeMutex(static_cast<DWORD>(pid))) {
      success = true;
    }
  }

  return success;
}

// backupLocalDat() removed — junction approach does not need pre-backup.
// Each profile has its own folder; no shared file to backup.

bool LaunchManager::isGW2Running() const {
  return !getRunningGW2Pids().isEmpty();
}

QList<qint64> LaunchManager::getRunningGW2Pids() const {
  QList<qint64> pids;

#ifdef Q_OS_WIN
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE) {
    return pids;
  }

  PROCESSENTRY32W pe32;
  pe32.dwSize = sizeof(pe32);

  if (Process32FirstW(hSnapshot, &pe32)) {
    do {
      QString processName = QString::fromWCharArray(pe32.szExeFile);
      if (processName.compare("Gw2-64.exe", Qt::CaseInsensitive) == 0 ||
          processName.compare("Gw2.exe", Qt::CaseInsensitive) == 0) {
        pids.append(pe32.th32ProcessID);
      }
    } while (Process32NextW(hSnapshot, &pe32));
  }

  CloseHandle(hSnapshot);
#endif

  return pids;
}

QStringList LaunchManager::buildArguments(const LaunchProfile &profile) const {
  QStringList args = profile.arguments;
  // Note: -autologin is added by ProfileManager::toLaunchProfile()
  // when autoLogin + Local.dat is active. Keyboard-based autologin
  // was archived — see docs/archived-features/keyboard-autologin.md
  return args;
}

QString LaunchManager::generateMumbleLinkName() {
  ++m_instanceCounter;
  return QString("GW2MumbleLink%1").arg(m_instanceCounter);
}

void LaunchManager::onProcessFinished(int exitCode,
                                      QProcess::ExitStatus status) {
  Q_UNUSED(status);
  auto *process = qobject_cast<QProcess *>(sender());
  if (process) {
    qint64 pid = process->processId();
    m_runningProcesses.removeOne(process);
    emit gw2Exited(pid, exitCode);
    process->deleteLater();

    qInfo() << "GW2 process exited. PID:" << pid << "Exit code:" << exitCode;

    // === Notify overlay manager that profile's GW2 process exited ===
    QString profileId = m_processToProfileId.take(process);
    if (!profileId.isEmpty()) {
      emit profileExited(profileId);
    }

    // === Clean up stale mumble link name ===
    if (!profileId.isEmpty() && m_profileMumbleNames.contains(profileId)) {
      qInfo() << "Cleaned up mumble link name for profile:" << profileId
              << "name:" << m_profileMumbleNames.value(profileId);
      m_profileMumbleNames.remove(profileId);
    }

    // === Deactivate junction when all instances close ===
    if (m_runningProcesses.isEmpty() && m_localDatManager &&
        m_localDatManager->isJunctionActive()) {
      qInfo() << "All GW2 instances closed — deactivating junction";
      m_localDatManager->deactivateProfile();
    }
  }
}

void LaunchManager::deactivateJunction() {
  if (m_localDatManager && m_localDatManager->isJunctionActive()) {
    qInfo() << "Post-launch: deactivating junction, restoring original AppData";
    m_localDatManager->deactivateProfile();
  }
}

// === Steam Detection ===

bool LaunchManager::isSteamInstall(const QString &gw2Path) {
  // Check for steam_api64.dll or steam_appid.txt in the GW2 folder
  QDir dir(gw2Path);
  return dir.exists("steam_api64.dll") || dir.exists("steam_appid.txt");
}

QString LaunchManager::detectSteamGW2Path() {
  QString steamPath;

#ifdef Q_OS_WIN
  // Read Steam install path from registry
  QSettings steamReg("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
                     QSettings::NativeFormat);
  steamPath = steamReg.value("InstallPath").toString();

  if (steamPath.isEmpty()) {
    // Try alternative location
    QSettings steamReg2("HKEY_CURRENT_USER\\SOFTWARE\\Valve\\Steam",
                        QSettings::NativeFormat);
    steamPath = steamReg2.value("SteamPath").toString();
  }

  if (!steamPath.isEmpty()) {
    // Check default library folder
    QString gw2Path = steamPath + "/steamapps/common/Guild Wars 2";
    if (QDir(gw2Path).exists("Gw2-64.exe")) {
      return gw2Path;
    }

    // Check library folders file for other install locations
    QString libraryFile = steamPath + "/steamapps/libraryfolders.vdf";
    QFile file(libraryFile);
    if (file.open(QIODevice::ReadOnly)) {
      QString content = file.readAll();
      file.close();

      // Simple parsing - look for "path" entries
      QRegularExpression regex("\"path\"\\s*\"([^\"]+)\"");
      auto matches = regex.globalMatch(content);
      while (matches.hasNext()) {
        auto match = matches.next();
        QString libPath = match.captured(1).replace("\\\\", "/");
        QString checkPath = libPath + "/steamapps/common/Guild Wars 2";
        if (QDir(checkPath).exists("Gw2-64.exe")) {
          return checkPath;
        }
      }
    }
  }
#endif

  return QString();
}

QString LaunchManager::detectEpicGW2Path() {
#ifdef Q_OS_WIN
  // Epic stores game manifests as .item JSON files in ProgramData
  QString manifestDir = "C:/ProgramData/Epic/EpicGamesLauncher/Data/Manifests";
  QDir dir(manifestDir);
  if (!dir.exists()) {
    qInfo() << "Epic manifests directory not found";
    return QString();
  }

  // GW2's Epic catalog item ID
  const QString gw2AppName = "8d87562b481d44dd938c6a34a87d7355";

  QStringList filters;
  filters << "*.item";
  QFileInfoList manifests = dir.entryInfoList(filters, QDir::Files);

  for (const QFileInfo &fi : manifests) {
    QFile file(fi.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
      continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull())
      continue;

    QJsonObject obj = doc.object();
    if (obj["AppName"].toString() == gw2AppName) {
      QString installLocation = obj["InstallLocation"].toString();
      if (!installLocation.isEmpty()) {
        // Check if Gw2-64.exe exists at this location
        QDir installDir(installLocation);
        if (installDir.exists("Gw2-64.exe")) {
          qInfo() << "Found Epic GW2 installation:" << installLocation;
          return installLocation;
        }
      }
    }
  }
#endif
  return QString();
}

QString LaunchManager::getEffectiveGw2Path(const AccountProfile &profile,
                                           const QString &globalPath) {
  // 1. If profile has a custom path set, use it
  if (!profile.customGw2Path.isEmpty()) {
    QString path = profile.customGw2Path;
    // Normalize: if path ends with .exe, extract parent directory
    if (path.endsWith(".exe", Qt::CaseInsensitive)) {
      QFileInfo fi(path);
      path = fi.absolutePath();
    }
    return path;
  }

  // 2. Try platform-specific detection based on profile provider
  switch (profile.accountProvider) {
  case AccountProvider::Steam: {
    QString steamPath = detectSteamGW2Path();
    if (!steamPath.isEmpty()) {
      qInfo() << "Using detected Steam installation:" << steamPath;
      return steamPath;
    }
    break;
  }
  case AccountProvider::Epic: {
    QString epicPath = detectEpicGW2Path();
    if (!epicPath.isEmpty()) {
      qInfo() << "Using detected Epic installation:" << epicPath;
      return epicPath;
    }
    break;
  }
  case AccountProvider::Standalone:
  default:
    break;
  }

  // 3. Fall back to global path
  return globalPath;
}

// === Platform Running Detection ===

bool LaunchManager::isSteamRunning() {
#ifdef Q_OS_WIN
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE)
    return false;

  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  bool found = false;

  if (Process32FirstW(hSnapshot, &pe)) {
    do {
      if (QString::fromWCharArray(pe.szExeFile)
              .compare("Steam.exe", Qt::CaseInsensitive) == 0) {
        found = true;
        break;
      }
    } while (Process32NextW(hSnapshot, &pe));
  }

  CloseHandle(hSnapshot);
  return found;
#else
  return false;
#endif
}

bool LaunchManager::isEpicRunning() {
#ifdef Q_OS_WIN
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE)
    return false;

  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  bool found = false;

  if (Process32FirstW(hSnapshot, &pe)) {
    do {
      if (QString::fromWCharArray(pe.szExeFile)
              .compare("EpicGamesLauncher.exe", Qt::CaseInsensitive) == 0) {
        found = true;
        break;
      }
    } while (Process32NextW(hSnapshot, &pe));
  }

  CloseHandle(hSnapshot);
  return found;
#else
  return false;
#endif
}

QString LaunchManager::getProcessPath(qint64 pid) {
#ifdef Q_OS_WIN
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                static_cast<DWORD>(pid));
  if (hProcess == nullptr) {
    return QString();
  }

  wchar_t path[MAX_PATH];
  DWORD pathLen = MAX_PATH;

  if (QueryFullProcessImageNameW(hProcess, 0, path, &pathLen)) {
    CloseHandle(hProcess);
    return QString::fromWCharArray(path);
  }

  CloseHandle(hProcess);
  return QString();
#else
  Q_UNUSED(pid);
  return QString();
#endif
}

void LaunchManager::onGW2WindowDetected(qint64 pid,
                                        const AccountProfile &profile) {
  qInfo() << "EVENT-BASED: GW2 window detected, PID:" << pid
          << "Profile:" << profile.nickname;

  // Always emit the window-confirmed trigger (needed for junction timing)
  emit profileWindowConfirmed(profile.id);

  // For standalone profiles, launchWithProfile() already set up:
  // - profileLaunched signal, gw2Launched signal
  // - startProcessMonitor, pipe server, path tracking
  // Only emit remaining signals and skip setup.
  bool alreadySetUp = m_monitoredPids.contains(pid);

  if (!alreadySetUp) {
    // Steam/Epic: first time seeing this PID — emit launch signals
    emit gw2Launched(pid);
    emit profileLaunched(profile.id, pid); // For running state tracking
  }

  // === Setup section — skip for standalone (already done) ===
  if (!alreadySetUp) {
    // Discover ACTUAL exe path from the running process.
    // This is critical for Steam/Epic which may launch from a different
    // installation than the global m_gw2Path (e.g., Epic installs inside
    // Steam's folder as a subfolder).
    QString gw2Path;
    QString processExePath = getProcessPath(pid);
    if (!processExePath.isEmpty()) {
      QFileInfo fi(processExePath);
      gw2Path = fi.absolutePath();
      qInfo() << "Found installation location:" << gw2Path;
    } else {
      // Fallback to profile path or global
      gw2Path =
          profile.customGw2Path.isEmpty() ? m_gw2Path : profile.customGw2Path;
      qWarning() << "Could not detect installation location, using saved path:"
                 << gw2Path;
    }
    m_pidPaths[pid] = gw2Path;

    // Start process monitor for pre-injection crash detection
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (hProcess) {
      startProcessMonitor(hProcess, pid, profile, gw2Path);
    }

    // REVIEW BEFORE BETA: Audit finding #2 — this pipe server is near-identical
    // to the standalone one at ~L462. Deferred dedup due to different security
    // models (NULL DACL here vs default DACL for standalone). See
    // docs/audit-report-phase1.md for details.
    // Create pipe server for session tracking (event-based communication with
    // helper DLL)
    QString pipeName = QString("\\\\.\\pipe\\GW2AIO_%1").arg(pid);
    bool shouldPositionWindow = profile.useCustomWindow;

    std::thread([this, pid, profile, pipeName, shouldPositionWindow,
                 gw2Path]() {
#ifdef Q_OS_WIN
      // Create security attributes with NULL DACL to allow non-admin access
      // This is needed because Steam/Epic may launch GW2 without elevation
      SECURITY_DESCRIPTOR sd;
      InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
      SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);

      SECURITY_ATTRIBUTES sa;
      sa.nLength = sizeof(SECURITY_ATTRIBUTES);
      sa.lpSecurityDescriptor = &sd;
      sa.bInheritHandle = FALSE;

      // Create the first pipe immediately so it's ready for DLL
      HANDLE hPipe =
          CreateNamedPipeW(pipeName.toStdWString().c_str(), PIPE_ACCESS_INBOUND,
                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                           256, 256, INFINITE, &sa);

      if (hPipe == INVALID_HANDLE_VALUE) {
        qWarning() << "Failed to create pipe:" << GetLastError();
        return;
      }

      qInfo() << "Pipe server ready (event-based, open security):" << pipeName;

      bool shouldContinue = true;

      while (shouldContinue) {
        if (ConnectNamedPipe(hPipe, nullptr) ||
            GetLastError() == ERROR_PIPE_CONNECTED) {
          char buffer[256];
          DWORD bytesRead;
          if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead,
                       nullptr) &&
              bytesRead > 0) {
            buffer[bytesRead] = '\0';
            if (strcmp(buffer, "LOADED") == 0) {
              qInfo() << "EVENT: Got LOADED from PID:" << pid;

              // Mark as successfully loaded (GPU active) and sync build ID
              QMetaObject::invokeMethod(
                  this,
                  [this, pid, gw2Path]() {
                    m_loadedPids.insert(pid);
                    emit profileLoaded(gw2Path); // Triggers build ID sync
                  },
                  Qt::QueuedConnection);

              if (shouldPositionWindow) {
                QMetaObject::invokeMethod(
                    this,
                    [this, pid, profile]() {
                      setWindowPosition(pid, profile.windowX, profile.windowY,
                                        profile.windowWidth,
                                        profile.windowHeight);
                    },
                    Qt::QueuedConnection);
              }

              // Rename window title to include profile name
              QMetaObject::invokeMethod(
                  this,
                  [this, pid, profile]() {
                    setWindowTitle(pid, profile.nickname);
                  },
                  Qt::QueuedConnection);

              // Notify after window is positioned + renamed — children
              // depend on final window geometry.
              QMetaObject::invokeMethod(
                  this,
                  [this, profileId = profile.id]() {
                    emit profileCharacterSelectReached(profileId);
                  },
                  Qt::QueuedConnection);
            } else if (strcmp(buffer, "EXITING") == 0) {
              qInfo() << "EVENT: Got EXITING from PID:" << pid;

              // Trigger CEF orphan detection (NamedPipe trigger)
              CefManager::instance().registerExitSignal(
                  pid, CefTriggerSource::NamedPipe);

              shouldContinue = false;
            }
          }
        }
        DisconnectNamedPipe(hPipe);

        if (shouldContinue) {
          // Recreate pipe for next message (with same security)
          CloseHandle(hPipe);
          hPipe = CreateNamedPipeW(
              pipeName.toStdWString().c_str(), PIPE_ACCESS_INBOUND,
              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 256, 256,
              INFINITE, &sa);
          if (hPipe == INVALID_HANDLE_VALUE)
            return;
        }
      }
      CloseHandle(hPipe);

// Note: Crash detection and cleanup handled by startProcessMonitor
#endif
    }).detach();

    // Inject helper DLL immediately after pipe is created
    // The pipe server is already listening, so DLL can connect when ready
    QString helperDllPath =
        QCoreApplication::applicationDirPath() + "/GW2AIOHelper.dll";
    if (QFile::exists(helperDllPath)) {
      qInfo() << "Injecting helper DLL for PID:" << pid;
      m_dllInjector->inject(static_cast<DWORD>(pid), helperDllPath);
    }

    // Inject any profile-specific DLLs
    if (!profile.injectedDlls.isEmpty()) {
      qInfo() << "Injecting profile DLLs for PID:" << pid;
      injectDlls(pid, profile.injectedDlls);
    }

    // Store profile for tracking
    m_pendingProfiles[pid] = profile;
  } // end if (!alreadySetUp)
}

bool LaunchManager::checkForPatchCrash() const {
  // Read ArenaNet.log from %APPDATA%\Guild Wars 2
  QString logPath =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      "/Guild Wars 2/Arenanet.log";

  QFile logFile(logPath);
  if (!logFile.exists()) {
    // Try alternative path
    logPath = QDir::homePath() + "/AppData/Roaming/Guild Wars 2/Arenanet.log";
    logFile.setFileName(logPath);
  }

  if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qDebug() << "Could not open ArenaNet.log at:" << logPath;
    return false;
  }

  // Read first 1KB to check for crash header
  QByteArray header = logFile.read(1024);
  logFile.close();

  QString content = QString::fromUtf8(header);

  // Check for "Client needs to be patched" assertion
  if (content.contains("Client needs to be patched", Qt::CaseInsensitive)) {
    qInfo() << "Detected 'Client needs to be patched' in ArenaNet.log";
    return true;
  }

  return false;
}

void LaunchManager::startProcessMonitor(HANDLE hProcess, qint64 pid,
                                        const AccountProfile &profile,
                                        const QString &gw2Path) {
#ifdef Q_OS_WIN
  // Spawn background thread that waits for process termination
  // This is event-based (WaitForSingleObject), NOT timer-based
  m_monitoredPids.insert(pid);
  std::thread([this, hProcess, pid, profile, gw2Path]() {
    qInfo() << "Process monitor started for PID:" << pid;

    // Wait indefinitely for process to terminate (event-based, zero CPU)
    WaitForSingleObject(hProcess, INFINITE);

    qInfo() << "Process monitor: PID" << pid << "terminated";

    // IMMEDIATELY trigger CEF cleanup (runs in this thread before UI)
    // This catches orphaned CefHost.exe before crash popup can block main
    // thread
    CefManager::instance().registerExitSignal(pid,
                                              CefTriggerSource::ProcessMonitor);

    // Process terminated - check if LOADED was ever received
    QMetaObject::invokeMethod(
        this,
        [this, pid, profile, gw2Path]() {
          bool wasLoaded = m_loadedPids.contains(pid);

          // Clean up tracking maps FIRST
          m_pidPaths.remove(pid);
          m_loadedPids.remove(pid);
          m_pendingProfiles.remove(pid);
          m_monitoredPids.remove(pid);

          // Clean up stale mumble link name
          if (m_profileMumbleNames.contains(profile.id)) {
            qInfo() << "Cleaned up mumble link name for profile:" << profile.id
                    << "name:" << m_profileMumbleNames.value(profile.id);
            m_profileMumbleNames.remove(profile.id);
          }

          // Signal that GW2 exited to clear running badge BEFORE showing
          // modal dialog
          emit gw2Exited(pid, wasLoaded ? 0 : -1);

          if (!wasLoaded) {
            // Process died before GPU signal - crash during launch
            qWarning() << "Pre-injection crash detected for PID:" << pid;

            // Safety net: deactivate junction if crash happened before LOADED
            if (m_localDatManager && m_localDatManager->isJunctionActive()) {
              qInfo() << "Crash before LOADED — deactivating junction";
              m_localDatManager->deactivateProfile();
            }

            QString reason = checkForPatchCrash() ? "Patch may be required"
                                                  : "Unknown error";
            emit profileCrashDuringLaunch(profile.id, profile.nickname, gw2Path,
                                          reason);
          } else {
            qInfo() << "Normal exit for PID:" << pid << "(LOADED was received)";
          }
        },
        Qt::QueuedConnection);

    // Close handle now that process is dead
    CloseHandle(hProcess);
  }).detach();
#else
  Q_UNUSED(hProcess);
  Q_UNUSED(pid);
  Q_UNUSED(profile);
  Q_UNUSED(gw2Path);
#endif
}

#ifdef Q_OS_WIN
// Helper struct for EnumWindows callback
struct WindowTitleData {
  DWORD targetPid;
  QString newTitle;
  bool found;
};

static BOOL CALLBACK EnumWindowsForTitleCallback(HWND hwnd, LPARAM lParam) {
  auto *data = reinterpret_cast<WindowTitleData *>(lParam);

  DWORD windowPid;
  GetWindowThreadProcessId(hwnd, &windowPid);

  if (windowPid == data->targetPid) {
    // Check if it's a main window (visible and has title)
    if (IsWindowVisible(hwnd) && GetWindowTextLengthW(hwnd) > 0) {
      // Get current title to verify it's GW2
      wchar_t title[256];
      GetWindowTextW(hwnd, title, 256);
      if (wcsstr(title, L"Guild Wars 2") != nullptr) {
        SetWindowTextW(hwnd, data->newTitle.toStdWString().c_str());
        data->found = true;
        qInfo() << "Set window title to:" << data->newTitle;
        return FALSE; // Stop enumeration
      }
    }
  }
  return TRUE; // Continue enumeration
}
#endif

void LaunchManager::setWindowTitle(qint64 pid, const QString &profileName) {
#ifdef Q_OS_WIN
  WindowTitleData data;
  data.targetPid = static_cast<DWORD>(pid);
  data.newTitle = QString("%1 - Guild Wars 2").arg(profileName);
  data.found = false;

  EnumWindows(EnumWindowsForTitleCallback, reinterpret_cast<LPARAM>(&data));

  if (!data.found) {
    qWarning() << "Could not find GW2 window for PID:" << pid;
  }
#else
  Q_UNUSED(pid);
  Q_UNUSED(profileName);
#endif
}
