#pragma once

#include <QObject>
#include <QSet>
#include "ProfileManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief Watches for GW2 window creation using SetWinEventHook
 * 
 * Event-based detection - NO TIMERS.
 * When an ArenaNet class window is created, emits gw2WindowDetected with PID.
 */
class GW2WindowWatcher : public QObject
{
    Q_OBJECT

public:
    static GW2WindowWatcher* instance();
    
    /**
     * @brief Start watching for new GW2 windows
     * @param profile The profile to associate with the next detected window
     * @param existingPids PIDs to ignore (already running)
     */
    void watchForGW2(const AccountProfile& profile, const QSet<qint64>& existingPids);
    
    /**
     * @brief Stop watching
     */
    void stopWatching();
    
    /**
     * @brief Check if currently watching
     */
    bool isWatching() const { return m_watching; }

signals:
    /**
     * @brief Emitted when a new GW2 window is detected
     * @param pid Process ID of the new GW2 instance
     * @param profile The associated launch profile
     */
    void gw2WindowDetected(qint64 pid, const AccountProfile& profile);
    
    /**
     * @brief Emitted when watching was cancelled without detecting a window
     * @param profileId The profile ID that was being watched
     */
    void watchCancelled(const QString& profileId);

private:
    explicit GW2WindowWatcher(QObject* parent = nullptr);
    ~GW2WindowWatcher();
    
    static GW2WindowWatcher* s_instance;
    
    bool m_watching = false;
    AccountProfile m_pendingProfile;
    QSet<qint64> m_existingPids;

#ifdef Q_OS_WIN
    static HWINEVENTHOOK s_hook;
    static void CALLBACK WinEventCallback(
        HWINEVENTHOOK hook, DWORD event, HWND hwnd,
        LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
    
    void handleWindowCreated(HWND hwnd);
#endif

private slots:
    void onGW2Detected(qint64 pid);
};
