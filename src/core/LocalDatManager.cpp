/**
 * @file LocalDatManager.cpp
 * @brief Per-profile AppData management via directory junctions
 *
 * Each profile gets its own folder under ProfileData/{profileId}/
 * containing Local.dat, GFXSettings, InputBinds, etc.
 *
 * Before launching a profile, the real %APPDATA%/Guild Wars 2/ folder
 * is renamed to ProfileData/_default/ and a directory junction is
 * created at %APPDATA%/Guild Wars 2 → ProfileData/{profileId}/.
 *
 * After launch, GW2 has file handles into the profile folder.
 * The junction can then be swapped to the next profile or restored.
 *
 * See: features/local-dat-management.md
 */

#include "LocalDatManager.h"
#include "AtomicFileWriter.h"
#include <QProcess>

#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>

#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <winioctl.h>
// clang-format on
#endif

// =========================================================================
// CONSTRUCTORS
// =========================================================================

LocalDatManager::LocalDatManager(QObject *parent) : QObject(parent) {
  QString dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  m_profileDataDir = dataDir + "/ProfileData/";
  QDir().mkpath(m_profileDataDir);
  ensurePaths();
}

LocalDatManager::LocalDatManager(const QString &profileDataDir, QObject *parent)
    : QObject(parent) {
  m_profileDataDir = profileDataDir;
  if (!m_profileDataDir.endsWith('/')) {
    m_profileDataDir += '/';
  }
  QDir().mkpath(m_profileDataDir);
  ensurePaths();
}

void LocalDatManager::ensurePaths() {

  // GW2 always uses %APPDATA%/Guild Wars 2/ regardless of install path
  m_gw2AppDataPath = QDir::homePath() + "/AppData/Roaming/Guild Wars 2";
  m_defaultFolderPath = m_profileDataDir + "_default";
}

// =========================================================================
// JUNCTION-BASED PROFILE ACTIVATION
// =========================================================================
bool LocalDatManager::activateProfile(const QString &profileId) {
  qInfo() << "=== LocalDatManager::activateProfile() ===";
  qInfo() << "Profile:" << profileId;

  // Ensure the profile has a folder
  ensureProfileFolder(profileId);

  QString targetFolder = profileFolderPath(profileId);
  if (!QDir(targetFolder).exists()) {
    QString msg = "Profile folder does not exist: " + targetFolder;
    qWarning() << msg;
    emit error(msg);
    return false;
  }

  // Step 1: If a junction is already active, remove it first
  if (isJunction(m_gw2AppDataPath)) {
    qInfo() << "Removing existing junction at" << m_gw2AppDataPath;
    if (!removeJunction(m_gw2AppDataPath)) {
      QString msg = "Failed to remove existing junction";
      qWarning() << msg;
      emit error(msg);
      return false;
    }
  }

  // Step 2: If the real AppData folder exists (not a junction), preserve
  // it
  QFileInfo appDataInfo(m_gw2AppDataPath);
  if (appDataInfo.exists() && appDataInfo.isDir() &&
      !isJunction(m_gw2AppDataPath)) {
    if (!preserveOriginalAppData()) {
      return false;
    }
  }

  // Step 2b: Populate profile folder with missing files from _default.
  // The profile folder may only have Local.dat — GW2 needs other files
  // (GFXSettings, etc.) to avoid "Unable to open archive file".
  if (QDir(m_defaultFolderPath).exists()) {
    QDir defaultDir(m_defaultFolderPath);
    for (const QFileInfo &fi :
         defaultDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
      QString destFile = targetFolder + "/" + fi.fileName();
      if (!QFile::exists(destFile)) {
        QFile::copy(fi.absoluteFilePath(), destFile);
        qInfo() << "Copied missing file to profile:" << fi.fileName();
      }
    }
  }

  // Step 3: Create junction
  if (!createJunction(m_gw2AppDataPath, targetFolder)) {
    QString msg = "Failed to create junction: " + m_gw2AppDataPath + " -> " +
                  targetFolder;
    qWarning() << msg;

    // Try to restore the original folder
    restoreOriginalAppData();
    emit error(msg);
    return false;
  }

  m_junctionActive = true;
  qInfo() << "Junction created:" << m_gw2AppDataPath << "->" << targetFolder;
  emit activated(profileId);
  return true;
}

bool LocalDatManager::deactivateProfile() {
  qInfo() << "=== LocalDatManager::deactivateProfile() ===";

  // Step 1: Remove junction if active
  if (isJunction(m_gw2AppDataPath)) {
    if (!removeJunction(m_gw2AppDataPath)) {
      QString msg = "Failed to remove junction at " + m_gw2AppDataPath;
      qWarning() << msg;
      emit error(msg);
      return false;
    }
    qInfo() << "Junction removed";
  }

  // Step 2: Restore the original AppData folder from _default
  if (QDir(m_defaultFolderPath).exists()) {
    if (!restoreOriginalAppData()) {
      return false;
    }
  }

  m_junctionActive = false;
  qInfo() << "Profile deactivated, original AppData restored";
  emit deactivated();
  return true;
}

bool LocalDatManager::isJunctionActive() const {
  return m_junctionActive || isJunction(m_gw2AppDataPath);
}

// =========================================================================
// PROFILE FOLDER MANAGEMENT
// =========================================================================
void LocalDatManager::ensureProfileFolder(const QString &profileId) {
  QString folderPath = profileFolderPath(profileId);
  QDir().mkpath(folderPath);

  // NOTE: Do NOT create an empty Local.dat here.
  // A 0-byte Local.dat causes GW2 to crash with "Unable to open archive file".
  // GW2 creates its own valid Local.dat when it runs (for any launch type:
  // standalone, Steam, Epic). For standalone profiles, users save credentials
  // via "Save Current Login" which copies a valid 28MB file.
}

QString LocalDatManager::profileFolderPath(const QString &profileId) const {
  return m_profileDataDir + profileId;
}

QString LocalDatManager::profileLocalDatPath(const QString &profileId) const {
  return profileFolderPath(profileId) + "/Local.dat";
}

QString LocalDatManager::defaultFolderPath() const {
  return m_defaultFolderPath;
}

// =========================================================================
// SAVE/LOAD CREDENTIALS
// =========================================================================
LocalDatFile LocalDatManager::saveCurrentForAccount(const QString &profileId) {
  LocalDatFile result;

  // Source: the currently active GW2 Local.dat
  // If a junction is active, this reads from the junction target
  // If no junction, this reads from the real AppData folder
  QString sourcePath = m_gw2AppDataPath + "/Local.dat";

  if (!QFile::exists(sourcePath)) {
    qWarning() << "No Local.dat found at" << sourcePath;
    return result;
  }

  QFileInfo sourceInfo(sourcePath);
  if (sourceInfo.size() == 0) {
    qWarning() << "Local.dat is empty (0 bytes) — no credentials to save";
    return result;
  }

  // Destination: profile folder
  ensureProfileFolder(profileId);
  QString destPath = profileLocalDatPath(profileId);

  // Use atomic copy for safety
  if (!AtomicFileWriter::copyBinary(sourcePath, destPath)) {
    qWarning() << "Failed to copy Local.dat to profile folder";
    return result;
  }

  result.path = destPath;
  result.name = profileId;
  result.valid = true;
  result.md5Hash = result.calculateHash();

  qInfo() << "Saved current login to profile folder:" << destPath;
  qInfo() << "File size:" << QFileInfo(destPath).size() << "bytes";

  return result;
}

QList<LocalDatFile> LocalDatManager::listSavedFiles() const {
  QList<LocalDatFile> files;
  QDir profileDataDir(m_profileDataDir);

  for (const QFileInfo &dirInfo :
       profileDataDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {

    // Skip the _default folder
    if (dirInfo.fileName() == "_default") {
      continue;
    }

    QString localDat = dirInfo.absoluteFilePath() + "/Local.dat";
    if (QFile::exists(localDat) && QFileInfo(localDat).size() > 0) {
      LocalDatFile file;
      file.path = localDat;
      file.name = dirInfo.fileName();
      file.valid = true;
      files.append(file);
    }
  }

  return files;
}

bool LocalDatManager::deleteProfileFolder(const QString &profileId) {
  QString folderPath = profileFolderPath(profileId);
  QDir folder(folderPath);

  if (!folder.exists()) {
    return true; // Already gone
  }

  bool ok = folder.removeRecursively();
  if (ok) {
    qInfo() << "Deleted profile folder:" << folderPath;

  } else {
    qWarning() << "Failed to delete profile folder:" << folderPath;
  }
  return ok;
}

// =========================================================================
// LIFECYCLE
// =========================================================================
void LocalDatManager::cleanupOnStartup() {
  qInfo() << "=== LocalDatManager::cleanupOnStartup() ===";

  // If a stale junction exists from a crash, clean it up
  if (isJunction(m_gw2AppDataPath)) {
    qWarning() << "Stale junction detected at" << m_gw2AppDataPath
               << "- cleaning up from previous crash";
    removeJunction(m_gw2AppDataPath);
  }

  // If _default exists (meaning original was preserved), restore it
  if (QDir(m_defaultFolderPath).exists()) {
    qInfo() << "Restoring original AppData from _default";
    restoreOriginalAppData();
  }

  // Clean up stale Local.dat symlinks from the old symlink approach.
  // Without this, Save Current Login is blocked by the symlink check.
  QString localDatPath = m_gw2AppDataPath + "/Local.dat";
  QFileInfo localDatInfo(localDatPath);
  if (localDatInfo.exists() && localDatInfo.isSymLink()) {
    qWarning() << "Stale Local.dat symlink detected - resolving to real file";
    QString symlinkTarget = localDatInfo.symLinkTarget();
    QFile::remove(localDatPath);
    if (!symlinkTarget.isEmpty() && QFile::exists(symlinkTarget)) {
      QFile::copy(symlinkTarget, localDatPath);
      qInfo() << "Replaced symlink with real copy from" << symlinkTarget;
    } else {
      qInfo() << "Symlink removed (target was missing)";
    }
  }

  // Clean up 0-byte Local.dat files in profile data folders.
  // These were incorrectly created by the old ensureProfileFolder() and
  // cause GW2 to crash with "Unable to open archive file".
  QDir profileDir(m_profileDataDir);
  if (profileDir.exists()) {
    for (const auto &entry :
         profileDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      QString localDatPath = m_profileDataDir + entry + "/Local.dat";
      QFileInfo fi(localDatPath);
      if (fi.exists() && fi.size() == 0) {
        QFile::remove(localDatPath);
        qInfo() << "Removed 0-byte Local.dat from profile folder:" << entry;
      }
    }
  }

  m_junctionActive = false;
  qInfo() << "Startup cleanup complete";
}

void LocalDatManager::cleanupOnExit() {
  qInfo() << "=== LocalDatManager::cleanupOnExit() ===";

  if (isJunction(m_gw2AppDataPath)) {
    qInfo() << "Junction still active on exit — cleaning up";
    removeJunction(m_gw2AppDataPath);
  }

  if (QDir(m_defaultFolderPath).exists()) {
    qInfo() << "Restoring original AppData on exit";
    restoreOriginalAppData();
  }

  m_junctionActive = false;
}

// =========================================================================
// MIGRATION
// =========================================================================
void LocalDatManager::migrateFromFlatFiles(const QString &oldSavedDatsDir) {
  qInfo() << "=== LocalDatManager::migrateFromFlatFiles() ===";
  qInfo() << "Old dir:" << oldSavedDatsDir;

  QDir oldDir(oldSavedDatsDir);
  if (!oldDir.exists()) {
    qInfo() << "No old SavedDats directory found — nothing to migrate";
    return;
  }

  // Find all {uuid}.dat files (skip backups and _original_local.dat)
  QStringList datFiles = oldDir.entryList({"*.dat"}, QDir::Files);
  int migrated = 0;

  for (const QString &fileName : datFiles) {

    // Skip backup and sentinel files
    if (fileName.startsWith("_") || fileName.endsWith(".bak")) {
      continue;
    }

    // Extract profile ID from filename: {uuid}.dat → {uuid}
    QString profileId = QFileInfo(fileName).baseName();
    QString oldPath = oldDir.filePath(fileName);
    QString newFolder = profileFolderPath(profileId);
    QString newPath = newFolder + "/Local.dat";

    // Skip if already migrated
    if (QFile::exists(newPath)) {
      qInfo() << "Already migrated:" << profileId;
      continue;
    }

    // Create profile folder and copy
    QDir().mkpath(newFolder);
    if (AtomicFileWriter::copyBinary(oldPath, newPath)) {
      qInfo() << "Migrated:" << oldPath << "->" << newPath;
      migrated++;

      // Remove old file to prevent re-migration if ProfileData is later deleted
      QFile::remove(oldPath);

    } else {
      qWarning() << "Failed to migrate:" << oldPath;
    }
  }

  // Migrate _original_local.dat → _default/Local.dat
  QString originalBackup = oldDir.filePath("_original_local.dat");
  if (QFile::exists(originalBackup)) {
    QDir().mkpath(m_defaultFolderPath);
    QString defaultLocalDat = m_defaultFolderPath + "/Local.dat";
    if (!QFile::exists(defaultLocalDat)) {
      if (QFile::copy(originalBackup, defaultLocalDat)) {
        qInfo() << "Migrated _original_local.dat -> _default/Local.dat";
      }
    }
  }

  qInfo() << "Migration complete:" << migrated << "files migrated";
}

// =========================================================================
// PRESERVE / RESTORE ORIGINAL APPDATA
// =========================================================================
bool LocalDatManager::preserveOriginalAppData() {
  qInfo() << "Preserving original AppData folder";

  // If _default already exists, check if the real AppData folder also
  // still exists. If so, _default may be incomplete (e.g., migration only
  // copied Local.dat). Merge missing files, then remove the real folder.
  if (QDir(m_defaultFolderPath).exists()) {
    QFileInfo realFolder(m_gw2AppDataPath);
    if (realFolder.exists() && realFolder.isDir() &&
        !isJunction(m_gw2AppDataPath)) {
      qInfo() << "_default exists but real AppData folder also exists"
              << "- merging missing files into _default";
      QDir source(m_gw2AppDataPath);
      for (const QFileInfo &fi :
           source.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        QString dest = m_defaultFolderPath + "/" + fi.fileName();
        if (!QFile::exists(dest)) {
          if (QFile::copy(fi.absoluteFilePath(), dest)) {
            qInfo() << "Merged into _default:" << fi.fileName();
          } else {
            qWarning() << "Failed to merge" << fi.fileName() << "into _default";
          }
        }
      }
      // Remove the real folder so junction can be created in its place
      QDir(m_gw2AppDataPath).removeRecursively();
      qInfo() << "Removed real AppData folder after merging into _default";
    } else {
      qInfo() << "_default already exists - original already preserved";
    }
    return true;
  }

  // Rename the real folder to _default
  QDir parentDir(QFileInfo(m_gw2AppDataPath).absolutePath());
  QString realName = QFileInfo(m_gw2AppDataPath).fileName();

  if (!parentDir.rename(realName, m_defaultFolderPath)) {

    // Fallback: copy all files to _default
    QDir().mkpath(m_defaultFolderPath);
    QDir source(m_gw2AppDataPath);
    bool allCopied = true;
    for (const QFileInfo &fi :
         source.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
      QString dest = m_defaultFolderPath + "/" + fi.fileName();
      if (!QFile::copy(fi.absoluteFilePath(), dest)) {
        qWarning() << "Failed to copy" << fi.fileName() << "to _default";
        allCopied = false;
      }
    }
    if (!allCopied) {
      QString msg = "Failed to fully preserve original AppData";
      qWarning() << msg;
      emit error(msg);
      return false;
    }

    // Verify the copy contains at least as many files as the original
    // before destroying the original (guards against partial copy from
    // disk-full or permission errors that didn't fail QFile::copy)
    int sourceCount = QDir(m_gw2AppDataPath)
                          .entryList(QDir::Files | QDir::NoDotAndDotDot)
                          .size();
    int destCount = QDir(m_defaultFolderPath)
                        .entryList(QDir::Files | QDir::NoDotAndDotDot)
                        .size();
    if (destCount < sourceCount) {
      QString msg = QString("Copy verification failed: _default has %1 files "
                            "but original has %2")
                        .arg(destCount)
                        .arg(sourceCount);
      qWarning() << msg;
      emit error(msg);
      return false;
    }

    // Remove original folder after verified copy
    QDir(m_gw2AppDataPath).removeRecursively();
    qInfo() << "Preserved via copy (rename failed, verified"
            << destCount << "files)";

  } else {
    qInfo() << "Renamed" << m_gw2AppDataPath << "to _default";
  }

  return true;
}

bool LocalDatManager::restoreOriginalAppData() {
  qInfo() << "Restoring original AppData folder from _default";

  // Safety check: if AppData path exists as junction, remove it first
  if (isJunction(m_gw2AppDataPath)) {
    removeJunction(m_gw2AppDataPath);
  }

  // If AppData path still exists as a real folder, something is wrong
  if (QDir(m_gw2AppDataPath).exists() && !isJunction(m_gw2AppDataPath)) {
    qWarning() << "AppData folder already exists and is not a junction";

    // Remove _default since we don't need it
    QDir(m_defaultFolderPath).removeRecursively();
    return true;
  }

  // Rename _default back to the real path
  QDir parentDir(QFileInfo(m_defaultFolderPath).absolutePath());
  if (!parentDir.rename("_default", m_gw2AppDataPath)) {

    // Fallback: copy files back
    QDir().mkpath(m_gw2AppDataPath);
    QDir source(m_defaultFolderPath);
    for (const QFileInfo &fi :
         source.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
      QString dest = m_gw2AppDataPath + "/" + fi.fileName();
      QFile::copy(fi.absoluteFilePath(), dest);
    }
    QDir(m_defaultFolderPath).removeRecursively();
    qInfo() << "Restored via copy (rename failed)";

  } else {
    qInfo() << "Renamed _default back to" << m_gw2AppDataPath;
  }

  return true;
}

// =========================================================================
// DIRECTORY JUNCTION OPERATIONS (Windows)
// =========================================================================

#ifdef Q_OS_WIN
bool LocalDatManager::createJunction(const QString &junctionPath,
                                     const QString &targetPath) {

  // Use cmd mklink /J - proven to work with GW2.
  // Custom Win32 FSCTL_SET_REPARSE_POINT produced junctions that GW2
  // could not read (Unable to open archive file error).
  QString nativeJunction = QDir::toNativeSeparators(junctionPath);
  QString nativeTarget = QDir::toNativeSeparators(targetPath);

  // Build the command string manually - QProcess::setArguments()
  // double-quotes paths with spaces, breaking cmd.exe built-in commands.
  QString cmdLine =
      QString("mklink /J \"%1\" \"%2\"").arg(nativeJunction, nativeTarget);

  QProcess proc;
  proc.setProgram("cmd.exe");
  proc.setNativeArguments("/c " + cmdLine);
  proc.start();
  proc.waitForFinished(5000);

  if (proc.exitCode() != 0) {
    qWarning() << "mklink /J failed:" << proc.readAllStandardError();
    return false;
  }

  qInfo() << "Directory junction created:" << junctionPath << "->"
          << targetPath;
  return true;
}

bool LocalDatManager::removeJunction(const QString &junctionPath) {
  QString nativePath = QDir::toNativeSeparators(junctionPath);

  // Use cmd rmdir to remove junction (rmdir only removes the reparse
  // point, not the target directory contents)
  QString cmdLine = QString("rmdir \"%1\"").arg(nativePath);

  QProcess proc;
  proc.setProgram("cmd.exe");
  proc.setNativeArguments("/c " + cmdLine);
  proc.start();
  proc.waitForFinished(5000);

  if (proc.exitCode() != 0) {
    qWarning() << "rmdir junction failed:" << proc.readAllStandardError();
    return false;
  }

  qInfo() << "Junction removed:" << junctionPath;
  return true;
}
bool LocalDatManager::isJunction(const QString &path) const {
  QString nativePath = QDir::toNativeSeparators(path);
  DWORD attrs =
      GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativePath.utf16()));

  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }

  return (attrs & FILE_ATTRIBUTE_DIRECTORY) &&
         (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
}

#else

// Non-Windows stubs
bool LocalDatManager::createJunction(const QString &, const QString &) {
  qWarning() << "Directory junctions are only supported on Windows";
  return false;
}

bool LocalDatManager::removeJunction(const QString &) { return true; }

bool LocalDatManager::isJunction(const QString &) const { return false; }

#endif
