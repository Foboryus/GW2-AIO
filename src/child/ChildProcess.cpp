#include "ChildProcess.h"
#include "core/MumbleLink.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

ChildProcess::ChildProcess(const QString &profileId,
                           const QString &mumbleName,
                           qint64 gw2Pid,
                           const QString &pipeName,
                           const QString &profileName,
                           QObject *parent)
    : QObject(parent)
    , m_profileId(profileId)
    , m_mumbleName(mumbleName)
    , m_gw2Pid(gw2Pid)
    , m_pipeName(pipeName)
    , m_profileName(profileName)
{
}

ChildProcess::~ChildProcess()
{
    stop();
}

bool ChildProcess::start()
{
    if (m_started) {
        return true;
    }

    qInfo() << "ChildProcess: Starting for profile" << m_profileName
            << "mumble:" << m_mumbleName
            << "gw2Pid:" << m_gw2Pid;

    // Create MumbleLink reader for the assigned segment
    m_mumbleLink = new MumbleLink(m_mumbleName, this);

    // Connect MumbleLink signals
    // Use dataUpdated for continuous state monitoring (fires every tick)
    connect(m_mumbleLink, &MumbleLink::dataUpdated,
            this, &ChildProcess::onMumbleDataUpdated);
    connect(m_mumbleLink, &MumbleLink::mapChanged,
            this, &ChildProcess::onMapChanged);
    // Handle character select / loading screen transitions.
    // MumbleLink suppresses mapChanged during invalid states (mapType==1,
    // mapId==0, position zero), but DOES emit connectionChanged(false).
    // Without this, m_inGame stays true during character select and
    // children continue rendering stale content.
    connect(m_mumbleLink, &MumbleLink::connectionChanged,
            this, [this](bool connected) {
        if (!connected && m_inGame) {
            qInfo() << "ChildProcess: MumbleLink disconnected"
                    << "(char select / loading) — unloading map for"
                    << m_profileName;
            onMapLeft();
            m_inGame = false;
            m_currentMapId = 0;
        }
    });

    // Start MumbleLink polling at IDLE rate — children start unfocused
    // (m_focused = false). Will switch to focused rate on first focus gain.
    if (!m_mumbleLink->start(IDLE_POLL_MS)) {
        qWarning() << "ChildProcess: Failed to start MumbleLink for segment"
                   << m_mumbleName;
        return false;
    }

    // Start diagnostic timer
    m_statusTimer.start();

    qInfo() << "[DIAG] ChildProcess: STARTED"
            << m_profileName
            << "gw2Pid:" << m_gw2Pid
            << "mumble:" << m_mumbleName
            << "initialPollRate:" << IDLE_POLL_MS << "ms"
            << "focused:" << m_focused
            << "inGame:" << m_inGame;

    // Start monitoring the GW2 process — exit when it dies
    startPidMonitor();

    // Allow subclass to initialize (D3D11 windows, pipelines, etc.)
    if (!onInitialize()) {
        qWarning() << "ChildProcess: Subclass initialization failed";
        stop();
        return false;
    }

    // Connect to grandfather's named pipe for settings/commands
    connectToPipe();

    m_started = true;
    qInfo() << "ChildProcess: Started successfully for" << m_profileName;
    return true;
}

void ChildProcess::stop()
{
    if (!m_started) {
        return;
    }

    qInfo() << "ChildProcess: Stopping for" << m_profileName;

    m_started = false;

    // Allow subclass cleanup first
    onShutdown();

    // Stop MumbleLink
    if (m_mumbleLink) {
        m_mumbleLink->stop();
    }

    // Stop PID monitor
    stopPidMonitor();

    // Close pipe
    if (m_pipeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipeHandle);
        m_pipeHandle = INVALID_HANDLE_VALUE;
    }

    // Stop pipe timers
    if (m_pipeReadTimer) {
        m_pipeReadTimer->stop();
    }
    if (m_pipeReconnectTimer) {
        m_pipeReconnectTimer->stop();
    }

    qInfo() << "ChildProcess: Stopped for" << m_profileName;
}

// --- Virtual defaults ---

void ChildProcess::onFeatureToggle(const QString &key, bool enabled)
{
    Q_UNUSED(key);
    Q_UNUSED(enabled);
    // Default: no-op. Subclasses override if they handle toggles.
}

void ChildProcess::onShutdown()
{
    // Default: no-op. Subclasses override for cleanup.
}

// --- MumbleLink handlers ---

void ChildProcess::onMumbleDataUpdated()
{
    if (!m_mumbleLink) {
        return;
    }

    ++m_tickCount;

    // Periodic status log for diagnostics
    if (m_statusTimer.elapsed() >= STATUS_LOG_INTERVAL_MS) {
        qInfo() << "[DIAG] ChildProcess status:"
                << m_profileName
                << "focused:" << m_focused
                << "inGame:" << m_inGame
                << "ticks:" << m_tickCount
                << "pollRate:" << (m_focused ? FOCUSED_POLL_MS : IDLE_POLL_MS) << "ms"
                << "mapId:" << m_currentMapId;
        m_tickCount = 0;
        m_statusTimer.restart();
    }

    // Detect focus changes via GetForegroundWindow (Layer 1)
    // Focus GAIN: instant (first tick that sees GW2/self in foreground)
    // Focus LOSS: debounced — requires FOCUS_LOSS_DEBOUNCE_MS of continuous
    // non-GW2 foreground. This prevents system processes (wsappx, svchost,
    // Service Host) from causing false focus-loss events when they briefly
    // steal foreground.
    HWND fgWnd = GetForegroundWindow();
    DWORD fgPid = 0;
    if (fgWnd) {
        GetWindowThreadProcessId(fgWnd, &fgPid);
    }
    // Accept focus if foreground belongs to GW2 OR to our own process.
    const DWORD selfPid = GetCurrentProcessId();
    const bool fgIsOurs = (fgPid == static_cast<DWORD>(m_gw2Pid) ||
                           fgPid == selfPid);


    // --- Focus GAIN: instant, no debounce ---
    if (fgIsOurs && !m_focused) {
        m_focused = true;
        m_focusLossPending = false;  // Cancel any pending loss
        setFocusedPollRate();

        qInfo() << "[DIAG] ChildProcess: FOCUS_CHANGED"
                << m_profileName
                << "focused: true"
                << "source: GetForegroundWindow"
                << "pollRate:" << FOCUSED_POLL_MS << "ms"
                << "inGame:" << m_inGame
                << "mapId:" << m_currentMapId;

        onFocusChanged(true);
    }
    // --- Focus LOSS: debounced ---
    else if (!fgIsOurs && m_focused) {
        if (!m_focusLossPending) {
            // Start debounce timer
            m_focusLossPending = true;
            m_focusLossTimer.start();
        } else if (m_focusLossTimer.elapsed() >= FOCUS_LOSS_DEBOUNCE_MS) {
            // Debounce expired — foreground has been consistently non-ours
            m_focused = false;
            m_focusLossPending = false;
            setIdlePollRate();

            qInfo() << "[DIAG] ChildProcess: FOCUS_CHANGED"
                    << m_profileName
                    << "focused: false"
                    << "source: GetForegroundWindow (debounced)"
                    << "pollRate:" << IDLE_POLL_MS << "ms"
                    << "inGame:" << m_inGame
                    << "mapId:" << m_currentMapId;

            onFocusChanged(false);
        }
        // else: still within debounce window — ignore transient loss
    }
    // --- Foreground returned during debounce: cancel pending loss ---
    else if (fgIsOurs && m_focusLossPending) {
        m_focusLossPending = false;
    }
}

void ChildProcess::notifyOverlayFocusChanged(bool focused)
{
    // Layer 2: Focus update from overlay's WinEvent hook.
    // Focus GAIN: instant (0ms latency)
    // Focus LOSS: debounced — system processes (wsappx, svchost) briefly
    // steal foreground, triggering WinEvent FOREGROUND_LOST. We use the
    // same debounce timer as Layer 1 to absorb these transients.

    if (focused) {
        // --- Instant focus gain ---
        m_focusLossPending = false;  // Cancel any pending loss
        if (m_focused) {
            return;  // Already focused — no-op
        }
        m_focused = true;
        setFocusedPollRate();

        qInfo() << "[DIAG] ChildProcess: FOCUS_CHANGED"
                << m_profileName
                << "focused: true"
                << "source: OverlayWinEvent"
                << "pollRate:" << FOCUSED_POLL_MS << "ms"
                << "inGame:" << m_inGame
                << "mapId:" << m_currentMapId;

        onFocusChanged(true);
    } else {
        // --- Debounced focus loss ---
        if (!m_focused) {
            return;  // Already unfocused — no-op
        }
        if (!m_focusLossPending) {
            m_focusLossPending = true;
            m_focusLossTimer.start();
            // Don't fire yet — wait for debounce.
            // Layer 1 polling will check the timer on subsequent ticks.
        }
        // The actual focus=false transition is handled by Layer 1's
        // debounce check in onMumbleDataUpdated(). This ensures a single
        // code path for focus loss, preventing race conditions.
    }
}

void ChildProcess::onMapChanged(uint32_t mapId)
{
    qInfo() << "ChildProcess: Map changed to" << mapId
            << "for" << m_profileName;

    // Character selection check: mapType == 1 means character select
    if (m_mumbleLink && m_mumbleLink->isCharacterSelect()) {
        // In character selection — unload if we were in-game
        if (m_inGame) {
            qInfo() << "ChildProcess: Entered character select, unloading map data";
            onMapLeft();
            m_inGame = false;
            m_currentMapId = 0;
        }
        return;
    }

    // Entering a real map (mapId > 0 and not character select)
    if (mapId > 0) {
        // If we were on a different map, notify leave first
        if (m_inGame && m_currentMapId != mapId) {
            onMapLeft();
        }

        m_currentMapId = mapId;
        m_inGame = true;
        onMapEntered(mapId);

        // Fix focus/visibility race: children start at 5s idle poll rate,
        // so onMumbleDataUpdated() may not have detected focus yet. If GW2
        // is already foreground when we enter a map, trigger focus gain NOW
        // instead of waiting up to 5 seconds for the next poll tick.
        if (!m_focused) {
            HWND fgWnd = GetForegroundWindow();
            DWORD fgPid = 0;
            if (fgWnd) {
                GetWindowThreadProcessId(fgWnd, &fgPid);
            }
            const DWORD selfPid = GetCurrentProcessId();
            if (fgPid == static_cast<DWORD>(m_gw2Pid) || fgPid == selfPid) {
                m_focused = true;
                m_focusLossPending = false;
                setFocusedPollRate();
                qInfo() << "[DIAG] ChildProcess: FOCUS_CHANGED"
                        << m_profileName
                        << "focused: true"
                        << "source: onMapChanged (instant fix)"
                        << "inGame:" << m_inGame
                        << "mapId:" << m_currentMapId;
                onFocusChanged(true);
            }
        }

        // Notify grandfather via pipe that we entered a map
        if (m_pipeHandle != INVALID_HANDLE_VALUE) {
            QString msg = QString("MAP %1\n").arg(mapId);
            QByteArray data = msg.toUtf8();
            DWORD written = 0;
            WriteFile(m_pipeHandle, data.constData(),
                      static_cast<DWORD>(data.size()), &written, nullptr);
        }
    }
}

// --- GW2 PID monitoring ---

void ChildProcess::startPidMonitor()
{
    if (m_gw2Pid <= 0) {
        qWarning() << "ChildProcess: Invalid GW2 PID, cannot monitor";
        return;
    }

    m_gw2ProcessHandle = OpenProcess(SYNCHRONIZE, FALSE,
                                      static_cast<DWORD>(m_gw2Pid));
    if (!m_gw2ProcessHandle) {
        qWarning() << "ChildProcess: Failed to open GW2 process (PID:"
                   << m_gw2Pid << ") error:" << GetLastError();
        // GW2 may have already exited
        QMetaObject::invokeMethod(this, [this]() {
            emit exitRequested();
        }, Qt::QueuedConnection);
        return;
    }

    // Register async callback when GW2 exits
    if (!RegisterWaitForSingleObject(
            &m_waitHandle,
            m_gw2ProcessHandle,
            &ChildProcess::onGw2ProcessExited,
            this,
            INFINITE,
            WT_EXECUTEONLYONCE)) {
        qWarning() << "ChildProcess: RegisterWaitForSingleObject failed, error:"
                   << GetLastError();
        CloseHandle(m_gw2ProcessHandle);
        m_gw2ProcessHandle = nullptr;
    }
}

void ChildProcess::stopPidMonitor()
{
    if (m_waitHandle) {
        UnregisterWait(m_waitHandle);
        m_waitHandle = nullptr;
    }

    if (m_gw2ProcessHandle) {
        CloseHandle(m_gw2ProcessHandle);
        m_gw2ProcessHandle = nullptr;
    }
}

void CALLBACK ChildProcess::onGw2ProcessExited(PVOID context, BOOLEAN timedOut)
{
    Q_UNUSED(timedOut);

    auto *self = static_cast<ChildProcess *>(context);

    qInfo() << "ChildProcess: GW2 process exited for" << self->m_profileName;

    // Marshal to main thread safely
    QMetaObject::invokeMethod(self, [self]() {
        self->stop();
        emit self->exitRequested();
    }, Qt::QueuedConnection);
}

// --- Named pipe IPC ---

void ChildProcess::connectToPipe()
{
    if (m_pipeName.isEmpty()) {
        qInfo() << "ChildProcess: No pipe name specified, running without IPC";
        return;
    }

    // Keep wstring alive (Win32 API rule)
    std::wstring pipeNameW = m_pipeName.toStdWString();

    m_pipeHandle = CreateFileW(
        pipeNameW.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        qWarning() << "ChildProcess: Failed to connect to pipe"
                   << m_pipeName << "error:" << err;

        // Retry connection periodically (grandfather may not have created pipe yet)
        if (!m_pipeReconnectTimer) {
            m_pipeReconnectTimer = new QTimer(this);
            m_pipeReconnectTimer->setInterval(1000);
            connect(m_pipeReconnectTimer, &QTimer::timeout,
                    this, &ChildProcess::connectToPipe);
        }
        m_pipeReconnectTimer->start();
        return;
    }

    // Connected — stop retry timer
    if (m_pipeReconnectTimer) {
        m_pipeReconnectTimer->stop();
    }

    qInfo() << "ChildProcess: Connected to pipe" << m_pipeName;

    // Set pipe to message mode for reading
    DWORD pipeMode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(m_pipeHandle, &pipeMode, nullptr, nullptr);

    // Send READY to request initial settings
    const char *readyMsg = "READY\n";
    DWORD written = 0;
    WriteFile(m_pipeHandle, readyMsg,
              static_cast<DWORD>(strlen(readyMsg)), &written, nullptr);

    // Start polling for incoming commands from grandfather
    startPipeReader();
}

void ChildProcess::startPipeReader()
{
    if (m_pipeReadTimer) {
        return; // Already running
    }

    m_pipeReadTimer = new QTimer(this);
    m_pipeReadTimer->setInterval(100); // 10Hz — responsive settings sync
    connect(m_pipeReadTimer, &QTimer::timeout, this, &ChildProcess::pollPipe);
    m_pipeReadTimer->start();
    qInfo() << "ChildProcess: Pipe reader started (100ms)";
}

void ChildProcess::pollPipe()
{
    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    // Non-blocking check: is there data to read?
    DWORD bytesAvailable = 0;
    BOOL peekOk = PeekNamedPipe(
        m_pipeHandle,
        nullptr, 0, nullptr,
        &bytesAvailable, nullptr);

    if (!peekOk) {
        // Pipe broken (grandfather exited?) — stop polling
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
            qWarning() << "ChildProcess: Pipe broken — stopping reader";
            m_pipeReadTimer->stop();
            CloseHandle(m_pipeHandle);
            m_pipeHandle = INVALID_HANDLE_VALUE;
        }
        return;
    }

    if (bytesAvailable == 0) {
        return;
    }

    // Read the available data
    QByteArray buffer(static_cast<int>(bytesAvailable), '\0');
    DWORD bytesRead = 0;
    BOOL readOk = ReadFile(
        m_pipeHandle, buffer.data(),
        bytesAvailable, &bytesRead, nullptr);

    if (!readOk || bytesRead == 0) {
        return;
    }

    buffer.resize(static_cast<int>(bytesRead));
    QString data = QString::fromUtf8(buffer);

    // Process each command (pipe message mode: one message per ReadFile)
    processCommand(data);
}

bool ChildProcess::sendToGrandfather(const QByteArray &message)
{
    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        qWarning() << "ChildProcess: Cannot send to grandfather — pipe not connected";
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(m_pipeHandle, message.constData(),
                        static_cast<DWORD>(message.size()), &written, nullptr);

    if (!ok) {
        DWORD err = GetLastError();
        qWarning() << "ChildProcess: WriteFile to grandfather failed, error:" << err;
        return false;
    }

    return true;
}

void ChildProcess::processCommand(const QString &command)
{
    if (command.startsWith("SETTINGS\n")) {
        // Parse JSON payload after the command keyword
        QString jsonStr = command.mid(9); // Skip "SETTINGS\n"
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            onSettingsReceived(doc.object());
        } else {
            qWarning() << "ChildProcess: Failed to parse SETTINGS JSON:"
                       << error.errorString();
        }
    } else if (command.startsWith("THEME\n")) {
        QString jsonStr = command.mid(6); // Skip "THEME\n"
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            // Theme updates go through settings
            QJsonObject settings;
            settings["theme"] = doc.object();
            onSettingsReceived(settings);
        }
    } else if (command.startsWith("FOCUS ")) {
        bool focused = command.mid(6).trimmed() == "1";
        m_focused = focused;
        if (m_focused) {
            setFocusedPollRate();
        } else {
            setIdlePollRate();
        }
        onFocusChanged(m_focused);
    } else if (command.startsWith("TOGGLE ")) {
        // Format: "TOGGLE key 1" or "TOGGLE key 0"
        QStringList parts = command.mid(7).split(' ');
        if (parts.size() >= 2) {
            QString key = parts[0];
            bool enabled = parts[1].trimmed() == "1";
            onFeatureToggle(key, enabled);
        }
    } else if (command.trimmed() == "RELOAD_PACKS") {
        qInfo() << "ChildProcess: Received RELOAD_PACKS — reloading pack data";
        onReloadPacks();
    } else if (command.startsWith("LAYER_RESET ")) {
        // Grandfather tells compositor to close a stale consumer for a layer
        // whose child was terminated (so tryOpenConsumers can reopen it).
        QString layerKey = command.mid(12).trimmed();
        onLayerReset(layerKey);
    } else if (command.trimmed() == "STOP") {
        qInfo() << "ChildProcess: Received STOP command";
        stop();
        emit exitRequested();
    } else {
        qWarning() << "ChildProcess: Unknown command:" << command.left(50);
    }
}

// --- Poll rate management ---

void ChildProcess::setFocusedPollRate()
{
    if (m_mumbleLink && m_mumbleLink->isRunning()) {
        m_mumbleLink->setUpdateInterval(FOCUSED_POLL_MS);
    }
}

void ChildProcess::setIdlePollRate()
{
    if (m_mumbleLink && m_mumbleLink->isRunning()) {
        m_mumbleLink->setUpdateInterval(IDLE_POLL_MS);
    }
}
