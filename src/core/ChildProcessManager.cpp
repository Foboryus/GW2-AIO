/**
 * @file ChildProcessManager.cpp
 * @brief Grandfather-side manager for child process lifecycle
 *
 * See ChildProcessManager.h for class documentation.
 */

#include "ChildProcessManager.h"
#include "LaunchManager.h"
#include "MumbleLink.h"
#include "ProfileManager.h"

#include "core/AppConfig.h"
#include "core/OverlayInstance.h"
#include "core/OverlayInstanceManager.h"
#include "core/RadialSettingsManager.h"
#include "features/markers/MarkerSettingsManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

// ============================================================================
// Constructor / Destructor
// ============================================================================

ChildProcessManager::ChildProcessManager(LaunchManager *launchManager,
                                         ProfileManager *profileManager,
                                         QObject *parent)
    : QObject(parent)
    , m_launchManager(launchManager)
    , m_profileManager(profileManager)
{
    // Resolve children directory: same directory as the running exe
    // (build/Release/lib/ — where GW2AIO_app.exe and child exes live)
    m_childrenDir = QCoreApplication::applicationDirPath();

    // Self-contained wiring: connect to LaunchManager signals internally

    // Cache PID when a profile launches
    connect(m_launchManager, &LaunchManager::profileLaunched,
            this, &ChildProcessManager::onProfileLaunched);

    // Spawn children when game reaches character select (LOADED signal)
    connect(m_launchManager, &LaunchManager::profileCharacterSelectReached,
            this, &ChildProcessManager::onProfileWindowConfirmed);

    // Terminate children when GW2 exits
    connect(m_launchManager, &LaunchManager::profileExited,
            this, &ChildProcessManager::onProfileExited);

    // Create Job Object for automatic child cleanup
    ensureJobObject();

    // Startup cleanup: remove stale hardlinks from previous sessions
    // Hardlinks use UUID format: GW2AIO-<feature>-<uuid>.exe
    QDir dir(m_childrenDir);
    const QStringList staleLinks = dir.entryList(
        {QStringLiteral("GW2AIO-*-*-*-*-*-*.exe")}, QDir::Files);
    for (const QString &file : staleLinks) {
        // Skip base executables (no UUID suffix)
        // Base exes: GW2AIO-3d.exe, GW2AIO-overlay.exe, etc.
        // Hardlinks: GW2AIO-3d-ecd7fdcc-9ca6-4b2a-8f1e-1a2b3c4d5e6f.exe
        QString fullPath = dir.absoluteFilePath(file);
        if (QFile::remove(fullPath)) {
            qInfo() << "ChildProcessManager: cleaned stale hardlink:" << file;
        }
    }

    qInfo() << "ChildProcessManager: created — childrenDir:" << m_childrenDir;
}

ChildProcessManager::~ChildProcessManager()
{
    stopPipePolling();
    terminateAll();

    if (m_jobObject) {
        CloseHandle(m_jobObject);
        m_jobObject = nullptr;
    }

    qInfo() << "ChildProcessManager: destroyed";
}

// ============================================================================
// Public API
// ============================================================================

void ChildProcessManager::spawnForRunningProfiles()
{
    const QMap<QString, qint64> running = m_profileManager->runningProfiles();
    if (running.isEmpty()) {
        qInfo() << "ChildProcessManager: No running profiles — nothing to reconnect";
        return;
    }

    qInfo() << "ChildProcessManager: Reconnecting to" << running.size()
            << "running profile(s)";

    for (auto it = running.constBegin(); it != running.constEnd(); ++it) {
        const QString &profileId = it.key();
        qint64 pid = it.value();

        // Skip if children already exist for this profile
        if (m_children.contains(profileId) && !m_children[profileId].isEmpty()) {
            qInfo() << "ChildProcessManager: Children already exist for"
                    << profileId << "— skipping";
            continue;
        }

        // Cache the PID (onProfileLaunched normally does this)
        m_profilePids[profileId] = pid;

        // Resolve mumble link name
        QString mumbleName = m_profileManager->mumbleLinkNameForRunningProfile(profileId);
        if (mumbleName.isEmpty()) {
            mumbleName = m_launchManager->mumbleLinkNameForProfile(profileId);
        }
        if (mumbleName.isEmpty()) {
            mumbleName = QStringLiteral("MumbleLink");
        }

        // Check MumbleLink for GPU activity / game-loaded state
        // uiTick > 0 means the game engine is rendering (character select or in-game)
        // uiTick == 0 means the game is still on splash/login screen
        // Direct Win32 shared memory read — no timer, no QObject, just a one-shot check
#ifdef Q_OS_WIN
        uint32_t probeUiTick = 0;
        {
            HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, mumbleName.toStdWString().c_str());
            if (hMap) {
                const auto *mem = static_cast<const LinkedMem *>(
                    MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(LinkedMem)));
                if (mem) {
                    probeUiTick = mem->uiTick;
                    UnmapViewOfFile(mem);
                }
                CloseHandle(hMap);
            } else {
                qInfo() << "ChildProcessManager: MumbleLink segment" << mumbleName
                        << "not available for" << profileId << "— game may not be ready";
                continue;
            }
        }

        if (probeUiTick == 0) {
            qInfo() << "ChildProcessManager: MumbleLink uiTick=0 for" << profileId
                    << "— game still on splash screen, deferring child spawn";
            continue;
        }

        qInfo() << "ChildProcessManager: MumbleLink active (uiTick="
                << probeUiTick << ") for" << profileId
                << "— game is loaded, spawning children";
#endif

        // Resolve profile nickname for window rename
        const QList<AccountProfile> &profiles = m_profileManager->profiles();
        QString profileName;
        for (const auto &p : profiles) {
            if (p.id == profileId) {
                profileName = p.nickname;
                break;
            }
        }

        // Rename the GW2 window title (if not already renamed)
        if (!profileName.isEmpty()) {
            m_launchManager->setWindowTitle(pid, profileName);
        }

        qInfo() << "ChildProcessManager: Spawning children for reconnected profile:"
                << profileId << "mumble:" << mumbleName << "pid:" << pid;

        spawnChildren(profileId, mumbleName, pid);
    }
}

void ChildProcessManager::spawnChildren(const QString &profileId,
                                        const QString &mumbleName,
                                        qint64 gw2Pid)
{
    qInfo() << "ChildProcessManager: spawning children for profile:"
            << profileId << "mumble:" << mumbleName << "pid:" << gw2Pid;

    // Load per-profile rendering toggles to determine which features to spawn
    const QString markerStateDir = AppConfig::instance().markerStateDir();
    MarkerSettingsManager settings(markerStateDir);
    settings.loadForProfile(profileId);

    // Main kill switch — if rendering is globally disabled, skip all children
    if (!settings.renderingEnabled()) {
        qInfo() << "ChildProcessManager: rendering disabled for" << profileId
                << "— skipping all children";
        return;
    }

    // === Enqueue compositor FIRST (must be running before feature children) ===
    m_spawnQueue.append({profileId, mumbleName, gw2Pid, QStringLiteral("compositor")});

    // === Enqueue feature children (serialized — one at a time via READY signals) ===
    struct FeatureToggle {
        QString key;
        bool enabled;
    };
    // Load radial toggle from per-profile radial settings
    const QString radialConfigDir = AppConfig::instance().radialConfigDir();
    RadialSettingsManager radialSettings(radialConfigDir);
    radialSettings.loadForProfile(profileId);

    const QList<FeatureToggle> features = {
        {QStringLiteral("3d"),      settings.render3dEnabled()},
        {QStringLiteral("minimap"), settings.renderMinimapEnabled()},
        {QStringLiteral("bigmap"),  settings.renderBigMapEnabled()},
        {QStringLiteral("radial"),  radialSettings.settings().radialEnabled},
        {QStringLiteral("overlay"), true},  // Always spawn — overlay HUD menu
    };

    for (const auto &feat : features) {
        if (feat.enabled) {
            m_spawnQueue.append({profileId, mumbleName, gw2Pid, feat.key});
        } else {
            qInfo() << "ChildProcessManager: feature" << feat.key
                    << "disabled for profile" << profileId << "— skipped";
        }
    }

    qInfo() << "[COMPOSITOR_LIFECYCLE] Enqueued" << m_spawnQueue.size()
            << "children for" << profileId << "(serialized spawn queue)";

    // Ensure pipe polling is running to read READY signals from children
    startPipePolling();

    // Start processing the queue if not already in progress
    if (!m_spawnInProgress) {
        processSpawnQueue();
    }
}

void ChildProcessManager::processSpawnQueue()
{
    if (m_spawnQueue.isEmpty()) {
        m_spawnInProgress = false;
        qInfo() << "[COMPOSITOR_LIFECYCLE] Spawn queue empty — all children launched";
        return;
    }

    m_spawnInProgress = true;
    const SpawnRequest req = m_spawnQueue.takeFirst();

    qInfo() << "[COMPOSITOR_LIFECYCLE] Spawning next in queue:" << req.featureKey
            << "for" << req.profileId
            << "(remaining:" << m_spawnQueue.size() << ")";

    spawnChild(req.profileId, req.mumbleName, req.gw2Pid, req.featureKey);
    // m_spawnInProgress stays true — processSpawnQueue() will be called
    // again when the child sends READY or COMPOSITOR_READY
}

void ChildProcessManager::terminateChildren(const QString &profileId)
{
    if (!m_children.contains(profileId)) {
        return;
    }

    QList<ChildProcessInfo> &children = m_children[profileId];
    int totalChildren = children.size();

    qInfo() << "[CHILD_EXIT] BEGIN terminateChildren — profile:" << profileId
            << "count:" << totalChildren;

    int childIdx = 0;
    for (ChildProcessInfo &child : children) {
        childIdx++;
        qInfo() << "[CHILD_EXIT] Child" << childIdx << "/" << totalChildren
                << "feature:" << child.featureKey
                << "profile:" << child.profileName
                << "pid:" << child.processId
                << "hasPipe:" << (child.pipeHandle != INVALID_HANDLE_VALUE)
                << "hasProcess:" << (child.processHandle != nullptr);

        // Try graceful via pipe first: send STOP command
        if (child.pipeHandle != INVALID_HANDLE_VALUE) {
            qInfo() << "[CHILD_EXIT]   Step 1: sending STOP via pipe...";
            const char *stopMsg = "STOP\n";
            DWORD written = 0;
            BOOL writeOk = WriteFile(child.pipeHandle, stopMsg,
                      static_cast<DWORD>(strlen(stopMsg)), &written, nullptr);
            FlushFileBuffers(child.pipeHandle);
            qInfo() << "[CHILD_EXIT]   Step 1: STOP sent — writeOk:" << writeOk
                    << "bytes:" << written;

            // Wait briefly for graceful exit
            if (child.processHandle) {
                qInfo() << "[CHILD_EXIT]   Step 2: waiting 2s for graceful exit...";
                DWORD waitResult = WaitForSingleObject(child.processHandle, 2000);
                if (waitResult != WAIT_OBJECT_0) {
                    // Force terminate if didn't exit gracefully
                    qWarning() << "[CHILD_EXIT]   Step 2: TIMEOUT — force-terminating"
                               << child.featureKey;
                    TerminateProcess(child.processHandle, 1);
                } else {
                    qInfo() << "[CHILD_EXIT]   Step 2: graceful exit confirmed";
                }
            }
        } else if (child.processHandle) {
            // No pipe — force terminate
            qInfo() << "[CHILD_EXIT]   Step 1: no pipe — force-terminating";
            TerminateProcess(child.processHandle, 1);
        }

        // Cleanup handles
        if (child.pipeHandle != INVALID_HANDLE_VALUE) {
            qInfo() << "[CHILD_EXIT]   Step 3: closing pipe handle...";
            DisconnectNamedPipe(child.pipeHandle);
            CloseHandle(child.pipeHandle);
            child.pipeHandle = INVALID_HANDLE_VALUE;
            qInfo() << "[CHILD_EXIT]   Step 3: pipe closed";
        }
        if (child.processHandle) {
            qInfo() << "[CHILD_EXIT]   Step 4: closing process handle...";
            CloseHandle(child.processHandle);
            child.processHandle = nullptr;
            qInfo() << "[CHILD_EXIT]   Step 4: process handle closed";
        }

        // Clean up hardlink
        if (!child.exePath.isEmpty() && QFileInfo::exists(child.exePath)) {
            qInfo() << "[CHILD_EXIT]   Step 5: removing hardlink:" << child.exePath;
            QFile::remove(child.exePath);
        }

        qInfo() << "[CHILD_EXIT]   Step 6: emitting childTerminated signal...";
        emit childTerminated(profileId, child.featureKey);
        qInfo() << "[CHILD_EXIT] Child" << childIdx << "/" << totalChildren
                << "DONE —" << child.featureKey;
    }

    m_children.remove(profileId);
    m_profilePids.remove(profileId);

    qInfo() << "[CHILD_EXIT] END terminateChildren — profile:" << profileId;
}

void ChildProcessManager::terminateChild(const QString &profileId,
                                         const QString &featureKey)
{
    if (!m_children.contains(profileId)) {
        return;
    }

    QList<ChildProcessInfo> &children = m_children[profileId];
    for (int i = 0; i < children.size(); ++i) {
        ChildProcessInfo &child = children[i];
        if (child.featureKey != featureKey) {
            continue;
        }

        qInfo() << "ChildProcessManager: terminating" << featureKey
                << "child for profile" << child.profileName;

        // Graceful shutdown via pipe
        if (child.pipeHandle != INVALID_HANDLE_VALUE) {
            const char *stopMsg = "STOP\n";
            DWORD written = 0;
            WriteFile(child.pipeHandle, stopMsg,
                      static_cast<DWORD>(strlen(stopMsg)), &written, nullptr);
            FlushFileBuffers(child.pipeHandle);

            if (child.processHandle) {
                DWORD waitResult = WaitForSingleObject(child.processHandle, 2000);
                if (waitResult != WAIT_OBJECT_0) {
                    TerminateProcess(child.processHandle, 1);
                }
            }
        } else if (child.processHandle) {
            TerminateProcess(child.processHandle, 1);
        }

        // Cleanup
        if (child.pipeHandle != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(child.pipeHandle);
            CloseHandle(child.pipeHandle);
        }
        if (child.processHandle) {
            CloseHandle(child.processHandle);
        }
        if (!child.exePath.isEmpty() && QFileInfo::exists(child.exePath)) {
            QFile::remove(child.exePath);
        }

        emit childTerminated(profileId, featureKey);
        children.removeAt(i);

        // Remove profile entry if no children remain
        if (children.isEmpty()) {
            m_children.remove(profileId);
        }
        return;
    }
}

void ChildProcessManager::terminateAll()
{
    if (m_children.isEmpty()) {
        return;
    }

    qInfo() << "ChildProcessManager: terminating all children ("
            << childCount() << "total)";

    const QList<QString> profileIds = m_children.keys();
    for (const QString &profileId : profileIds) {
        terminateChildren(profileId);
    }
}

void ChildProcessManager::pushSettings(const QString &profileId,
                                       const QJsonObject &settings)
{
    if (!m_children.contains(profileId)) {
        return;
    }

    QByteArray payload = "SETTINGS\n" +
        QJsonDocument(settings).toJson(QJsonDocument::Compact) + "\n";

    for (ChildProcessInfo &child : m_children[profileId]) {
        if (child.pipeHandle != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(child.pipeHandle, payload.constData(),
                      static_cast<DWORD>(payload.size()), &written, nullptr);
        }
    }
}

void ChildProcessManager::syncFeatureToggles(const QString &profileId)
{
    // Read marker toggles from the LIVE parent instance (avoids disk race).
    // The live instance is always up-to-date because applyDisplayJson() and
    // Profile Editor setters update it directly. Disk may lag due to the
    // 2-second debounced save, causing stale reads and spurious child kills.
    bool renderingEnabled = true;
    bool render3dEnabled = true;
    bool renderMinimapEnabled = true;
    bool renderBigMapEnabled = true;

    if (m_overlayInstanceMgr) {
        auto *inst = m_overlayInstanceMgr->instance(profileId);
        if (inst && inst->markerSettings()) {
            auto *ms = inst->markerSettings();
            renderingEnabled     = ms->renderingEnabled();
            render3dEnabled      = ms->render3dEnabled();
            renderMinimapEnabled = ms->renderMinimapEnabled();
            renderBigMapEnabled  = ms->renderBigMapEnabled();
        }
    } else {
        // Fallback: no live instance yet — read from disk
        const QString markerStateDir = AppConfig::instance().markerStateDir();
        MarkerSettingsManager settings(markerStateDir);
        settings.loadForProfile(profileId);
        renderingEnabled     = settings.renderingEnabled();
        render3dEnabled      = settings.render3dEnabled();
        renderMinimapEnabled = settings.renderMinimapEnabled();
        renderBigMapEnabled  = settings.renderBigMapEnabled();
    }

    // Build desired state map
    struct FeatureToggle {
        QString key;
        bool enabled;
    };
    // Load radial toggle from per-profile radial settings (on disk — no live
    // radial instance in the parent; child saves synchronously before IPC)
    const QString radialConfigDir = AppConfig::instance().radialConfigDir();
    RadialSettingsManager radialSettings(radialConfigDir);
    radialSettings.loadForProfile(profileId);

    const QList<FeatureToggle> featureToggles = {
        {QStringLiteral("compositor"), true},  // Compositor — always on
        {QStringLiteral("3d"),      renderingEnabled && render3dEnabled},
        {QStringLiteral("minimap"), renderingEnabled && renderMinimapEnabled},
        {QStringLiteral("bigmap"),  renderingEnabled && renderBigMapEnabled},
        {QStringLiteral("radial"),  renderingEnabled && radialSettings.settings().radialEnabled},
        // Overlay is ALWAYS ON — it's the control panel. Never kill it.
        // Users need it to re-enable rendering if they disabled it.
        {QStringLiteral("overlay"), true},
    };

    // Build set of currently running features for this profile
    QSet<QString> runningFeatures;
    if (m_children.contains(profileId)) {
        for (const auto &child : m_children[profileId]) {
            runningFeatures.insert(child.featureKey);
        }
    }

    qint64 gw2Pid = m_profilePids.value(profileId, 0);
    QString mumbleName = m_launchManager->mumbleLinkNameForProfile(profileId);
    if (mumbleName.isEmpty()) {
        mumbleName = QStringLiteral("MumbleLink");
    }

    // ---------------------------------------------------------------
    // Relay current toggle state to ALL running children via SETTINGS.
    // This covers both the overlay-IPC path (redundant but harmless)
    // and the Profile Editor path (no prior IPC relay — essential).
    // Children handle pause/resume internally via onSettingsReceived().
    // ---------------------------------------------------------------
    QJsonObject togglePayload;
    togglePayload[QStringLiteral("renderingEnabled")]     = renderingEnabled;
    togglePayload[QStringLiteral("render3dEnabled")]      = render3dEnabled;
    togglePayload[QStringLiteral("renderMinimapEnabled")] = renderMinimapEnabled;
    togglePayload[QStringLiteral("renderBigMapEnabled")]  = renderBigMapEnabled;

    QJsonDocument doc(togglePayload);
    QByteArray relay = "SETTINGS\n" + doc.toJson(QJsonDocument::Compact);

    if (m_children.contains(profileId)) {
        for (auto &child : m_children[profileId]) {
            if (child.pipeHandle == INVALID_HANDLE_VALUE) continue;
            DWORD written = 0;
            WriteFile(child.pipeHandle, relay.constData(),
                      static_cast<DWORD>(relay.size()), &written, nullptr);
        }
    }

    // Only SPAWN children that are enabled but not running.
    // Never kill running children — the SETTINGS relay above tells them
    // to stop rendering. This avoids:
    //   - 7+ second pack reload on respawn
    //   - Focus race (new child starts unfocused → D3D11 deferred)
    //   - SharedTexture destruction → compositor gap
    for (const auto &feat : featureToggles) {
        bool isRunning = runningFeatures.contains(feat.key);

        if (feat.enabled && !isRunning && gw2Pid > 0) {
            // Feature enabled but child not running → spawn
            qInfo() << "ChildProcessManager: feature" << feat.key
                    << "re-enabled for" << profileId << "— spawning child";
            spawnChild(profileId, mumbleName, gw2Pid, feat.key);
        }
        // NOTE: Disabled-but-running is handled by the SETTINGS relay above.
        // The child pauses rendering internally (<1 frame latency).
    }
}

void ChildProcessManager::setOverlayInstanceManager(OverlayInstanceManager *mgr)
{
    m_overlayInstanceMgr = mgr;

    if (!mgr) return;

    // When a new overlay is created, wire its settings changes to sync
    connect(mgr, &OverlayInstanceManager::overlayCreated,
            this, [this](const QString &profileId) {
        if (!m_overlayInstanceMgr) return;

        auto *inst = m_overlayInstanceMgr->instance(profileId);
        if (!inst || !inst->markerSettings()) return;

        // When ANY setting changes, sync child spawn/kill state
        connect(inst->markerSettings(), &MarkerSettingsManager::settingsChanged,
                this, [this, profileId]() {
            syncFeatureToggles(profileId);
        });

        qInfo() << "ChildProcessManager: wired settings sync for" << profileId;
    });
}

int ChildProcessManager::childCount() const
{
    int total = 0;
    for (auto it = m_children.constBegin(); it != m_children.constEnd(); ++it) {
        total += it.value().size();
    }
    return total;
}

QList<ChildProcessInfo> ChildProcessManager::childrenForProfile(
    const QString &profileId) const
{
    return m_children.value(profileId);
}

// ============================================================================
// Private slots
// ============================================================================

void ChildProcessManager::onProfileLaunched(const QString &profileId,
                                            qint64 pid)
{
    // Cache PID — needed later when profileWindowConfirmed fires
    m_profilePids.insert(profileId, pid);
    qInfo() << "ChildProcessManager: cached PID" << pid
            << "for profile:" << profileId;

    // Retroactive spawn: if profileWindowConfirmed already fired but
    // PID wasn't available yet (common for Steam/Epic), spawn now.
    if (m_pendingSpawn.remove(profileId)) {
        qInfo() << "ChildProcessManager: retroactive spawn for profile:"
                 << profileId << "(window confirmed before PID arrived)";
        QString mumbleName = m_launchManager->mumbleLinkNameForProfile(profileId);
        if (mumbleName.isEmpty()) {
            mumbleName = QStringLiteral("MumbleLink");
        }
        spawnChildren(profileId, mumbleName, pid);
    }
}

void ChildProcessManager::onProfileWindowConfirmed(const QString &profileId)
{
    qint64 gw2Pid = m_profilePids.value(profileId, 0);

    // Fallback: profileWindowConfirmed can fire before profileLaunched
    // (race condition in signal ordering). Use ProfileManager's running
    // state tracking as a secondary PID source.
    if (gw2Pid <= 0) {
        gw2Pid = m_profileManager->getProfilePid(profileId);
        if (gw2Pid > 0) {
            m_profilePids.insert(profileId, gw2Pid);
            qInfo() << "ChildProcessManager: PID recovered from ProfileManager:"
                     << gw2Pid << "for profile:" << profileId;
        }
    }

    if (gw2Pid <= 0) {
        // PID not available yet — mark as pending.
        // onProfileLaunched will spawn retroactively when PID arrives.
        m_pendingSpawn.insert(profileId);
        qInfo() << "ChildProcessManager: window confirmed but no PID yet for"
                << profileId << "— queued for retroactive spawn";
        return;
    }

    QString mumbleName = m_launchManager->mumbleLinkNameForProfile(profileId);
    if (mumbleName.isEmpty()) {
        mumbleName = QStringLiteral("MumbleLink");
    }

    spawnChildren(profileId, mumbleName, gw2Pid);
}

void ChildProcessManager::onProfileExited(const QString &profileId)
{
    m_pendingSpawn.remove(profileId);
    terminateChildren(profileId);
}

// ============================================================================
// Hardlink management
// ============================================================================

QString ChildProcessManager::createHardlink(const QString &featureKey,
                                             const QString &profileId)
{
    // Source: the feature-specific child exe in lib/
    QString sourceExe = QDir(m_childrenDir).filePath(
        QString("GW2AIO-%1.exe").arg(featureKey));

    if (!QFileInfo::exists(sourceExe)) {
        qWarning() << "ChildProcessManager: source exe not found:" << sourceExe;
        return QString();
    }

    // Target: GW2AIO-<feature>-<profileId UUID>.exe in same directory
    // Uses full UUID — immune to user profile renames or special characters
    QString targetExe = QDir(m_childrenDir).filePath(
        QString("GW2AIO-%1-%2.exe").arg(featureKey, profileId));

    // Remove existing hardlink if present (stale from previous run)
    if (QFileInfo::exists(targetExe)) {
        QFile::remove(targetExe);
    }

    // Create NTFS hardlink (no admin needed, same volume)
    // Keep wstring temporaries alive (Win32 API rule)
    std::wstring targetW = targetExe.toStdWString();
    std::wstring sourceW = sourceExe.toStdWString();

    if (!CreateHardLinkW(targetW.c_str(), sourceW.c_str(), nullptr)) {
        DWORD err = GetLastError();
        qWarning() << "ChildProcessManager: CreateHardLinkW failed —"
                   << "source:" << sourceExe << "target:" << targetExe
                   << "error:" << err;

        // Fallback: try to use the source exe directly (no custom name)
        return sourceExe;
    }

    qInfo() << "ChildProcessManager: hardlink created:" << targetExe;
    return targetExe;
}



// ============================================================================
// Job Object
// ============================================================================

void ChildProcessManager::ensureJobObject()
{
    if (m_jobObject) {
        return;
    }

    m_jobObject = CreateJobObjectW(nullptr, nullptr);
    if (!m_jobObject) {
        qWarning() << "ChildProcessManager: Failed to create Job Object, error:"
                   << GetLastError();
        return;
    }

    // Configure: kill all children when Job Object is closed
    // (i.e., when grandfather exits — even via crash/TerminateProcess)
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (!SetInformationJobObject(m_jobObject, JobObjectExtendedLimitInformation,
                                  &jeli, sizeof(jeli))) {
        qWarning() << "ChildProcessManager: SetInformationJobObject failed, error:"
                   << GetLastError();
    }

    qInfo() << "ChildProcessManager: Job Object created with KILL_ON_JOB_CLOSE";
}



// ============================================================================
// Pipe polling (reads upstream messages from children)
// ============================================================================

void ChildProcessManager::startPipePolling()
{
    if (m_pipePollingTimer) {
        return; // Already running
    }

    m_pipePollingTimer = new QTimer(this);
    m_pipePollingTimer->setInterval(200); // 5Hz — fast enough for settings sync
    connect(m_pipePollingTimer, &QTimer::timeout,
            this, &ChildProcessManager::pollChildPipes);
    m_pipePollingTimer->start();
    qInfo() << "ChildProcessManager: Pipe polling started (200ms)";
}

void ChildProcessManager::stopPipePolling()
{
    if (m_pipePollingTimer) {
        m_pipePollingTimer->stop();
        delete m_pipePollingTimer;
        m_pipePollingTimer = nullptr;
    }
}

void ChildProcessManager::pollChildPipes()
{
    // Iterate all profiles and their children
    for (auto profileIt = m_children.begin(); profileIt != m_children.end(); ++profileIt) {
        const QString &profileId = profileIt.key();
        for (auto &child : profileIt.value()) {
            if (child.pipeHandle == INVALID_HANDLE_VALUE) {
                continue;
            }

            // Non-blocking check: is there data to read?
            DWORD bytesAvailable = 0;
            BOOL peekOk = PeekNamedPipe(
                child.pipeHandle,
                nullptr,        // No peek buffer
                0,              // Zero bytes
                nullptr,        // Not reading
                &bytesAvailable,
                nullptr);       // Remaining

            if (!peekOk) {
                // Pipe may be broken (child exited) — skip silently
                continue;
            }

            if (bytesAvailable == 0) {
                continue;
            }

            // Read the available data
            QByteArray buffer(static_cast<int>(bytesAvailable), '\0');
            DWORD bytesRead = 0;
            BOOL readOk = ReadFile(
                child.pipeHandle,
                buffer.data(),
                bytesAvailable,
                &bytesRead,
                nullptr);

            if (!readOk || bytesRead == 0) {
                continue;
            }

            buffer.resize(static_cast<int>(bytesRead));
            QString message = QString::fromUtf8(buffer).trimmed();

            // Pipe is PIPE_TYPE_MESSAGE: one ReadFile = one complete message.
            // Do NOT split on \n — the message may contain \n as a delimiter
            // between command keyword and payload (e.g., "SETTING_CHANGED\n{json}").
            processChildMessage(profileId, child.featureKey, message);
        }
    }
}

void ChildProcessManager::processChildMessage(const QString &profileId,
                                               const QString &featureKey,
                                               const QString &message)
{
    if (message.startsWith("SETTING_CHANGED\n") || message.startsWith("SETTING_CHANGED")) {
        // Parse JSON payload
        QString jsonStr = message;
        if (jsonStr.startsWith("SETTING_CHANGED\n")) {
            jsonStr = jsonStr.mid(16); // Skip "SETTING_CHANGED\n"
        } else {
            jsonStr = jsonStr.mid(15); // Skip "SETTING_CHANGED"
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            qWarning() << "ChildProcessManager: Failed to parse SETTING_CHANGED:"
                       << error.errorString() << "from" << featureKey;
            return;
        }

        qInfo() << "ChildProcessManager: SETTING_CHANGED from" << featureKey
                << "for profile" << profileId;

        // Relay settings to ALL siblings of this profile (except the sender)
        QByteArray relay = "SETTINGS\n" + doc.toJson(QJsonDocument::Compact);

        if (m_children.contains(profileId)) {
            for (auto &sibling : m_children[profileId]) {
                if (sibling.featureKey == featureKey) {
                    continue; // Don't echo back to sender
                }
                if (sibling.pipeHandle == INVALID_HANDLE_VALUE) {
                    continue;
                }
                DWORD written = 0;
                WriteFile(sibling.pipeHandle, relay.constData(),
                          static_cast<DWORD>(relay.size()), &written, nullptr);
            }
        }

        // Phase 5.7: Apply settings to parent's MarkerSettingsManager.
        // This triggers the existing settingsChanged → syncFeatureToggles
        // signal chain (wired in setOverlayInstanceManager), enabling
        // in-game overlay toggles to kill/spawn child processes.
        if (m_overlayInstanceMgr) {
            auto *inst = m_overlayInstanceMgr->instance(profileId);
            if (inst && inst->markerSettings()) {
                QJsonObject obj = doc.object();
                // Apply display settings if present (rendering toggles live here)
                if (obj.contains("renderingEnabled") || obj.contains("render3dEnabled") ||
                    obj.contains("renderMinimapEnabled") || obj.contains("renderBigMapEnabled")) {
                    inst->markerSettings()->applyDisplayJson(obj);
                    qInfo() << "ChildProcessManager: Applied display settings from"
                            << featureKey << "to parent MarkerSettingsManager for"
                            << profileId;
                }
            }
        }

    } else if (message.startsWith("READY")) {
        qInfo() << "ChildProcessManager: Child" << featureKey << "READY for"
                << profileId;
        // Spawn queue: child is ready — spawn the next one
        processSpawnQueue();
    } else if (message.startsWith("MAP ")) {
        // Child sends "MAP <mapId>" when entering a new map
        qInfo() << "ChildProcessManager: Child" << featureKey
                << "entered map for" << profileId << ":" << message;
    } else if (message.trimmed() == "RELOAD_PACKS") {
        // Overlay child saved pack/category data to disk — relay to siblings
        qInfo() << "ChildProcessManager: RELOAD_PACKS from" << featureKey
                << "for" << profileId;
        QByteArray relay = "RELOAD_PACKS";
        if (m_children.contains(profileId)) {
            for (auto &sibling : m_children[profileId]) {
                if (sibling.featureKey == featureKey) {
                    continue; // Don't echo back to sender
                }
                if (sibling.pipeHandle == INVALID_HANDLE_VALUE) {
                    continue;
                }
                DWORD written = 0;
                WriteFile(sibling.pipeHandle, relay.constData(),
                          static_cast<DWORD>(relay.size()), &written, nullptr);
            }
        }
    } else if (message.startsWith("RADIAL_TOGGLE")) {
        // Overlay child toggled radial menu on/off
        // The child already saved to disk — just call syncFeatureToggles
        qInfo() << "ChildProcessManager: RADIAL_TOGGLE from" << featureKey
                << "for" << profileId;
        syncFeatureToggles(profileId);

    } else if (message.startsWith("COMPOSITOR_READY")) {
        // Compositor window is up and rendering — log dimensions
        // Format: COMPOSITOR_READY:<width>:<height>
        QStringList parts = message.split(':');
        int w = parts.size() > 1 ? parts[1].toInt() : 0;
        int h = parts.size() > 2 ? parts[2].toInt() : 0;
        qInfo() << "[COMPOSITOR_LIFECYCLE] Compositor READY for" << profileId
                << "— size:" << w << "x" << h;
        // Spawn queue: compositor is ready — spawn the next feature child
        processSpawnQueue();

    } else if (message.startsWith("RESIZE:")) {
        // Compositor resize — relay to ALL feature children of this profile
        QStringList parts = message.split(':');
        int w = parts.size() > 1 ? parts[1].toInt() : 0;
        int h = parts.size() > 2 ? parts[2].toInt() : 0;
        qInfo() << "[COMPOSITOR_LIFECYCLE] RESIZE from compositor —"
                << w << "x" << h << "relaying to features for" << profileId;

        QByteArray relay = (message + "\n").toUtf8();
        if (m_children.contains(profileId)) {
            for (auto &sibling : m_children[profileId]) {
                if (sibling.featureKey == "compositor") continue;
                if (sibling.pipeHandle == INVALID_HANDLE_VALUE) continue;
                DWORD written = 0;
                WriteFile(sibling.pipeHandle, relay.constData(),
                          static_cast<DWORD>(relay.size()), &written, nullptr);
            }
        }

    } else if (message.startsWith("INTERACTIVE_RECTS")) {
        // Feature child reports clickable areas — relay to compositor
        qInfo() << "[COMPOSITOR_LIFECYCLE] INTERACTIVE_RECTS from" << featureKey
                << "for" << profileId << "— relaying to compositor";

        QByteArray relay = (message + "\n").toUtf8();
        if (m_children.contains(profileId)) {
            for (auto &sibling : m_children[profileId]) {
                if (sibling.featureKey != "compositor") continue;
                if (sibling.pipeHandle == INVALID_HANDLE_VALUE) continue;
                DWORD written = 0;
                WriteFile(sibling.pipeHandle, relay.constData(),
                          static_cast<DWORD>(relay.size()), &written, nullptr);
            }
        }

    } else {
        qInfo() << "ChildProcessManager: Unknown message from" << featureKey
                << ":" << message.left(50);
    }
}

// ============================================================================
// Process spawning
// ============================================================================

bool ChildProcessManager::spawnChild(const QString &profileId,
                                     const QString &mumbleName,
                                     qint64 gw2Pid,
                                     const QString &featureKey)
{
    // Get profile name for display (logs, window titles, CLI arg)
    AccountProfile *profile = m_profileManager->profile(profileId);
    QString profileName = profile ? profile->nickname : profileId;

    qInfo() << "ChildProcessManager: spawning child — feature:" << featureKey
            << "profile:" << profileName << "pid:" << gw2Pid;

    // 1. Create NTFS hardlink (uses profileId UUID, not nickname)
    QString exePath = createHardlink(featureKey, profileId);
    if (exePath.isEmpty()) {
        emit childError(profileId, featureKey, "Failed to create hardlink");
        return false;
    }

    // 2. Generate pipe name
    QString pipeName = QString("\\\\.\\pipe\\GW2AIO-%1-%2")
                           .arg(featureKey, profileId.right(8));

    // 3. Defensive cleanup: if a stale pipe exists from a previous session
    //    (e.g., force-kill, crash, or the profileExited bug), connect to
    //    drain the instance so CreateNamedPipeW can succeed.
    {
        std::wstring stalePipeW = pipeName.toStdWString();
        HANDLE hStale = CreateFileW(
            stalePipeW.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hStale != INVALID_HANDLE_VALUE) {
            qInfo() << "ChildProcessManager: cleaned up stale pipe:" << pipeName;
            CloseHandle(hStale);
        }
    }

    // 4. Create pipe server
    std::wstring pipeNameW = pipeName.toStdWString();
    HANDLE hPipe = CreateNamedPipeW(
        pipeNameW.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);

    if (hPipe == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        qWarning() << "ChildProcessManager: pipe creation failed for"
                   << pipeName << "error:" << err;
        // Continue without pipe — child will retry
    }

    // 5. Build command line
    QString cmdLine = QString("\"%1\" --profile %2 --mumble %3 --pid %4 --pipe %5 --profile-name \"%6\"")
        .arg(exePath, profileId, mumbleName,
             QString::number(gw2Pid), pipeName, profileName);

    qInfo() << "ChildProcessManager: cmd:" << cmdLine;

    // 6. Create process (suspended so we can assign to Job Object first)
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::wstring cmdLineW = cmdLine.toStdWString();

    // Use CREATE_SUSPENDED to assign to Job Object before execution starts
    BOOL created = CreateProcessW(
        nullptr,
        const_cast<LPWSTR>(cmdLineW.c_str()),
        nullptr,        // Process attributes
        nullptr,        // Thread attributes
        FALSE,          // Inherit handles
        CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP,
        nullptr,        // Environment
        nullptr,        // Working directory
        &si,
        &pi);

    if (!created) {
        DWORD err = GetLastError();
        qWarning() << "ChildProcessManager: CreateProcessW failed, error:" << err;

        if (hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(hPipe);
        }

        emit childError(profileId, featureKey,
                        QString("CreateProcess failed (error %1)").arg(err));
        return false;
    }

    // 7. Assign to Job Object (before resuming)
    if (m_jobObject) {
        if (!AssignProcessToJobObject(m_jobObject, pi.hProcess)) {
            qWarning() << "ChildProcessManager: AssignProcessToJobObject failed,"
                       << "error:" << GetLastError();
        }
    }

    // 8. Resume the child process
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread); // Thread handle not needed

    // 9. Store child info
    ChildProcessInfo info;
    info.profileId = profileId;
    info.profileName = profileName;
    info.featureKey = featureKey;
    info.exePath = exePath;
    info.processHandle = pi.hProcess;
    info.processId = pi.dwProcessId;
    info.pipeHandle = hPipe;
    info.pipeName = pipeName;

    m_children[profileId].append(info);

    qInfo() << "ChildProcessManager: child spawned — PID:" << pi.dwProcessId
            << "feature:" << featureKey << "profile:" << profileName;

    emit childSpawned(profileId, featureKey);
    return true;
}
