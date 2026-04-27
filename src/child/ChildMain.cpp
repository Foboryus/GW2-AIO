/**
 * @file ChildMain.cpp
 * @brief Entry point for AIO child processes
 *
 * Each child executable is a hardlink to the feature-specific base exe
 * (e.g., GW2AIO-3d.exe). The feature type is determined at compile time
 * by the CMake target, not from argv[0], since each feature is a
 * separate executable target.
 *
 * CLI arguments:
 *   --profile <uuid>           Profile ID
 *   --mumble <segment_name>    MumbleLink shared memory segment name
 *   --pid <gw2_pid>            GW2 process ID to monitor
 *   --pipe <pipe_name>         Named pipe for grandfather IPC
 *   --profile-name <name>      Human-readable profile nickname
 *
 * The child process:
 * 1. Parses CLI arguments
 * 2. Initializes Qt, AppConfig, Logger (writing to children/ subdirectory)
 * 3. Creates the appropriate feature child (e.g., Child3DOverlay)
 * 4. Runs the Qt event loop until GW2 exits or STOP received
 */

#include "ChildProcess.h"
#include "core/AppConfig.h"
#include "core/Logger.h"

// Include feature-specific child headers
// Each CMake target compiles this file with the corresponding child class
#ifdef CHILD_FEATURE_3D
#include "Child3DOverlay.h"
#endif
#ifdef CHILD_FEATURE_MINIMAP
#include "ChildMinimap.h"
#endif
#ifdef CHILD_FEATURE_BIGMAP
#include "ChildBigMap.h"
#endif
#ifdef CHILD_FEATURE_RADIAL
#include "ChildRadial.h"
#endif
#ifdef CHILD_FEATURE_OVERLAY
#include "ChildOverlay.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // CRITICAL: Must match parent's app name for QStandardPaths to resolve
    // to the same AppDataLocation (logs, MarkerPacks, settings, etc.)
    app.setApplicationName("GW2 AIO Manager");
    app.setOrganizationName("GW2AIO");
    app.setApplicationVersion(APP_VERSION);

    // === Parse CLI arguments ===
    QCommandLineParser parser;
    parser.setApplicationDescription("GW2 AIO Child Process");

    QCommandLineOption profileOpt("profile", "Profile UUID", "uuid");
    QCommandLineOption mumbleOpt("mumble", "MumbleLink segment name", "name");
    QCommandLineOption pidOpt("pid", "GW2 process PID", "pid");
    QCommandLineOption pipeOpt("pipe", "Named pipe for IPC", "pipe");
    QCommandLineOption nameOpt("profile-name", "Profile display name", "name");

    parser.addOption(profileOpt);
    parser.addOption(mumbleOpt);
    parser.addOption(pidOpt);
    parser.addOption(pipeOpt);
    parser.addOption(nameOpt);
    parser.addHelpOption();
    parser.process(app);

    const QString profileId = parser.value(profileOpt);
    const QString mumbleName = parser.value(mumbleOpt);
    const qint64 gw2Pid = parser.value(pidOpt).toLongLong();
    const QString pipeName = parser.value(pipeOpt);
    const QString profileName = parser.value(nameOpt);

    // Validate required arguments
    if (profileId.isEmpty() || mumbleName.isEmpty() || gw2Pid <= 0) {
        qCritical() << "Missing required arguments. Usage:";
        qCritical() << "  --profile <uuid> --mumble <name> --pid <pid>"
                     << " [--pipe <name>] [--profile-name <name>]";
        return 1;
    }

    // === Initialize AppConfig ===
    AppConfig::instance().initialize();

    // === Initialize Logger to children/ subdirectory ===
    QString childrenLogDir = QDir(AppConfig::instance().logsDir())
                                 .filePath("children");
    QDir().mkpath(childrenLogDir);
    Logger::instance().initialize(childrenLogDir);
    Logger::instance().setConsoleOutput(true);

    qInfo() << "=== GW2 AIO Child Process ===";
    qInfo() << "Profile:" << profileName << "(" << profileId << ")";
    qInfo() << "MumbleLink:" << mumbleName;
    qInfo() << "GW2 PID:" << gw2Pid;
    qInfo() << "Pipe:" << (pipeName.isEmpty() ? "(none)" : pipeName);

    // === Create feature-specific child ===
    ChildProcess *child = nullptr;

#ifdef CHILD_FEATURE_3D
    child = new Child3DOverlay(profileId, mumbleName, gw2Pid,
                               pipeName, profileName);
    qInfo() << "Feature: 3D Overlay";
#elif defined(CHILD_FEATURE_MINIMAP)
    child = new ChildMinimap(profileId, mumbleName, gw2Pid,
                             pipeName, profileName);
    qInfo() << "Feature: Minimap";
#elif defined(CHILD_FEATURE_BIGMAP)
    child = new ChildBigMap(profileId, mumbleName, gw2Pid,
                            pipeName, profileName);
    qInfo() << "Feature: Big Map";
#elif defined(CHILD_FEATURE_RADIAL)
    child = new ChildRadial(profileId, mumbleName, gw2Pid,
                            pipeName, profileName);
    qInfo() << "Feature: Radial Menu";
#elif defined(CHILD_FEATURE_OVERLAY)
    child = new ChildOverlay(profileId, mumbleName, gw2Pid,
                             pipeName, profileName);
    qInfo() << "Feature: Overlay HUD";
#else
    qCritical() << "No feature defined at compile time!";
    return 1;
#endif

    // === Connect exit signal to application quit ===
    QObject::connect(child, &ChildProcess::exitRequested, &app, [&app, child]() {
        qInfo() << "Child process exiting...";
        child->deleteLater();
        app.quit();
    });

    // === Start the child ===
    if (!child->start()) {
        qCritical() << "Failed to start child process";
        delete child;
        return 1;
    }

    // === Run event loop ===
    int result = app.exec();

    qInfo() << "Child process finished with code" << result;
    Logger::instance().shutdown();
    return result;
}
