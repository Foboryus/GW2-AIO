#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QSharedMemory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <timeapi.h>  // timeBeginPeriod / timeEndPeriod
#pragma comment(lib, "winmm.lib")
#endif

// Core
#include "core/AddonManager.h"
#include "core/AppConfig.h"
#include "core/CefManager.h"
#include "core/CommandLineParser.h"
#include "core/CrashHandler.h"
#include "core/DataService.h"
#include "core/GW2Detector.h"
#include "core/LaunchManager.h"
#include "core/Logger.h"
#include "core/MumbleLink.h"
#include "core/SettingsManager.h"
#include "core/ThemeManager.h"

// UI
#include "ui/MainWindow.h"
#include "ui/OverlayWindow.h"
#include "ui/SetupWizard.h"
#include "ui/SplashScreen.h"
#include "ui/StyledTooltip.h"
#include "ui/UIHelpers.h"

// Features
#include "features/dps/DPSController.h"
#include "features/markers/MarkerController.h"
#include "features/markers/MarkerSettingsManager.h"
#include "features/modules/ModuleController.h"
#include "features/radial/RadialController.h"

// D3D11 Overlay Rendering Engine
#include "rendering/D3D11OverlayWindow.h"

int main(int argc, char *argv[]) {
  // Enable high DPI scaling - must be before QApplication creation
  // This makes QScreen::geometry() report scaled resolution matching Windows
  // display settings
  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

  QApplication app(argc, argv);

  // Reduce Windows timer resolution from default 15.6ms to 1ms.
  // This dramatically improves QTimer accuracy for the overlay render loop.
  // Games (including GW2) commonly call this. Paired with timeEndPeriod on exit.
#ifdef Q_OS_WIN
  timeBeginPeriod(1);
#endif

  app.setApplicationName("GW2 AIO Manager");
  app.setApplicationVersion(APP_VERSION);
  app.setOrganizationName("GW2AIO");
  app.setWindowIcon(QIcon(":/icons/app-icon.svg"));

  // Prevent Qt from quitting when the last visible window closes
  // This is needed because we have system tray functionality and parentless
  // dialogs
  app.setQuitOnLastWindowClosed(false);

// === Single-instance enforcement ===
#ifdef Q_OS_WIN
  HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GW2AIO_SingleInstance_Mutex");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // Another instance is running - try to bring it to foreground
    HWND existingWindow = FindWindowW(nullptr, L"GW2 AIO Manager");
    if (existingWindow) {
      // Restore minimized/hidden window and bring to front
      ShowWindow(existingWindow, SW_SHOW);
      ShowWindow(existingWindow, SW_RESTORE);
      SetForegroundWindow(existingWindow);
      // Force repaint to avoid white window
      InvalidateRect(existingWindow, nullptr, TRUE);
      UpdateWindow(existingWindow);
    }
    CloseHandle(hMutex);
    return 0;
  }
#else
  // Cross-platform fallback using shared memory
  QSharedMemory sharedMem("GW2AIO_SingleInstance");
  if (!sharedMem.create(1)) {
    // Styled warning dialog (non-Windows fallback)
    QDialog d;
    d.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    d.setAttribute(Qt::WA_TranslucentBackground);
    d.setMinimumWidth(350);
    auto *ol = new QVBoxLayout(&d);
    ol->setContentsMargins(0, 0, 0, 0);
    auto *bg = new QWidget();
    UIHelpers::applyWindowBackgroundRole(bg);
    ol->addWidget(bg);
    auto *ly = new QVBoxLayout(bg);
    ly->setContentsMargins(20, 20, 20, 20);
    auto *lb = new QLabel("GW2 AIO Manager is already running.");
    UIHelpers::applyLabelRole(lb);
    lb->setAlignment(Qt::AlignCenter);
    ly->addWidget(lb);
    auto *ok = new QPushButton("OK");
    ok->setMinimumHeight(36);
    UIHelpers::applyPrimaryStyle(ok);
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    ly->addWidget(ok);
    d.exec();
    return 0;
  }
#endif

  // Parse command line
  CommandLineParser cmdParser;
  cmdParser.parse(app.arguments());
  const auto &opts = cmdParser.options();

  // Initialize app config (portable mode detection)
  AppConfig::instance().initialize();

  // Configure QSettings for cross-platform compatibility
  // ALWAYS use INI format instead of Windows Registry for:
  // - Cross-platform support (Linux, macOS, Steam Deck)
  // - Portable mode (USB installs)
  // - Human-readable/editable settings
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     AppConfig::instance().dataDir());
  qInfo() << "Settings stored in:" << AppConfig::instance().dataDir();

  // Install crash handler
  CrashHandler::instance().install();

  // Check for previous crash
  if (CrashHandler::instance().hadPreviousCrash()) {
    qWarning() << "Previous session crashed. Check logs.";
    CrashHandler::instance().clearCrashFlag();
  }

  // Initialize logger
  Logger::instance().initialize(AppConfig::instance().logsDir());
  Logger::instance().setConsoleOutput(opts.consoleLog);

  qInfo() << "GW2 AIO Manager starting...";
  qInfo() << "Data directory:" << AppConfig::instance().dataDir();

  // Apply default theme (will be re-applied from saved setting after
  // DataService init)
  ThemeManager::instance().setBuiltinTheme(
      ThemeManager::BuiltinTheme::ClassicGold);

  // Cleanup orphaned CEF processes from previous sessions
  CefManager::instance().checkForOrphansOnStartup();

  // Install custom tooltip event filter for styled tooltips
  app.installEventFilter(new TooltipEventFilter());

  // Create centralized data service — the single gateway for all data
  // operations. Owns ProfileManager, SettingsManager, UpdateManager.
  DataService dataService;

  // Restore saved theme (default = ClassicGold = 0)
  int savedTheme = dataService.selectedTheme();
  auto themes = ThemeManager::builtinThemes();
  if (savedTheme >= 0 && savedTheme < themes.size()) {
    ThemeManager::instance().setBuiltinTheme(themes.at(savedTheme));
  }

  // Check if should start minimized (command-line OR user setting)
  bool startMinimized = opts.startMinimized || dataService.startMinimized();
  bool showTrayIcon = dataService.showTrayIcon();

  // CRITICAL: If tray icon is disabled, we MUST show the window
  // otherwise the app runs with no visible UI!
  if (startMinimized && !showTrayIcon) {
    qWarning() << "startMinimized=true but showTrayIcon=false - forcing window "
                  "to show";
    startMinimized = false;
  }

  // Show splash screen
  SplashScreen splash;
  if (!startMinimized) {
    splash.show();
    splash.setStatus("Initializing...");
    splash.setProgress(10);
  }

  // Check if first run - show setup wizard
  if (!dataService.setting("general/setupComplete", false).toBool()) {
    splash.hide();
    SetupWizard wizard(dataService.settingsManager());
    if (wizard.exec() != QDialog::Accepted) {
      return 0; // User cancelled
    }
    splash.show();
  }

  // Detect GW2
  splash.setStatus("Detecting Guild Wars 2...");
  splash.setProgress(20);

  GW2Detector detector;
  QString gw2Path = dataService.setting("general/gw2Path").toString();
  if (gw2Path.isEmpty()) {
    gw2Path = detector.detectGW2Path();
  }

  if (gw2Path.isEmpty()) {
    qWarning() << "GW2 installation not found!";
  } else {
    qInfo() << "Found GW2 at:" << gw2Path;
  }

  // Initialize Mumble Link (trigger-based: starts when GW2 confirmed running)
  splash.setStatus("Initializing Mumble Link...");
  splash.setProgress(40);

  MumbleLink mumbleLink;

  // Initialize feature controllers
  splash.setStatus("Loading radial menus...");
  splash.setProgress(50);

  RadialController radialController(&mumbleLink);
  if (dataService.setting("radial/enabled", true).toBool() && !opts.noRadial) {
    radialController.start();
  }

  splash.setStatus("Loading DPS tracker...");
  splash.setProgress(60);

  DPSController dpsController(&mumbleLink);
  // DPS meter is not implemented yet — disabled unconditionally
  // TODO: Re-enable when DPS feature is ready
  // if (dataService.setting("dps/enabled", false).toBool() && !opts.noDPS) {
  //   dpsController.start();
  //   dpsController.setMeterVisible(true);
  // }

  splash.setStatus("Loading marker system...");
  splash.setProgress(70);

  MarkerController markerController(&mumbleLink);
  markerController.setActivationStore(dataService.activationStore());
  markerController.setCacheDir(AppConfig::instance().markerPacksCacheDir());
  markerController.setMarkerSettings(dataService.markerSettings());

  // Wire pack-loading progress to splash (70–85% range)
  QObject::connect(
      markerController.manager(), &MarkerManager::packsLoadProgress, &splash,
      [&splash](int current, int total, const QString &name) {
        Q_UNUSED(name);
        splash.setStatus(
            QString("Loading marker pack %1/%2...").arg(current).arg(total));
        splash.setProgress(70 + (current * 15 / qMax(total, 1)));
      });

  if (dataService.setting("markers/enabled", true).toBool() &&
      !opts.noMarkers) {
    markerController.start();
  }

  // Initialize overlay window (in-game HUD)
  // Uses shared MumbleLink — trigger-based show/hide via connectionChanged
  OverlayWindow overlayWindow(&mumbleLink);
  overlayWindow.setMarkerController(&markerController);
  overlayWindow.setMarkerSettings(dataService.markerSettings());

  // Wire MumbleLink connection to MinimapRenderer lifecycle:
  // Show minimap markers when GW2 connects, hide when disconnects.
  QObject::connect(&mumbleLink, &MumbleLink::connectionChanged,
                   &markerController, &MarkerController::setVisible);

  // OverlayWindow internally connects to MumbleLink::connectionChanged
  // for trigger-based show/hide (startTracking/stopTracking via
  // onGameConnected)

  // D3D11 overlay window (native Win32 + Direct3D 11 rendering)
  // Renders markers and trails in-game via GPU-accelerated D3D11 pipeline
  D3D11OverlayWindow d3dOverlay(&mumbleLink);
  d3dOverlay.setMarkerManager(markerController.manager());
  d3dOverlay.setMarkerSettings(dataService.markerSettings());
  d3dOverlay.setImageCache(markerController.imageCache());
  d3dOverlay.setQtOverlayHwnd(
      reinterpret_cast<HWND>(overlayWindow.winId()));
  d3dOverlay.startTracking();

  // Wire Details Tracker toggle: OverlayMenuWidget → OverlayWindow → D3D11
  QObject::connect(&overlayWindow, &OverlayWindow::detailsTrackerToggled,
                   &d3dOverlay, &D3D11OverlayWindow::toggleDebugOverlay);

  splash.setStatus("Loading Blish modules...");
  splash.setProgress(80);

  ModuleController moduleController(&mumbleLink);
  if (dataService.setting("modules/enabled", true).toBool() &&
      !opts.noModules) {
    if (moduleController.start()) {
      qInfo() << "Blish modules:" << moduleController.modules().size()
              << "discovered";
    } else {
      qInfo() << "Blish modules: disabled (install .NET 6+)";
    }
  }

  splash.setStatus("Starting UI...");
  splash.setProgress(90);

  // Note: MainWindow creates its own tray icon in setupTrayIcon()

  // Create main window
  MainWindow mainWindow(&dataService, &markerController);
  mainWindow.setGW2Path(gw2Path);

  // Trigger-based: start MumbleLink when GW2 window is confirmed
  // (profileWindowConfirmed fires after GW2WindowWatcher detects the window)
  QObject::connect(mainWindow.launchManager(),
                   &LaunchManager::profileWindowConfirmed, &mumbleLink,
                   [&mumbleLink, &dataService,
                    lm = mainWindow.launchManager()](const QString &profileId) {
                     // Set correct link name for multibox isolation
                     QString linkName = lm->mumbleLinkNameForProfile(profileId);
                     if (linkName.isEmpty()) {
                       linkName = QStringLiteral("MumbleLink");
                     }
                     mumbleLink.setLinkName(linkName);
                     // Start polling if not already running
                     if (!mumbleLink.isRunning()) {
      mumbleLink.start(10);
                     }
                     // Load per-profile marker settings (exclusion zones,
                     // display prefs)
                     dataService.markerSettings()->loadForProfile(profileId);
                     qInfo() << "MumbleLink started for profile — segment:"
                             << linkName;
                   });

  splash.setStatus("Ready!");
  splash.setProgress(100);

  // Show main window or start minimized
  mainWindow.resize(1200, 800);
  QScreen *primaryScreen = QGuiApplication::primaryScreen();
  if (primaryScreen) {
    QRect screenGeom = primaryScreen->availableGeometry();
    int x = (screenGeom.width() - mainWindow.width()) / 2 + screenGeom.left();
    int y = (screenGeom.height() - mainWindow.height()) / 2 + screenGeom.top();
    mainWindow.move(x, y);
  }

  // Show main window or start minimized
  if (!startMinimized) {
    mainWindow.show();
  }
  // When startMinimized is true, window stays hidden - will show from tray

  splash.finish(&mainWindow);

  // If GW2 is already running (AIO restarted), start MumbleLink + overlay
  // for the first running profile. This enables overlay persistence across
  // AIO restarts — the user sees the overlay re-appear automatically.
  auto running = dataService.runningProfiles();
  if (!running.isEmpty()) {
    auto it = running.begin();
    QString profileId = it.key();
    // Read persisted mumble link name (saved alongside PID in running_profiles.json)
    QString linkName = dataService.profileManager()->mumbleLinkNameForRunningProfile(profileId);
    if (linkName.isEmpty()) {
      linkName = QStringLiteral("MumbleLink");
    }
    mumbleLink.setLinkName(linkName);
    if (!mumbleLink.isRunning()) {
      mumbleLink.start(10);
    }
    // Load per-profile marker settings (exclusion zones, display prefs)
    dataService.markerSettings()->loadForProfile(profileId);
    qInfo() << "MumbleLink started for already-running GW2 — profile:"
            << profileId << "segment:" << linkName;
  }

  qInfo() << "GW2 AIO Manager started";
  qInfo() << "Features active:";
  if (!opts.noRadial)
    qInfo() << "  - Radial menus: V=mounts, N=novelties, M=markers";
  if (!opts.noDPS)
    qInfo() << "  - DPS meter: visible (draggable)";
  if (!opts.noMarkers)
    qInfo() << "  - Marker system: TacO packs from"
            << AppConfig::instance().markerPacksDir();
  if (!opts.noModules)
    qInfo() << "  - Blish modules:" << moduleController.modules().size();

  // Auto-launch GW2 if requested
  if (opts.autoLaunchGW2 && !gw2Path.isEmpty()) {
    LaunchManager launcher;
    launcher.setGw2Path(gw2Path);
    LaunchProfile profile;
    profile.name = "AutoLaunch";
    launcher.launchGW2(profile);
  }

  int result = app.exec();

  // Cleanup
  qInfo() << "Shutting down...";

  moduleController.stop();
  markerController.stop();
  dpsController.stop();
  radialController.stop();
  d3dOverlay.stopTracking();
  mumbleLink.stop();

  // Clear crash flag on clean exit
  CrashHandler::instance().clearCrashFlag();

// Close single-instance mutex
#ifdef Q_OS_WIN
  if (hMutex) {
    CloseHandle(hMutex);
  }
#endif

  qInfo() << "GW2 AIO Manager exited cleanly";

  // Restore Windows timer resolution
#ifdef Q_OS_WIN
  timeEndPeriod(1);
#endif

  // Re-install crash handler — Qt may have overridden SetUnhandledExceptionFilter
  // during event loop setup. The crash occurs during stack destruction BELOW,
  // so our handler must be active at this point. Use direct API call instead of
  // install() to avoid writeCrashFlag() side-effect (QFile may be unsafe here).
  SetUnhandledExceptionFilter(CrashHandler::unhandledExceptionFilter);

  return result;
}
