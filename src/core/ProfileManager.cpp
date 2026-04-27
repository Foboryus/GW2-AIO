/**
 * @file ProfileManager.cpp
 * @brief Implementation of ProfileManager class
 *
 * Per-profile file storage architecture:
 * - Each profile stored in its own {uuid}.json file
 * - Manifest.json tracks order and default profile
 * - Atomic save pattern prevents corruption
 * - Auto-recovery from .bak files
 *
 * DO NOT add:
 * - UI code (belongs in ui/ folder)
 * - Launch logic (belongs in LaunchManager)
 * - Network/API code (belongs in respective managers)
 */

#include "ProfileManager.h"
#include "AppConfig.h"
#include "AtomicFileWriter.h"
#include "CefManager.h"
#include <QDebug>
#include <QDirIterator>

ProfileManager::ProfileManager(QObject *parent) : QObject(parent) {
  // Use AppConfig for cross-platform + portable mode support
  QString dataDir = AppConfig::instance().dataDir();
  m_profilesDir = QDir(dataDir).filePath("profiles");
  m_manifestPath = QDir(m_profilesDir).filePath("manifest.json");
  m_runningProfilesPath = QDir(dataDir).filePath("running_profiles.json");

  // Ensure profiles directory exists
  QDir().mkpath(m_profilesDir);

  load();
  loadRunningProfiles();
  validateRunningProfiles(); // Check for stale PIDs on startup
}

ProfileManager::ProfileManager(const QString &profilesDir, QObject *parent)
    : QObject(parent) {
  m_profilesDir = profilesDir;
  m_manifestPath = QDir(m_profilesDir).filePath("manifest.json");
  // running_profiles.json lives in the parent dir (the data dir)
  QDir parentDir(m_profilesDir);
  parentDir.cdUp();
  m_runningProfilesPath = parentDir.filePath("running_profiles.json");

  // Ensure profiles directory exists
  QDir().mkpath(m_profilesDir);

  load();
  loadRunningProfiles();
  validateRunningProfiles();
}

ProfileManager::ProfileManager(const QString &profilesDir,
                               const QString &savedDatsDir,
                               const QString &savedGfxDir,
                               const QString &savedHotkeysDir,
                               const QString &markerStateDir, QObject *parent)
    : QObject(parent), m_savedDatsDir(savedDatsDir), m_savedGfxDir(savedGfxDir),
      m_savedHotkeysDir(savedHotkeysDir), m_markerStateDir(markerStateDir) {
  m_profilesDir = profilesDir;
  m_manifestPath = QDir(m_profilesDir).filePath("manifest.json");
  QDir parentDir(m_profilesDir);
  parentDir.cdUp();
  m_runningProfilesPath = parentDir.filePath("running_profiles.json");

  QDir().mkpath(m_profilesDir);

  load();
  loadRunningProfiles();
  validateRunningProfiles();
}

// === Path Conversion Helpers ===

QString ProfileManager::toRelativePath(const QString &absolutePath,
                                       const QString &baseDir) const {
  if (absolutePath.isEmpty() || baseDir.isEmpty()) {
    return absolutePath;
  }

  // Normalize both paths for comparison
  QString normPath = QDir::cleanPath(absolutePath);
  QString normBase = QDir::cleanPath(baseDir);

  // Ensure base ends with /
  if (!normBase.endsWith('/')) {
    normBase += '/';
  }

  // If the path is under the base dir, return just the filename
  if (normPath.startsWith(normBase, Qt::CaseInsensitive)) {
    return normPath.mid(normBase.length());
  }

  // Path is external — keep it absolute
  return absolutePath;
}

QString ProfileManager::toAbsolutePath(const QString &relativePath,
                                       const QString &baseDir) const {
  if (relativePath.isEmpty() || baseDir.isEmpty()) {
    return relativePath;
  }

  // Already absolute? Leave it alone
  if (QDir::isAbsolutePath(relativePath)) {
    return relativePath;
  }

  // Relative — resolve against base dir
  return QDir(baseDir).filePath(relativePath);
}

// === Profile File Path Helpers ===

QString ProfileManager::profileFilePath(const QString &id) const {
  return QDir(m_profilesDir).filePath(id + ".json");
}

QString ProfileManager::generateId() const {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool ProfileManager::saveProfileToFile(const AccountProfile &profile) {
  QString filePath = profileFilePath(profile.id);

  // Convert paths to relative for portability
  QJsonObject json = profile.toJson();
  json["localDatPath"] = toRelativePath(profile.localDatPath, m_savedDatsDir);
  json["gfxSettingsPath"] =
      toRelativePath(profile.gfxSettingsPath, m_savedGfxDir);
  // customGw2Path stays absolute — it's an external path

  if (AtomicFileWriter::writeJson(filePath, json)) {
    qInfo() << "Saved profile:" << profile.nickname << "to" << filePath;
    // Save hotkeys to separate file
    saveHotkeys(profile);
    return true;
  }
  return false;
}

bool ProfileManager::loadProfileFromFile(const QString &id,
                                         AccountProfile &out) {
  QString filePath = profileFilePath(id);
  QString backupPath = filePath + ".bak";

  QFile file(filePath);
  bool loadedFromBackup = false;

  if (!file.open(QIODevice::ReadOnly)) {
    // Try backup
    if (QFile::exists(backupPath)) {
      file.setFileName(backupPath);
      if (file.open(QIODevice::ReadOnly)) {
        loadedFromBackup = true;
        qWarning() << "Loading profile" << id << "from backup";
      }
    }
    if (!file.isOpen()) {
      qWarning() << "Profile file not found:" << id;
      return false;
    }
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    // Try backup if main file was corrupt
    if (!loadedFromBackup && QFile::exists(backupPath)) {
      QFile backupFile(backupPath);
      if (backupFile.open(QIODevice::ReadOnly)) {
        doc = QJsonDocument::fromJson(backupFile.readAll(), &parseError);
        backupFile.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
          loadedFromBackup = true;
          // Restore backup
          QFile::remove(filePath);
          QFile::copy(backupPath, filePath);
        }
      }
    }
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
      qWarning() << "Failed to parse profile:" << id;
      return false;
    }
  }

  out = AccountProfile::fromJson(doc.object());

  // Resolve relative paths back to absolute
  out.localDatPath = toAbsolutePath(out.localDatPath, m_savedDatsDir);
  out.gfxSettingsPath = toAbsolutePath(out.gfxSettingsPath, m_savedGfxDir);

  // Load hotkeys from separate file (overrides any backward-compat values)
  loadHotkeys(out);

  // === Auto-fix stale gfxSettingsPath ===
  // If profile has a stale path (doesn't match current nickname), fix it
  if (!out.nickname.trimmed().isEmpty()) {
    QString gfxDir = m_savedGfxDir.isEmpty()
                         ? QStandardPaths::writableLocation(
                               QStandardPaths::AppDataLocation) +
                               "/GFXSettings/"
                         : m_savedGfxDir;
    // Ensure trailing separator
    if (!gfxDir.endsWith('/')) {
      gfxDir += '/';
    }
    QString expectedPath = gfxDir + out.id + "_GFX.xml";

    // Check if current path is stale (wrong filename or empty)
    bool pathIsStale = false;
    if (out.gfxSettingsPath.isEmpty()) {
      // Empty path - check if a matching GFX file exists
      if (QFile::exists(expectedPath)) {
        pathIsStale = true;
      }
    } else if (!QFile::exists(out.gfxSettingsPath) &&
               QFile::exists(expectedPath)) {
      // Path doesn't exist but expected one does
      pathIsStale = true;
    }

    if (pathIsStale) {
      qInfo() << "ProfileManager: Auto-fixing stale gfxSettingsPath for"
              << out.nickname;
      qInfo() << "  Old path:" << out.gfxSettingsPath;
      qInfo() << "  New path:" << expectedPath;
      out.gfxSettingsPath = expectedPath;
      // Save the fix immediately
      saveProfileToFile(out);
    }
  }

  if (loadedFromBackup) {
    emit profilesRestoredFromBackup();
  }

  return true;
}

bool ProfileManager::saveManifest() {
  QJsonObject root;
  root["version"] = 1;
  root["defaultProfileId"] = m_defaultProfileId;

  QJsonArray orderArray;
  for (const auto &p : m_profiles) {
    orderArray.append(p.id);
  }
  root["profileOrder"] = orderArray;

  if (AtomicFileWriter::writeJson(m_manifestPath, root)) {
    qInfo() << "Saved manifest with" << m_profiles.size() << "profiles";
    return true;
  }
  return false;
}

bool ProfileManager::loadManifest() {
  QString backupPath = m_manifestPath + ".bak";
  bool loadedFromBackup = false;
  bool restoredAny = false;

  QFile file(m_manifestPath);

  if (!file.open(QIODevice::ReadOnly)) {
    // Try backup
    if (QFile::exists(backupPath)) {
      file.setFileName(backupPath);
      if (file.open(QIODevice::ReadOnly)) {
        loadedFromBackup = true;
      }
    }
    if (!file.isOpen()) {
      // No manifest - try to rebuild from profile files
      qWarning() << "No manifest found, scanning for profile files...";
      return false; // Will trigger rebuild
    }
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    if (!loadedFromBackup && QFile::exists(backupPath)) {
      QFile backupFile(backupPath);
      if (backupFile.open(QIODevice::ReadOnly)) {
        doc = QJsonDocument::fromJson(backupFile.readAll(), &parseError);
        backupFile.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
          loadedFromBackup = true;
          QFile::remove(m_manifestPath);
          QFile::copy(backupPath, m_manifestPath);
          restoredAny = true;
        }
      }
    }
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
      qWarning() << "Manifest corrupted, will rebuild";
      return false;
    }
  }

  QJsonObject root = doc.object();
  m_defaultProfileId = root["defaultProfileId"].toString();

  QJsonArray orderArray = root["profileOrder"].toArray();
  m_profileOrder.clear();
  for (const auto &item : orderArray) {
    m_profileOrder.append(item.toString());
  }

  if (loadedFromBackup || restoredAny) {
    qWarning() << "Manifest restored from backup";
  }

  return true;
}

// === Main load/save ===

bool ProfileManager::load() {
  m_profiles.clear();
  bool manifestLoaded = loadManifest();
  bool anyRestored = false;

  if (manifestLoaded && !m_profileOrder.isEmpty()) {
    // Load profiles in manifest order
    QStringList validOrder;
    for (const QString &id : m_profileOrder) {
      AccountProfile p;
      if (loadProfileFromFile(id, p)) {
        m_profiles.append(p);
        validOrder.append(id);
      } else {
        qWarning() << "Profile in manifest but file missing:" << id;
      }
    }
    m_profileOrder = validOrder;
  } else {
    // No manifest or empty - scan for .json files
    qInfo() << "Rebuilding manifest from profile files...";
    QDirIterator it(m_profilesDir, {"*.json"}, QDir::Files);
    while (it.hasNext()) {
      it.next();
      QString filename = it.fileName();
      if (filename == "manifest.json")
        continue;

      QString id = filename.chopped(5); // Remove .json
      AccountProfile p;
      if (loadProfileFromFile(id, p)) {
        m_profiles.append(p);
        m_profileOrder.append(id);
      }
    }

    // Save rebuilt manifest
    if (!m_profiles.isEmpty()) {
      if (m_defaultProfileId.isEmpty() ||
          !m_profileOrder.contains(m_defaultProfileId)) {
        m_defaultProfileId = m_profiles.first().id;
      }
      saveManifest();
    }
  }

  // No profiles yet - that's fine for fresh install, launcher will be empty

  // Validate default profile ID (only if we have profiles)
  if (!m_profiles.isEmpty() && (m_defaultProfileId.isEmpty() ||
                                !m_profileOrder.contains(m_defaultProfileId))) {
    m_defaultProfileId = m_profiles.first().id;
    saveManifest();
  }

  emit profilesChanged();
  return true;
}

bool ProfileManager::save() {
  // For backward compatibility, save() just saves the manifest
  // Individual profiles are saved on update
  return saveManifest();
}

// === Profile CRUD ===

AccountProfile *ProfileManager::profile(const QString &id) {
  for (int i = 0; i < m_profiles.size(); i++) {
    if (m_profiles[i].id == id) {
      return &m_profiles[i];
    }
  }
  return nullptr;
}

AccountProfile *ProfileManager::profileByNickname(const QString &nickname) {
  for (int i = 0; i < m_profiles.size(); i++) {
    if (m_profiles[i].nickname == nickname) {
      return &m_profiles[i];
    }
  }
  return nullptr;
}

QString ProfileManager::addProfile(const QString &nickname) {
  AccountProfile p;
  p.id = generateId();
  p.nickname = nickname;
  p.arguments << "-maploadinfo";

  // Phase 7: every profile gets a unique persistent mumble link name
  p.mumbleLinkName = QStringLiteral("GW2Mumble_%1").arg(p.id.left(8));
  p.useCustomMumble = true;
  qInfo() << "[DEV] ProfileManager: new profile mumbleLinkName:"
          << p.mumbleLinkName << "for:" << p.id;

  m_profiles.append(p);
  m_profileOrder.append(p.id);

  if (m_profiles.size() == 1) {
    m_defaultProfileId = p.id;
  }

  saveProfileToFile(p);
  saveManifest();

  emit profileAdded(p.id);
  emit profilesChanged();

  return p.id;
}

void ProfileManager::addCompleteProfile(const AccountProfile &profile) {
  m_profiles.append(profile);
  m_profileOrder.append(profile.id);

  if (m_profiles.size() == 1) {
    m_defaultProfileId = profile.id;
  }

  saveProfileToFile(profile);
  saveManifest();

  emit profileAdded(profile.id);
  emit profilesChanged();
}

bool ProfileManager::removeProfile(const QString &id) {
  for (int i = 0; i < m_profiles.size(); i++) {
    if (m_profiles[i].id == id) {
      m_profiles.removeAt(i);
      m_profileOrder.removeAll(id);

      // Delete profile files
      QString filePath = profileFilePath(id);
      QFile::remove(filePath);
      QFile::remove(filePath + ".bak");
      QFile::remove(filePath + ".tmp");

      // Delete hotkey file
      if (!m_savedHotkeysDir.isEmpty()) {
        QString hotkeyPath =
            QDir(m_savedHotkeysDir).filePath(id + "_hotkeys.json");
        QFile::remove(hotkeyPath);
        QFile::remove(hotkeyPath + ".bak");
        QFile::remove(hotkeyPath + ".tmp");
      }

      // Delete saved Local.dat
      if (!m_savedDatsDir.isEmpty()) {
        QString datPath = QDir(m_savedDatsDir).filePath(id + ".dat");
        QFile::remove(datPath);
      }

      // Delete saved GFX settings
      if (!m_savedGfxDir.isEmpty()) {
        QString gfxPath = QDir(m_savedGfxDir).filePath(id + "_GFX.xml");
        QFile::remove(gfxPath);
      }

      if (m_defaultProfileId == id && !m_profiles.isEmpty()) {
        m_defaultProfileId = m_profiles.first().id;
      }

      saveManifest();
      emit profileRemoved(id);
      emit profilesChanged();
      return true;
    }
  }
  return false;
}

bool ProfileManager::updateProfile(const AccountProfile &profile) {
  for (int i = 0; i < m_profiles.size(); i++) {
    if (m_profiles[i].id == profile.id) {
      m_profiles[i] = profile;
      saveProfileToFile(profile); // Only save this profile file
      emit profileUpdated(profile.id);
      return true;
    }
  }
  return false;
}

bool ProfileManager::moveProfile(int fromIndex, int toIndex) {
  if (fromIndex < 0 || fromIndex >= m_profiles.size())
    return false;
  if (toIndex < 0 || toIndex >= m_profiles.size())
    return false;
  if (fromIndex == toIndex)
    return true;

  m_profiles.move(fromIndex, toIndex);
  m_profileOrder.move(fromIndex, toIndex);
  saveManifest(); // Only manifest changes, not profile files

  emit profilesChanged();
  return true;
}

void ProfileManager::setDefaultProfileId(const QString &id) {
  if (m_defaultProfileId != id && profile(id)) {
    m_defaultProfileId = id;
    saveManifest();
  }
}

AccountProfile *ProfileManager::defaultProfile() {
  return profile(m_defaultProfileId);
}

// === Import/Export ===

bool ProfileManager::exportProfile(const QString &id, const QString &filePath) {
  AccountProfile *p = profile(id);
  if (!p) {
    qWarning() << "Cannot export - profile not found:" << id;
    return false;
  }

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "Cannot export - failed to open file:" << filePath;
    return false;
  }

  QJsonObject exportObj = p->toJson();

  // Embed referenced data files as base64
  if (!p->localDatPath.isEmpty() && QFile::exists(p->localDatPath)) {
    QFile datFile(p->localDatPath);
    if (datFile.open(QIODevice::ReadOnly)) {
      exportObj["_embedded_localDat"] =
          QString::fromLatin1(datFile.readAll().toBase64());
      exportObj["_embedded_localDat_name"] =
          QFileInfo(p->localDatPath).fileName();
      datFile.close();
      qInfo() << "Embedded Local.dat in export:"
              << QFileInfo(p->localDatPath).fileName();
    }
  }

  if (!p->gfxSettingsPath.isEmpty() && QFile::exists(p->gfxSettingsPath)) {
    QFile gfxFile(p->gfxSettingsPath);
    if (gfxFile.open(QIODevice::ReadOnly)) {
      exportObj["_embedded_gfxSettings"] =
          QString::fromLatin1(gfxFile.readAll().toBase64());
      exportObj["_embedded_gfxSettings_name"] =
          QFileInfo(p->gfxSettingsPath).fileName();
      gfxFile.close();
      qInfo() << "Embedded GFX settings in export:"
              << QFileInfo(p->gfxSettingsPath).fileName();
    }
  }

  // Embed hotkey settings
  if (!p->hotkeyFocus.isEmpty() || !p->hotkeyMinimize.isEmpty()) {
    QJsonObject hotkeyObj;
    hotkeyObj["hotkeyFocus"] = p->hotkeyFocus;
    hotkeyObj["hotkeyMinimize"] = p->hotkeyMinimize;
    exportObj["_embedded_hotkeys"] = hotkeyObj;
  }

  // Embed marker state (exclusion zones, display settings, per-pack overrides)
  if (!m_markerStateDir.isEmpty()) {
    QString stateDir = QDir(m_markerStateDir).filePath(id);
    QDir dir(stateDir);
    if (dir.exists()) {
      QJsonObject markerState;
      QStringList jsonFiles = dir.entryList({"*.json"}, QDir::Files);
      for (const QString &fileName : jsonFiles) {
        QFile f(dir.filePath(fileName));
        if (f.open(QIODevice::ReadOnly)) {
          QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
          f.close();
          if (doc.isObject()) {
            markerState[fileName] = doc.object();
          }
        }
      }
      if (!markerState.isEmpty()) {
        exportObj["_embedded_markerState"] = markerState;
        qInfo() << "Embedded marker state in export:"
                << markerState.keys().size() << "files";
      }
    }
  }

  file.write(QJsonDocument(exportObj).toJson());
  file.close();

  qInfo() << "Exported profile:" << p->nickname << "to" << filePath;
  return true;
}

bool ProfileManager::importProfile(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Cannot import - failed to open file:" << filePath;
    return false;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    qWarning() << "Cannot import - invalid JSON:" << parseError.errorString();
    return false;
  }

  QJsonObject importObj = doc.object();
  AccountProfile imported = AccountProfile::fromJson(importObj);

  // Generate new ID to avoid conflicts
  imported.id = generateId();

  // Rename if nickname already exists
  QString baseName = imported.nickname;
  int counter = 1;
  while (profileByNickname(imported.nickname)) {
    imported.nickname = baseName + " (" + QString::number(counter++) + ")";
  }

  // Extract embedded Local.dat if present
  if (importObj.contains("_embedded_localDat") && !m_savedDatsDir.isEmpty()) {
    QByteArray datData = QByteArray::fromBase64(
        importObj["_embedded_localDat"].toString().toLatin1());
    if (!datData.isEmpty()) {
      QDir().mkpath(m_savedDatsDir);
      QString destPath = QDir(m_savedDatsDir).filePath(imported.id + ".dat");
      QFile destFile(destPath);
      if (destFile.open(QIODevice::WriteOnly)) {
        destFile.write(datData);
        destFile.close();
        imported.localDatPath = destPath;
        qInfo() << "Extracted Local.dat to:" << destPath;
      }
    }
  }

  // Extract embedded GFX settings if present
  if (importObj.contains("_embedded_gfxSettings") && !m_savedGfxDir.isEmpty()) {
    QByteArray gfxData = QByteArray::fromBase64(
        importObj["_embedded_gfxSettings"].toString().toLatin1());
    if (!gfxData.isEmpty()) {
      QDir().mkpath(m_savedGfxDir);
      QString destPath = QDir(m_savedGfxDir).filePath(imported.id + "_GFX.xml");
      QFile destFile(destPath);
      if (destFile.open(QIODevice::WriteOnly)) {
        destFile.write(gfxData);
        destFile.close();
        imported.gfxSettingsPath = destPath;
        qInfo() << "Extracted GFX settings to:" << destPath;
      }
    }
  }

  // Extract embedded hotkeys if present
  if (importObj.contains("_embedded_hotkeys")) {
    QJsonObject hotkeyObj = importObj["_embedded_hotkeys"].toObject();
    imported.hotkeyFocus = hotkeyObj["hotkeyFocus"].toString();
    imported.hotkeyMinimize = hotkeyObj["hotkeyMinimize"].toString();
  }

  // Extract embedded marker state if present
  if (importObj.contains("_embedded_markerState") &&
      !m_markerStateDir.isEmpty()) {
    QJsonObject markerState = importObj["_embedded_markerState"].toObject();
    if (!markerState.isEmpty()) {
      QString stateDir = QDir(m_markerStateDir).filePath(imported.id);
      QDir().mkpath(stateDir);
      for (auto it = markerState.constBegin(); it != markerState.constEnd();
           ++it) {
        QString filePath = QDir(stateDir).filePath(it.key());
        QFile f(filePath);
        if (f.open(QIODevice::WriteOnly)) {
          f.write(QJsonDocument(it.value().toObject()).toJson());
          f.close();
        }
      }
      qInfo() << "Extracted marker state:" << markerState.keys().size()
              << "files to" << stateDir;
    }
  }

  addCompleteProfile(imported);

  qInfo() << "Imported profile:" << imported.nickname;
  return true;
}

bool ProfileManager::exportAllProfiles(const QString &folderPath) {
  QDir().mkpath(folderPath);
  bool allSuccess = true;

  for (const auto &p : m_profiles) {
    QString filePath = QDir(folderPath).filePath(p.nickname + ".json");
    if (!exportProfile(p.id, filePath)) {
      allSuccess = false;
    }
  }

  return allSuccess;
}

// === Running Profile Tracking Implementation ===

bool ProfileManager::isProfileRunning(const QString &id) {
  if (!m_runningProfiles.contains(id))
    return false;

  qint64 pid = m_runningProfiles[id];
  if (isProcessAlive(pid)) {
    return true;
  }

  // Process is dead - auto-clean the stale entry
  qInfo() << "Profile" << id << "PID" << pid << "is no longer alive - clearing";
  m_runningProfiles.remove(id);
  saveRunningProfiles();

  // Trigger CEF orphan detection (ProfileManager trigger)
  CefManager::instance().registerExitSignal(pid,
                                            CefTriggerSource::ProfileManager);

  // IMPORTANT: Defer signal emission to avoid re-entrancy crash
  QTimer::singleShot(
      0, this, [this, id]() { emit profileRunningStateChanged(id, false); });

  return false;
}

qint64 ProfileManager::getProfilePid(const QString &id) const {
  return m_runningProfiles.value(id, 0);
}

void ProfileManager::setProfileRunning(const QString &id, qint64 pid,
                                       const QString &mumbleLinkName) {
  if (pid <= 0)
    return;
  m_runningProfiles[id] = pid;
  if (!mumbleLinkName.isEmpty()) {
    m_runningMumbleNames[id] = mumbleLinkName;
  } else {
    m_runningMumbleNames.remove(id);
  }
  saveRunningProfiles();
  emit profileRunningStateChanged(id, true);
  qInfo() << "Profile" << id << "now running with PID:" << pid
          << "mumbleLink:" << (mumbleLinkName.isEmpty() ? "MumbleLink" : mumbleLinkName);
}

void ProfileManager::clearProfileRunning(const QString &id) {
  if (m_runningProfiles.remove(id) > 0) {
    m_runningMumbleNames.remove(id);
    saveRunningProfiles();
    emit profileRunningStateChanged(id, false);
    qInfo() << "Profile" << id << "no longer running";
  }
}

void ProfileManager::validateRunningProfiles() {
  QList<QString> staleIds;
  for (auto it = m_runningProfiles.begin(); it != m_runningProfiles.end();
       ++it) {
    if (!isProcessAlive(it.value())) {
      staleIds.append(it.key());
    }
  }

  for (const QString &id : staleIds) {
    qInfo() << "Removing stale PID for profile:" << id;
    m_runningProfiles.remove(id);
    m_runningMumbleNames.remove(id);
    emit profileRunningStateChanged(id, false);
  }

  if (!staleIds.isEmpty()) {
    saveRunningProfiles();
  }
}

bool ProfileManager::isProcessAlive(qint64 pid) const {
#ifdef Q_OS_WIN
  HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                static_cast<DWORD>(pid));
  if (hProcess == nullptr)
    return false;

  DWORD exitCode = 0;
  bool stillRunning =
      GetExitCodeProcess(hProcess, &exitCode) && exitCode == STILL_ACTIVE;
  CloseHandle(hProcess);
  return stillRunning;
#else
  return false;
#endif
}

void ProfileManager::loadRunningProfiles() {
  QFile file(m_runningProfilesPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return; // No file yet, that's OK
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (!doc.isObject())
    return;

  QJsonObject obj = doc.object();
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    if (it.value().isObject()) {
      // New format: {"profileId": {"pid": 12345, "mumbleLinkName": "..."}}
      QJsonObject entry = it.value().toObject();
      m_runningProfiles[it.key()] = entry["pid"].toVariant().toLongLong();
      QString mumbleName = entry["mumbleLinkName"].toString();
      if (!mumbleName.isEmpty()) {
        m_runningMumbleNames[it.key()] = mumbleName;
      }
    } else {
      // Old format (backward compat): {"profileId": 12345}
      m_runningProfiles[it.key()] = it.value().toVariant().toLongLong();
    }
  }

  qInfo() << "Loaded" << m_runningProfiles.size()
          << "running profiles from disk";
}

void ProfileManager::saveRunningProfiles() {
  QJsonObject obj;
  for (auto it = m_runningProfiles.begin(); it != m_runningProfiles.end();
       ++it) {
    QJsonObject entry;
    entry["pid"] = it.value();
    QString mumbleName = m_runningMumbleNames.value(it.key());
    if (!mumbleName.isEmpty()) {
      entry["mumbleLinkName"] = mumbleName;
    }
    obj[it.key()] = entry;
  }

  if (!AtomicFileWriter::writeJson(m_runningProfilesPath, obj)) {
    qWarning() << "Failed to save running profiles";
  }
}

bool ProfileManager::bringProfileWindowToFocus(const QString &id) {
#ifdef Q_OS_WIN
  if (!isProfileRunning(id))
    return false;

  qint64 pid = m_runningProfiles[id];
  HWND targetWindow = nullptr;

  // Struct to pass data to callback
  struct EnumData {
    DWORD targetPid;
    HWND foundWindow;
  } enumData = {static_cast<DWORD>(pid), nullptr};

  // Find window belonging to this PID with ArenaNet class
  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *data = reinterpret_cast<EnumData *>(lParam);

        DWORD windowPid = 0;
        GetWindowThreadProcessId(hwnd, &windowPid);

        if (windowPid == data->targetPid) {
          wchar_t className[256];
          GetClassNameW(hwnd, className, 256);
          if (wcsstr(className, L"ArenaNet")) {
            data->foundWindow = hwnd;
            return FALSE; // Stop enumeration
          }
        }
        return TRUE; // Continue
      },
      reinterpret_cast<LPARAM>(&enumData));

  targetWindow = enumData.foundWindow;

  if (targetWindow) {
    // Restore if minimized
    if (IsIconic(targetWindow)) {
      ShowWindow(targetWindow, SW_RESTORE);
    }

    // Bring to foreground
    SetForegroundWindow(targetWindow);

    qInfo() << "Brought window to focus for profile:" << id;
    return true;
  }

  qWarning() << "Could not find window for profile:" << id;
  return false;
#else
  return false;
#endif
}

bool ProfileManager::minimizeProfileWindow(const QString &id) {
#ifdef Q_OS_WIN
  if (!isProfileRunning(id))
    return false;

  qint64 pid = m_runningProfiles[id];

  struct EnumData {
    DWORD targetPid;
    HWND foundWindow;
  } enumData = {static_cast<DWORD>(pid), nullptr};

  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *data = reinterpret_cast<EnumData *>(lParam);

        DWORD windowPid = 0;
        GetWindowThreadProcessId(hwnd, &windowPid);

        if (windowPid == data->targetPid) {
          wchar_t className[256];
          GetClassNameW(hwnd, className, 256);
          if (wcsstr(className, L"ArenaNet")) {
            data->foundWindow = hwnd;
            return FALSE;
          }
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&enumData));

  if (enumData.foundWindow) {
    ShowWindow(enumData.foundWindow, SW_MINIMIZE);
    qInfo() << "Minimized window for profile:" << id;
    return true;
  }

  qWarning() << "Could not find window to minimize for profile:" << id;
  return false;
#else
  Q_UNUSED(id);
  return false;
#endif
}

// === Hotkey File Operations ===

bool ProfileManager::loadHotkeys(AccountProfile &profile) {
  if (m_savedHotkeysDir.isEmpty())
    return false;

  QString filePath =
      QDir(m_savedHotkeysDir).filePath(profile.id + "_hotkeys.json");

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    // No hotkey file yet — that's normal for new or migrated profiles
    return false;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    qWarning() << "Failed to parse hotkeys file for:" << profile.id;
    return false;
  }

  QJsonObject obj = doc.object();

  // Validate file type
  if (obj["type"].toString() != "gw2aio_hotkeys") {
    qWarning() << "Invalid hotkeys file type for:" << profile.id;
    return false;
  }

  // int version = obj["version"].toInt(1); // For future migration
  profile.hotkeyFocus = obj["hotkeyFocus"].toString();
  profile.hotkeyMinimize = obj["hotkeyMinimize"].toString();

  return true;
}

bool ProfileManager::saveHotkeys(const AccountProfile &profile) {
  if (m_savedHotkeysDir.isEmpty())
    return false;

  // Only write hotkey file if there are hotkeys to save
  if (profile.hotkeyFocus.isEmpty() && profile.hotkeyMinimize.isEmpty()) {
    // Clean up file if it exists but both hotkeys are now cleared
    QString filePath =
        QDir(m_savedHotkeysDir).filePath(profile.id + "_hotkeys.json");
    if (QFile::exists(filePath)) {
      QFile::remove(filePath);
      QFile::remove(filePath + ".bak");
      QFile::remove(filePath + ".tmp");
    }
    return true;
  }

  QDir().mkpath(m_savedHotkeysDir);

  QJsonObject obj;
  obj["type"] = "gw2aio_hotkeys";
  obj["version"] = 1;
  obj["hotkeyFocus"] = profile.hotkeyFocus;
  obj["hotkeyMinimize"] = profile.hotkeyMinimize;

  QString filePath =
      QDir(m_savedHotkeysDir).filePath(profile.id + "_hotkeys.json");
  return AtomicFileWriter::writeJson(filePath, obj);
}
