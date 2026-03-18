/**
 * @file DataService.cpp
 * @brief Centralized Data Service implementation
 *
 * All methods delegate to the appropriate inner manager.
 * Change signals are emitted on every write operation.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Direct QSettings usage (use SettingsManager)
 */

#include "DataService.h"
#include "DataIntegrityChecker.h"
#include "GFXManager.h"
#include "LocalDatManager.h"
#include "LocalStorageBackend.h"
#include "ProfileManager.h"
#include "SettingsManager.h"
#include "UpdateManager.h"
#include "features/markers/ActivationStore.h"
#include "features/markers/MarkerSettingsManager.h"

DataService::DataService(QObject *parent)
    : QObject(parent), m_storageBackend(new LocalStorageBackend()),
      m_profileManager(new ProfileManager(
          m_storageBackend->profilesDir(), m_storageBackend->savedDatsDir(),
          m_storageBackend->savedGfxDir(), m_storageBackend->savedHotkeysDir(),
          m_storageBackend->markerStateDir(), this)),
      m_settingsManager(
          new SettingsManager(m_storageBackend->settingsFilePath(), this)),
      m_updateManager(
          new UpdateManager(m_storageBackend->settingsFilePath(), this)),
      m_localDatManager(
          new LocalDatManager(m_storageBackend->profileDataDir(), this)),
      m_gfxManager(new GFXManager(m_storageBackend->savedGfxDir(), this)),
      m_activationStore(
          new ActivationStore(m_storageBackend->markerStateDir(), this)),
      m_markerSettings(
          new MarkerSettingsManager(m_storageBackend->markerStateDir(), this)) {

  // Forward ProfileManager signals
  connect(m_profileManager, &ProfileManager::profilesChanged, this,
          &DataService::profilesChanged);
  connect(m_profileManager, &ProfileManager::profileAdded, this,
          &DataService::profileAdded);
  connect(m_profileManager, &ProfileManager::profileUpdated, this,
          &DataService::profileUpdated);
  connect(m_profileManager, &ProfileManager::profileRemoved, this,
          &DataService::profileRemoved);
  connect(m_profileManager, &ProfileManager::profileRunningStateChanged, this,
          &DataService::profileRunningStateChanged);
  connect(m_profileManager, &ProfileManager::profilesRestoredFromBackup, this,
          &DataService::profilesRestoredFromBackup);

  // Clean up ProfileData folder when a profile is deleted
  connect(m_profileManager, &ProfileManager::profileRemoved, m_localDatManager,
          &LocalDatManager::deleteProfileFolder);

  // Run data integrity checks after all managers are initialized
  DataIntegrityChecker checker(m_profileManager, m_storageBackend);
  checker.runAllChecks();

  // Migrate from old flat LocalDats/ to new ProfileData/{uuid}/ structure
  m_localDatManager->migrateFromFlatFiles(m_storageBackend->savedDatsDir());

  // Clean up stale junctions if a previous session crashed
  m_localDatManager->cleanupOnStartup();

  // Clean up marker state files when profile is deleted
  connect(m_profileManager, &ProfileManager::profileRemoved, this,
          [this](const QString &id) {
            ActivationStore::deleteProfileState(
                m_storageBackend->markerStateDir(), id);
            m_markerSettings->deleteForProfile(id);
          });
}

DataService::~DataService() { delete m_storageBackend; }

// =========================================================================
// PROFILE OPERATIONS
// =========================================================================

QList<AccountProfile> DataService::profiles() const {
  return m_profileManager->profiles();
}

AccountProfile *DataService::profile(const QString &id) {
  return m_profileManager->profile(id);
}

AccountProfile *DataService::profileByNickname(const QString &nickname) {
  return m_profileManager->profileByNickname(nickname);
}

QString DataService::addProfile(const QString &nickname) {
  return m_profileManager->addProfile(nickname);
}

void DataService::addCompleteProfile(const AccountProfile &profile) {
  m_profileManager->addCompleteProfile(profile);
}

bool DataService::updateProfile(const AccountProfile &profile) {
  return m_profileManager->updateProfile(profile);
}

bool DataService::removeProfile(const QString &id) {
  return m_profileManager->removeProfile(id);
}

bool DataService::moveProfile(int fromIndex, int toIndex) {
  bool result = m_profileManager->moveProfile(fromIndex, toIndex);
  if (result) {
    emit profileOrderChanged();
  }
  return result;
}

// --- Targeted profile mutations ---

void DataService::setLastLoginTime(const QString &profileId,
                                   const QDateTime &time) {
  auto *p = m_profileManager->profile(profileId);
  if (p) {
    p->lastLoginTime = time;
    m_profileManager->updateProfile(*p);
  }
}

void DataService::setLocalDatPath(const QString &profileId,
                                  const QString &path) {
  auto *p = m_profileManager->profile(profileId);
  if (p) {
    p->localDatPath = path;
    m_profileManager->updateProfile(*p);
  }
}

void DataService::setGfxSettingsPath(const QString &profileId,
                                     const QString &path) {
  auto *p = m_profileManager->profile(profileId);
  if (p) {
    p->gfxSettingsPath = path;
    m_profileManager->updateProfile(*p);
  }
}

// --- Import/Export ---

bool DataService::exportProfile(const QString &id, const QString &filePath) {
  return m_profileManager->exportProfile(id, filePath);
}

bool DataService::importProfile(const QString &filePath) {
  return m_profileManager->importProfile(filePath);
}

bool DataService::exportAllProfiles(const QString &folderPath) {
  return m_profileManager->exportAllProfiles(folderPath);
}

bool DataService::importProfileWithChecks(const QString &filePath,
                                          QString &outGw2Path) {
  outGw2Path.clear();

  // Capture profile count before import to identify the new profile
  int countBefore = m_profileManager->profiles().size();

  bool ok = m_profileManager->importProfile(filePath);
  if (!ok) {
    return false;
  }

  // The newly imported profile is the last one in the list
  QList<AccountProfile> allProfiles = m_profileManager->profiles();
  if (allProfiles.size() <= countBefore) {
    qWarning() << "importProfileWithChecks: Profile count didn't increase "
                  "after import";
    return true; // Import succeeded but couldn't find profile for checks
  }

  const QString &importedId = allProfiles.last().id;

  // Run integrity checks on the imported profile
  DataIntegrityChecker checker(m_profileManager, m_storageBackend);
  checker.validateSingleProfile(importedId);

  // Re-read the profile after integrity fixes may have modified it
  AccountProfile *profile = m_profileManager->profile(importedId);
  if (!profile) {
    return true; // Import succeeded but profile disappeared (shouldn't happen)
  }

  // Resolve the GW2 path this profile will use
  outGw2Path = (profile->useCustomGw2Path && !profile->customGw2Path.isEmpty())
                   ? profile->customGw2Path
                   : gw2Path();

  // Note: No build ID sync on import. New paths start at 0 (unverified).
  // UpdateManager will require a clean launch to verify the build for this
  // path.

  return true;
}

// --- Default profile ---

QString DataService::defaultProfileId() const {
  return m_profileManager->defaultProfileId();
}

void DataService::setDefaultProfileId(const QString &id) {
  m_profileManager->setDefaultProfileId(id);
}

AccountProfile *DataService::defaultProfile() {
  return m_profileManager->defaultProfile();
}

// --- Running state ---

bool DataService::isProfileRunning(const QString &id) {
  return m_profileManager->isProfileRunning(id);
}

qint64 DataService::getProfilePid(const QString &id) const {
  return m_profileManager->getProfilePid(id);
}

void DataService::setProfileRunning(const QString &id, qint64 pid) {
  m_profileManager->setProfileRunning(id, pid);
}

void DataService::clearProfileRunning(const QString &id) {
  m_profileManager->clearProfileRunning(id);
}

void DataService::validateRunningProfiles() {
  m_profileManager->validateRunningProfiles();
}

QMap<QString, qint64> DataService::runningProfiles() const {
  return m_profileManager->runningProfiles();
}

bool DataService::bringProfileWindowToFocus(const QString &id) {
  return m_profileManager->bringProfileWindowToFocus(id);
}

bool DataService::minimizeProfileWindow(const QString &id) {
  return m_profileManager->minimizeProfileWindow(id);
}

bool DataService::loadProfiles() { return m_profileManager->load(); }

bool DataService::saveProfiles() { return m_profileManager->save(); }

// =========================================================================
// GLOBAL SETTINGS
// =========================================================================

QVariant DataService::setting(const QString &key,
                              const QVariant &defaultValue) const {
  return m_settingsManager->value(key, defaultValue);
}

void DataService::setSetting(const QString &key, const QVariant &value) {
  m_settingsManager->setValue(key, value);
  emit settingChanged(key, value);
}

bool DataService::hasSetting(const QString &key) const {
  return m_settingsManager->contains(key);
}

void DataService::removeSetting(const QString &key) {
  m_settingsManager->remove(key);
}

// --- Typed convenience accessors ---

QString DataService::gw2Path() const {
  return m_settingsManager->value("gw2Path").toString();
}

void DataService::setGw2Path(const QString &path) {
  setSetting("gw2Path", path);
}

bool DataService::showTrayIcon() const {
  return m_settingsManager->value("showTrayIcon", true).toBool();
}

void DataService::setShowTrayIcon(bool show) {
  setSetting("showTrayIcon", show);
}

bool DataService::startMinimized() const {
  return m_settingsManager->value("startMinimized", false).toBool();
}

void DataService::setStartMinimized(bool minimize) {
  setSetting("startMinimized", minimize);
}

bool DataService::checkUpdates() const {
  return m_settingsManager->value("checkUpdates", true).toBool();
}

void DataService::setCheckUpdates(bool check) {
  setSetting("checkUpdates", check);
}

bool DataService::cefCleanup() const {
  return m_settingsManager->value("cefCleanup", true).toBool();
}

void DataService::setCefCleanup(bool clean) { setSetting("cefCleanup", clean); }

int DataService::selectedTheme() const {
  return m_settingsManager->value("theme", 0).toInt();
}

void DataService::setSelectedTheme(int theme) { setSetting("theme", theme); }

void DataService::syncSettings() { m_settingsManager->sync(); }

QString DataService::settingsFilePath() const {
  return m_settingsManager->settingsFilePath();
}

QString DataService::savedDatsDir() const {
  return m_storageBackend->savedDatsDir();
}

QString DataService::savedGfxDir() const {
  return m_storageBackend->savedGfxDir();
}

// =========================================================================
// LOCAL DAT & GFX MANAGERS
// =========================================================================

LocalDatManager *DataService::localDatManager() { return m_localDatManager; }

GFXManager *DataService::gfxManager() { return m_gfxManager; }

ActivationStore *DataService::activationStore() { return m_activationStore; }

MarkerSettingsManager *DataService::markerSettings() {
  return m_markerSettings;
}

// =========================================================================
// UPDATE MANAGER
// =========================================================================

// UpdateManager is accessed directly via updateManager() accessor.
// No wrappers needed — callers use the full UpdateManager API.
