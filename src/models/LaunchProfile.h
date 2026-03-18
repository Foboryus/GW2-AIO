#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

/**
 * @brief Account launch profile for multi-account support
 */
struct LaunchProfile {
  QString id;
  QString name;          // Profile display name
  QStringList arguments; // Additional command-line args
  QDateTime lastUsed;
  int processPriority = 0; // 0=Normal, 1=AboveNormal, 2=High, 3=Realtime
};

/**
 * @brief Available GW2 command-line arguments
 */
namespace LaunchArguments {
// Display
constexpr const char *WINDOWED = "-windowed";
constexpr const char *FULLSCREEN = "-fullscreen";
constexpr const char *FPS = "-fps";

// Audio
constexpr const char *NOSOUND = "-nosound";
constexpr const char *NOMUSIC = "-nomusic";

// Performance/Debug
constexpr const char *MAPLOADINFO = "-maploadinfo";
constexpr const char *BMP = "-bmp"; // Screenshots as BMP
constexpr const char *NOPATCHUI = "-nopatchui";

// Multi-boxing
constexpr const char *SHARE_ARCHIVE = "-shareArchive";

// Login
constexpr const char *AUTOLOGIN = "-autologin";

// Graphics
constexpr const char *DX9 = "-dx9";
constexpr const char *DX11 = "-dx11";

// All available arguments
inline QStringList all() {
  return {WINDOWED, FULLSCREEN, FPS,           NOSOUND,   NOMUSIC, MAPLOADINFO,
          BMP,      NOPATCHUI,  SHARE_ARCHIVE, AUTOLOGIN, DX9,     DX11};
}

inline QString description(const QString &arg) {
  if (arg == WINDOWED)
    return "Launch in windowed mode";
  if (arg == FULLSCREEN)
    return "Launch in fullscreen mode";
  if (arg == FPS)
    return "Show FPS counter";
  if (arg == NOSOUND)
    return "Disable all sound";
  if (arg == NOMUSIC)
    return "Disable music only";
  if (arg == MAPLOADINFO)
    return "Show map loading info";
  if (arg == BMP)
    return "Save screenshots as BMP";
  if (arg == NOPATCHUI)
    return "Skip patcher UI";
  if (arg == SHARE_ARCHIVE)
    return "Required for multi-boxing";
  if (arg == AUTOLOGIN)
    return "Auto-login with saved credentials";
  if (arg == DX9)
    return "Use DirectX 9";
  if (arg == DX11)
    return "Use DirectX 11";
  return "";
}
} // namespace LaunchArguments
