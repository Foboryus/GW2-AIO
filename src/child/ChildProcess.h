#pragma once

/**
 * @file ChildProcess.h
 * @brief Base class for all AIO child processes
 *
 * Provides the lifecycle foundation for feature-specific child processes
 * (3D overlay, minimap, bigmap, radial, blish). Each child:
 *
 * - Reads MumbleLink directly (shared memory, no IPC needed for game state)
 * - Monitors its GW2 PID — exits when GW2 exits
 * - Connects to the grandfather's named pipe for settings/commands
 * - Only loads data after entering a map (past character selection)
 * - Renders at full rate when focused, idles at 1Hz when unfocused
 *
 * Subclasses override the virtual methods to implement feature-specific
 * rendering and data loading.
 */

// clang-format off
#include <windows.h>
// clang-format on

#include <QObject>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>

class MumbleLink;
class QTimer;

class ChildProcess : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Construct a child process
   * @param profileId Profile UUID
   * @param mumbleName MumbleLink shared memory segment name
   * @param gw2Pid PID of the GW2 instance this child serves
   * @param pipeName Named pipe for grandfather ↔ child IPC
   * @param profileName Human-readable profile nickname (for window title)
   * @param parent QObject parent
   */
  explicit ChildProcess(const QString &profileId,
                        const QString &mumbleName,
                        qint64 gw2Pid,
                        const QString &pipeName,
                        const QString &profileName,
                        QObject *parent = nullptr);
  ~ChildProcess() override;

  // Non-copyable
  ChildProcess(const ChildProcess &) = delete;
  ChildProcess &operator=(const ChildProcess &) = delete;

  /**
   * @brief Start the child process lifecycle
   * Begins MumbleLink polling, GW2 PID monitoring, and pipe connection.
   * @return true if started successfully
   */
  bool start();

  /**
   * @brief Graceful shutdown
   */
  void stop();

  // --- Accessors ---
  const QString &profileId() const { return m_profileId; }
  const QString &profileName() const { return m_profileName; }
  MumbleLink *mumbleLink() const { return m_mumbleLink; }
  qint64 gw2Pid() const { return m_gw2Pid; }
  bool isFocused() const { return m_focused; }
  bool isInGame() const { return m_inGame; }
  uint32_t currentMapId() const { return m_currentMapId; }

signals:
  /**
   * @brief Emitted when the child should exit (GW2 died, STOP received)
   */
  void exitRequested();

protected:
  // --- Virtual lifecycle methods (override in subclasses) ---

  /**
   * @brief Called once when the player enters a map (past character selection)
   * Load map-specific data here (markers, trails, etc.)
   * @param mapId The map the player entered
   */
  virtual void onMapEntered(uint32_t mapId) = 0;

  /**
   * @brief Called when the player leaves a map (enters loading screen or char select)
   * Unload map data here to free memory.
   */
  virtual void onMapLeft() = 0;

  /**
   * @brief Called when the GW2 window gains or loses focus
   * Adjust rendering rate here.
   * @param focused true if GW2 window is now in foreground
   */
  virtual void onFocusChanged(bool focused) = 0;

  /**
   * @brief Called when the grandfather pushes updated settings via pipe
   * Apply marker settings, theme changes, etc.
   * @param settings JSON object with settings data
   */
  virtual void onSettingsReceived(const QJsonObject &settings) = 0;

  /**
   * @brief Called when a feature toggle command arrives via pipe
   * @param key Feature key (e.g., "show_trails", "show_markers")
   * @param enabled New state
   */
  virtual void onFeatureToggle(const QString &key, bool enabled);

  /**
   * @brief Called when overlay child saved pack/category data to disk.
   * Children with MarkerSettingsManager should call loadForProfile()
   * to re-read pack enabled/category states from disk.
   */
  virtual void onReloadPacks() {} // default no-op

  /**
   * @brief Called when a sibling child was terminated for a specific layer.
   * The compositor overrides this to close stale SharedTextureConsumer handles,
   * allowing tryOpenConsumers to reconnect when a replacement child spawns.
   * Default no-op — only meaningful for the compositor.
   * @param layerKey Feature key whose consumer should be reset (e.g., "radial")
   */
  virtual void onLayerReset(const QString &layerKey) { Q_UNUSED(layerKey); }

  /**
   * @brief Called during initialization to allow subclass-specific setup
   * @return true if initialization succeeded
   */
  virtual bool onInitialize() = 0;

  /**
   * @brief Reset the focus-loss debounce timer.
   * Called by the compositor when EVENT_OBJECT_LOCATIONCHANGE fires,
   * proving GW2 is actively being moved/resized. This prevents transient
   * foreground changes during resize from triggering focus loss.
   */
  void resetFocusLossDebounce() {
    if (m_focusLossPending) {
      m_focusLossTimer.restart();
    }
  }

  /**
   * @brief Notify base class of focus change from overlay's WinEvent hook.
   * Subclasses should connect their overlay window's focusChanged signal
   * to this method for instant focus detection (Layer 2).
   * This bypasses the polling-based detection for immediate response.
   * @param focused true if GW2 window gained foreground focus
   */
  void notifyOverlayFocusChanged(bool focused);

  /**
   * @brief Called during shutdown for subclass cleanup
   */
  virtual void onShutdown();

  /**
   * @brief Send a message to the grandfather process via named pipe
   * Used for upstream IPC (e.g., SETTING_CHANGED from overlay child)
   * @param message Complete message to send (should end with \n)
   * @return true if message was sent successfully
   */
  bool sendToGrandfather(const QByteArray &message);

private slots:
  /**
   * @brief Process MumbleLink data updates (called every tick)
   */
  void onMumbleDataUpdated();

  /**
   * @brief Handle map change from MumbleLink
   */
  void onMapChanged(uint32_t mapId);

private:
  // --- GW2 PID monitoring ---
  void startPidMonitor();
  void stopPidMonitor();
  static void CALLBACK onGw2ProcessExited(PVOID context, BOOLEAN timedOut);

  // --- Named pipe IPC ---
  void connectToPipe();
  void processCommand(const QString &command);
  void startPipeReader();
  void pollPipe();

  // --- MumbleLink poll rate management ---
  void setFocusedPollRate();
  void setIdlePollRate();

  // --- Members ---
  QString m_profileId;
  QString m_mumbleName;
  qint64 m_gw2Pid;
  QString m_pipeName;
  QString m_profileName;

  MumbleLink *m_mumbleLink = nullptr;

  // GW2 PID monitoring
  HANDLE m_gw2ProcessHandle = nullptr;
  HANDLE m_waitHandle = nullptr;

  // Named pipe
  HANDLE m_pipeHandle = INVALID_HANDLE_VALUE;
  QTimer *m_pipeReconnectTimer = nullptr;
  QTimer *m_pipeReadTimer = nullptr;  // Polls pipe for incoming messages

  // State tracking
  bool m_focused = false;
  bool m_inGame = false;        // true after entering a map (past char select)
  uint32_t m_currentMapId = 0;
  bool m_started = false;

  // Focus debounce: system processes (wsappx, svchost) briefly steal foreground,
  // causing false focus-loss events. We require the foreground to be consistently
  // non-GW2/non-self for FOCUS_LOSS_DEBOUNCE_MS before accepting focus loss.
  QElapsedTimer m_focusLossTimer;  // Measures continuous unfocused time
  bool m_focusLossPending = false; // True while debounce is counting

  // Diagnostic: periodic status logging
  uint64_t m_tickCount = 0;
  QElapsedTimer m_statusTimer;
  static constexpr qint64 STATUS_LOG_INTERVAL_MS = 30000; // Log every 30s

  // Poll rate constants
  // GW2 writes MumbleLink at ~60Hz — 16ms matches its frame rate exactly.
  // Unfocused: 5s heartbeat is a safety net for focus regain if WinEvent fails.
  // Primary focus detection is Layer 2 (WinEvent) at ~0ms latency.
  static constexpr int FOCUSED_POLL_MS = 16;   // ~62.5Hz when focused (matches GW2 ~60Hz)
  static constexpr int IDLE_POLL_MS = 5000;    // 5s heartbeat when unfocused
  static constexpr qint64 FOCUS_LOSS_DEBOUNCE_MS = 500; // 500ms grace period
};
