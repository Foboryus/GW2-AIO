#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QProgressBar>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief Parentless always-on-top dialog shown when Steam/Epic is not running
 * 
 * Features:
 * - Always on top of all windows
 * - Centered on primary monitor
 * - Loading bar button animation
 * - Detection-based Continue enable (min 2 seconds)
 */
class PlatformLaunchDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Platform {
        Steam,
        Epic
    };

    explicit PlatformLaunchDialog(Platform platform, QWidget* parent = nullptr);
    ~PlatformLaunchDialog();

    /**
     * @brief Get the result - true if platform is now running
     */
    bool platformReady() const { return m_platformReady; }
    
    /**
     * @brief Skip detection wait and show Continue button immediately
     * Call this when platform is already running before exec()
     */
    void skipToReady();

private slots:
    void onOpenClicked();
    void onCancelClicked();
    void onPlatformDetected();
    void onContinueClicked();
    void updateLoadingProgress();

private:
    void setupUI();
    void centerOnPrimaryScreen();
    void startWaitingForPlatform();
    void stopWaitingForPlatform();
    void startLoadingAnimation();
    void enableContinueButton();
    
    QString getPlatformName() const;
    QString getPlatformExePath() const;
    QString getTargetProcessName() const;

    Platform m_platform;
    bool m_platformReady = false;
    bool m_platformDetected = false;
    bool m_waiting = false;
    bool m_loading = false;

    QWidget* m_messageContainer = nullptr;
    QLabel* m_messageLabel = nullptr;
    QPushButton* m_actionButton = nullptr;  // Open -> Loading -> Continue
    QPushButton* m_cancelButton = nullptr;
    QProgressBar* m_progressBar = nullptr;  // Overlay on button
    
    QTimer* m_loadingTimer = nullptr;
    QElapsedTimer m_loadingElapsed;
    QElapsedTimer m_detectionElapsed;  // Tracks time since platform was detected
    static constexpr int POST_DETECTION_MS = 2000;  // 2 seconds after detection

#ifdef Q_OS_WIN
    static HWINEVENTHOOK s_hook;
    static PlatformLaunchDialog* s_instance;
    static void CALLBACK WinEventCallback(
        HWINEVENTHOOK hook, DWORD event, HWND hwnd,
        LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
#endif
};
