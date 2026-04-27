#include "PlatformLaunchDialog.h"
#include "UIHelpers.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QSettings>
#include <QStyle>

#ifdef Q_OS_WIN
#include <tlhelp32.h>
#endif

#ifdef Q_OS_WIN
HWINEVENTHOOK PlatformLaunchDialog::s_hook = nullptr;
PlatformLaunchDialog *PlatformLaunchDialog::s_instance = nullptr;
#endif

PlatformLaunchDialog::PlatformLaunchDialog(Platform platform, QWidget *parent)
    : QDialog(parent), m_platform(platform) {
  // Parentless, always-on-top, proper dialog flags
  setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint |
                 Qt::FramelessWindowHint);
  setAttribute(Qt::WA_DeleteOnClose, false);

  setupUI();
  centerOnPrimaryScreen();
}

PlatformLaunchDialog::~PlatformLaunchDialog() {
  stopWaitingForPlatform();
  if (m_loadingTimer) {
    m_loadingTimer->stop();
  }
}

void PlatformLaunchDialog::setupUI() {
  setMinimumWidth(400);
  setAttribute(Qt::WA_TranslucentBackground); // Required for rounded corners

  // Outer layout for transparent dialog
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  // Background container with gold border + rounded corners
  auto *bgContainer = new QWidget();
  UIHelpers::applyPopupBackgroundRole(bgContainer);
  outerLayout->addWidget(bgContainer);

  auto *layout = new QVBoxLayout(bgContainer);
  layout->setSpacing(20);
  layout->setContentsMargins(30, 30, 30, 30);

  // Styled message container (dark box with rounded borders)
  m_messageContainer = new QWidget(bgContainer);
  UIHelpers::applyContainerRole(m_messageContainer);
  auto *containerLayout = new QVBoxLayout(m_messageContainer);
  containerLayout->setContentsMargins(20, 20, 20, 20);

  // Message label
  m_messageLabel = new QLabel(m_messageContainer);
  m_messageLabel->setText(
      QString("<b>%1 Required</b><br><br>"
              "%1 must be running to launch this profile.<br><br>"
              "Click the button below to open %1.")
          .arg(getPlatformName()));
  m_messageLabel->setWordWrap(true);
  UIHelpers::applyLabelRole(m_messageLabel);
  m_messageLabel->setStyleSheet("background: transparent;");
  m_messageLabel->setAlignment(Qt::AlignCenter);
  containerLayout->addWidget(m_messageLabel);

  layout->addWidget(m_messageContainer);

  // Action button (Open -> Loading -> Continue)
  m_actionButton =
      new QPushButton(QString("Open %1").arg(getPlatformName()), this);
  m_actionButton->setMinimumHeight(50);
  UIHelpers::applyNeutralStyle(m_actionButton);
  connect(m_actionButton, &QPushButton::clicked, this,
          &PlatformLaunchDialog::onOpenClicked);
  layout->addWidget(m_actionButton);

  // Progress bar (hidden initially, shows during loading)
  m_progressBar = new QProgressBar(this);
  m_progressBar->setMinimum(0);
  m_progressBar->setMaximum(100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(false);
  m_progressBar->setMinimumHeight(6);
  m_progressBar->hide();
  layout->addWidget(m_progressBar);

  // Cancel button
  m_cancelButton = new QPushButton("Cancel", this);
  m_cancelButton->setMinimumHeight(40);
  UIHelpers::applyCancelStyle(m_cancelButton);
  connect(m_cancelButton, &QPushButton::clicked, this,
          &PlatformLaunchDialog::onCancelClicked);
  layout->addWidget(m_cancelButton);

  // Loading timer
  m_loadingTimer = new QTimer(this);
  connect(m_loadingTimer, &QTimer::timeout, this,
          &PlatformLaunchDialog::updateLoadingProgress);
}

void PlatformLaunchDialog::centerOnPrimaryScreen() {
  // Ensure size is calculated before positioning
  adjustSize();

  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen) {
    QRect screenGeometry = screen->availableGeometry();
    int x = screenGeometry.x() +
            (screenGeometry.width() - width()) / 2; // Centered horizontally
    // Position at 1/6 from top - higher up to see launchers below
    int y = screenGeometry.y() + screenGeometry.height() / 6;
    move(x, y);
  }
}

void PlatformLaunchDialog::onOpenClicked() {
  QString exePath = getPlatformExePath();

  if (exePath.isEmpty()) {
    m_messageLabel->setText(
        QString("<b>Error</b><br><br>Could not find %1 installation.")
            .arg(getPlatformName()));
    return;
  }

  qInfo() << "Opening" << getPlatformName() << "from:" << exePath;

  // Start the platform
  bool started = QProcess::startDetached(exePath, {});

  if (!started) {
    m_messageLabel->setText(QString("<b>Error</b><br><br>Failed to start %1.")
                                .arg(getPlatformName()));
    return;
  }

  // Start loading animation
  startLoadingAnimation();

  // Start event-based detection
  startWaitingForPlatform();
}

void PlatformLaunchDialog::startLoadingAnimation() {
  m_loading = true;
  m_loadingElapsed.start();

  // Update button to loading state
  m_actionButton->setText(QString("%1 Opening...").arg(getPlatformName()));
  m_actionButton->setEnabled(false);
  m_actionButton->disconnect();

  // Show progress bar
  m_progressBar->setValue(0);
  m_progressBar->show();

  // Disable cancel briefly to prevent accidental clicks
  m_cancelButton->setEnabled(false);
  QTimer::singleShot(500, this, [this]() { m_cancelButton->setEnabled(true); });

  // Start progress animation (update every 50ms)
  m_loadingTimer->start(50);
}

void PlatformLaunchDialog::updateLoadingProgress() {
  if (!m_loading)
    return;

  // If platform not detected yet, animate as indeterminate (pulsing 0-50%)
  if (!m_platformDetected) {
    qint64 elapsed = m_loadingElapsed.elapsed();
    int pulse = static_cast<int>((elapsed / 10) % 100); // 0-100 cycle every 1s
    if (pulse > 50)
      pulse = 100 - pulse; // Triangle wave 0-50-0
    m_progressBar->setValue(pulse);
  } else {
    // Platform detected - now counting down 2 more seconds
    qint64 sinceDetection = m_detectionElapsed.elapsed();
    int progress =
        qMin(100, static_cast<int>((sinceDetection * 100) / POST_DETECTION_MS));
    m_progressBar->setValue(progress);

    // Enable Continue after 2 seconds post-detection
    if (sinceDetection >= POST_DETECTION_MS) {
      enableContinueButton();
    }
  }
}

void PlatformLaunchDialog::enableContinueButton() {
  m_loading = false;
  m_loadingTimer->stop();

  m_progressBar->setValue(100);
  m_progressBar->hide();

  m_actionButton->setText("Continue");
  m_actionButton->setEnabled(true);
  UIHelpers::applyActionStyle(m_actionButton);
  connect(m_actionButton, &QPushButton::clicked, this,
          &PlatformLaunchDialog::onContinueClicked);

  m_platformReady = true;

  // Update message
  m_messageLabel->setText(QString("<b>%1 is Ready</b><br><br>"
                                  "Once %1 is ready, click Continue to launch the game.")
                              .arg(getPlatformName()));
}

void PlatformLaunchDialog::onCancelClicked() {
  stopWaitingForPlatform();
  m_platformReady = false;
  reject();
}

void PlatformLaunchDialog::onContinueClicked() {
  qInfo() << "User clicked Continue for" << getPlatformName();
  stopWaitingForPlatform();
  accept(); // Close dialog and return success
}

void PlatformLaunchDialog::skipToReady() {
  // Platform is already running - immediately enable Continue
  m_platformDetected = true;
  m_platformReady = true;
  enableContinueButton();
}

void PlatformLaunchDialog::startWaitingForPlatform() {
#ifdef Q_OS_WIN
  if (m_waiting)
    return;

  m_waiting = true;
  s_instance = this;

  // Install SetWinEventHook to detect window creation
  s_hook =
      SetWinEventHook(EVENT_OBJECT_CREATE, // eventMin
                      EVENT_OBJECT_CREATE, // eventMax
                      nullptr, // hmodWinEventProc (nullptr for out-of-context)
                      WinEventCallback,     // lpfnWinEventProc
                      0,                    // idProcess (0 = all processes)
                      0,                    // idThread (0 = all threads)
                      WINEVENT_OUTOFCONTEXT // dwFlags
      );

  if (s_hook) {
    qInfo() << "SetWinEventHook installed for" << getPlatformName()
            << "detection";
  } else {
    qWarning() << "Failed to install SetWinEventHook";
  }
#endif
}

void PlatformLaunchDialog::stopWaitingForPlatform() {
#ifdef Q_OS_WIN
  if (!m_waiting)
    return;

  m_waiting = false;

  if (s_hook) {
    UnhookWinEvent(s_hook);
    s_hook = nullptr;
    qInfo() << "SetWinEventHook uninstalled";
  }

  s_instance = nullptr;
#endif
}

void PlatformLaunchDialog::onPlatformDetected() {
  qInfo() << getPlatformName() << "detected! Starting 2s countdown...";

  stopWaitingForPlatform();
  m_platformDetected = true;

  // Start the post-detection countdown
  m_detectionElapsed.start();

  // Update button text to show we're almost ready
  m_actionButton->setText(
      QString("%1 Ready! Please wait...").arg(getPlatformName()));
}

QString PlatformLaunchDialog::getPlatformName() const {
  return m_platform == Platform::Steam ? "Steam" : "Epic Games";
}

QString PlatformLaunchDialog::getTargetProcessName() const {
  return m_platform == Platform::Steam ? "Steam.exe" : "EpicGamesLauncher.exe";
}

QString PlatformLaunchDialog::getPlatformExePath() const {
#ifdef Q_OS_WIN
  if (m_platform == Platform::Steam) {
    // Try registry: HKEY_CURRENT_USER\Software\Valve\Steam\SteamPath
    QSettings steamReg("HKEY_CURRENT_USER\\Software\\Valve\\Steam",
                       QSettings::NativeFormat);
    QString steamPath = steamReg.value("SteamPath").toString();
    if (!steamPath.isEmpty()) {
      QString exePath = steamPath + "/Steam.exe";
      if (QFile::exists(exePath)) {
        return exePath;
      }
    }

    // Fallback: common paths
    QStringList commonPaths = {"C:/Program Files (x86)/Steam/Steam.exe",
                               "C:/Program Files/Steam/Steam.exe"};
    for (const QString &path : commonPaths) {
      if (QFile::exists(path))
        return path;
    }
  } else {
    // Epic Games Launcher
    QSettings epicReg("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Epic "
                      "Games\\EpicGamesLauncher",
                      QSettings::NativeFormat);
    QString appDataPath = epicReg.value("AppDataPath").toString();
    if (!appDataPath.isEmpty()) {
      // Epic stores path differently, construct from it
      QString exePath =
          "C:/Program Files (x86)/Epic "
          "Games/Launcher/Portal/Binaries/Win64/EpicGamesLauncher.exe";
      if (QFile::exists(exePath))
        return exePath;
      exePath = "C:/Program Files (x86)/Epic "
                "Games/Launcher/Portal/Binaries/Win32/EpicGamesLauncher.exe";
      if (QFile::exists(exePath))
        return exePath;
    }

    // Fallback: common paths
    QStringList commonPaths = {
        "C:/Program Files (x86)/Epic "
        "Games/Launcher/Portal/Binaries/Win64/EpicGamesLauncher.exe",
        "C:/Program Files (x86)/Epic "
        "Games/Launcher/Portal/Binaries/Win32/EpicGamesLauncher.exe",
        "C:/Program Files/Epic "
        "Games/Launcher/Portal/Binaries/Win64/EpicGamesLauncher.exe"};
    for (const QString &path : commonPaths) {
      if (QFile::exists(path))
        return path;
    }
  }
#endif
  return QString();
}

#ifdef Q_OS_WIN
void CALLBACK PlatformLaunchDialog::WinEventCallback(
    HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
    DWORD dwEventThread, DWORD dwmsEventTime) {
  Q_UNUSED(hook);
  Q_UNUSED(event);
  Q_UNUSED(idChild);
  Q_UNUSED(dwEventThread);
  Q_UNUSED(dwmsEventTime);

  if (!s_instance || idObject != OBJID_WINDOW || !hwnd)
    return;

  // Get process ID from window
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0)
    return;

  // Get process name
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!hProcess)
    return;

  wchar_t processName[MAX_PATH];
  DWORD size = MAX_PATH;
  bool gotName = QueryFullProcessImageNameW(hProcess, 0, processName, &size);
  CloseHandle(hProcess);

  if (!gotName)
    return;

  QString name = QString::fromWCharArray(processName);
  QString targetName = s_instance->getTargetProcessName();

  // Check if this is the process we're waiting for
  if (name.endsWith(targetName, Qt::CaseInsensitive)) {
    qInfo() << "Detected target process window:" << name;

    // Use QMetaObject to call on main thread
    QMetaObject::invokeMethod(s_instance, "onPlatformDetected",
                              Qt::QueuedConnection);
  }
}
#endif
