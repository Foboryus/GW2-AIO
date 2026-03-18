#pragma once

/**
 * @brief Local.dat Manager — Per-profile AppData via directory junctions
 *
 * Manages per-profile AppData folders for GW2. Each profile gets its own
 * folder containing Local.dat (credentials), GFXSettings, InputBinds, etc.
 * Before launching a profile, a directory junction redirects
 * %APPDATA%/Guild Wars 2/ to the profile's folder.
 *
 * This eliminates file locking conflicts during multiboxing because each
 * GW2 instance locks files in its own isolated folder.
 *
 * See: features/local-dat-management.md
 *
 * DO NOT ADD:
 * - Inline implementations (use LocalDatManager.cpp)
 * - Profile management logic (belongs in ProfileManager)
 */

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#endif 

struct LocalDatFile {
   QString path;  // Full path to the saved local.dat
  QString name;     // Display name (account nickname)
  QString gw2Build; // GW2 build version when created
  QString md5Hash;  // MD5 hash for validation
  bool valid = false;
  
 QString getName() const { return QFileInfo(path).baseName(); }
  
 QString calculateHash() const {
     QFile file(path);
     if (!file.open(QIODevice::ReadOnly)) {
       return QString();
      
    }
     QCryptographicHash hash(QCryptographicHash::Md5);
     hash.addData(&file);
     return hash.result().toHex();
    
  }
  
};

 class LocalDatManager : public QObject {
 Q_OBJECT
 public : explicit LocalDatManager(QObject *parent = nullptr);
   explicit LocalDatManager(
      const QString &profileDataDir, QObject *parent = nullptr);
  

      // === Junction-based Profile Activation ===

      /// Activate a profile: junction %APPDATA%/Guild Wars 2 → profile folder
      /// Returns true if junction was created successfully.
      bool activateProfile(const QString &profileId);
  

      /// Deactivate: remove junction, restore original AppData folder
      bool deactivateProfile();
  

      /// Check if a junction is currently active
      bool isJunctionActive() const;
  

      // === Profile Folder Management ===

      /// Ensure a profile folder exists with at least a Local.dat
      /// Creates the folder if needed; migrates from old flat file if present
      void ensureProfileFolder(const QString &profileId);
  

      /// Get the profile folder path (ProfileData/{profileId}/)
      QString profileFolderPath(const QString &profileId) const;
  

      /// Get the Local.dat path within a profile folder
      QString profileLocalDatPath(const QString &profileId) const;
  

      // === Save/Load Credentials ===

      /// Copy current GW2 Local.dat to the profile folder
      LocalDatFile saveCurrentForAccount(const QString &profileId);
  

      /// List all profiles that have saved Local.dat files
      QList<LocalDatFile> listSavedFiles() const;
  

      /// Delete a profile's saved data folder
      bool deleteProfileFolder(const QString &profileId);
  

      // === Lifecycle ===

      /// Cleanup stale state on AIO startup (restore junction if crashed)
      void cleanupOnStartup();
  

      /// Cleanup on AIO exit (restore junction if still active)
      void cleanupOnExit();
  

      // === Migration ===

      /// Migrate from old flat LocalDats/{uuid}.dat to
      /// ProfileData/{uuid}/Local.dat
      void migrateFromFlatFiles(const QString &oldSavedDatsDir);
  

      // === Accessors ===
 QString gw2AppDataPath() const {
    return m_gw2AppDataPath;
  }
   QString defaultFolderPath() const;
  
 signals : void activated(const QString &profileId);
   void deactivated();
   void error(const QString &message);

 private : QString m_profileDataDir; // ProfileData/ directory
  QString m_gw2AppDataPath;               // %APPDATA%/Guild Wars 2/
  QString m_defaultFolderPath;            // ProfileData/_default/
  bool m_junctionActive = false;
  

      // === Junction Operations (Windows) ===
      bool
      createJunction(const QString &junctionPath, const QString &targetPath);
   bool removeJunction(const QString &junctionPath);
   bool isJunction(const QString &path) const;
  

      /// Initialize internal paths
      void ensurePaths();
  

      /// Save the original AppData folder as _default
      bool preserveOriginalAppData();
  

      /// Restore _default back to the real AppData path
      bool restoreOriginalAppData();
  
};

