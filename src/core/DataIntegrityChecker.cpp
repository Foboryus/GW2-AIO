/**
 * @file DataIntegrityChecker.cpp
 * @brief Centralized startup validator for profile data integrity
 *
 * Runs after ProfileManager::load() to validate and auto-fix issues.
 * All writes go through ProfileManager's atomic save pipeline.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Backup/recovery (belongs in ProfileManager)
 */

#include "DataIntegrityChecker.h"
#include "ProfileManager.h"
#include "StorageBackend.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QUuid>

DataIntegrityChecker::DataIntegrityChecker(ProfileManager *pm,
                                           StorageBackend *sb)
    : m_pm(pm), m_sb(sb) {}

CheckResult DataIntegrityChecker::runAllChecks() {
  m_result = CheckResult(); // Reset

  qInfo() << "DataIntegrityChecker: Starting integrity checks...";

  checkMissingUUIDs();
  checkDuplicateUUIDs();
  checkOrphanedProfileFiles();
  checkMissingProfileFiles();
  checkStalePaths();
  checkInvalidCustomGw2Paths();
  checkOrphanedDataFiles();
  checkSchemaVersions();

  // Summary log
  if (m_result.issuesFound == 0) {
    qInfo() << "DataIntegrityChecker: All clear (0 issues)";
  } else {
    int warningCount = m_result.warnings.size();
    int fixedCount = m_result.issuesFixed;
    qInfo() << QString("DataIntegrityChecker: %1 issues found, %2 fixed, %3 "
                       "warnings")
                   .arg(m_result.issuesFound)
                   .arg(fixedCount)
                   .arg(warningCount);
    for (const QString &w : m_result.warnings) {
      qWarning() << "  Warning:" << w;
    }
  }

  return m_result;
}

// =============================================================================
// Single-profile validation (for post-import checks)
// =============================================================================

CheckResult
DataIntegrityChecker::validateSingleProfile(const QString &profileId) {
  m_result = CheckResult(); // Reset

  AccountProfile *p = m_pm->profile(profileId);
  if (!p) {
    qWarning() << "DataIntegrityChecker: validateSingleProfile called with "
                  "unknown ID:"
               << profileId;
    return m_result;
  }

  qInfo() << "DataIntegrityChecker: Validating imported profile:"
          << p->nickname;

  // Check stale Local.dat path
  if (!p->localDatPath.isEmpty() && !QFileInfo::exists(p->localDatPath)) {
    m_result.issuesFound++;
    qInfo() << "DataIntegrityChecker: Clearing stale localDatPath for"
            << p->nickname << ":" << p->localDatPath;
    p->localDatPath.clear();
    m_pm->updateProfile(*p);
    m_result.issuesFixed++;
  }

  // Check stale GFX settings path
  if (!p->gfxSettingsPath.isEmpty() && !QFileInfo::exists(p->gfxSettingsPath)) {
    m_result.issuesFound++;
    qInfo() << "DataIntegrityChecker: Clearing stale gfxSettingsPath for"
            << p->nickname << ":" << p->gfxSettingsPath;
    p->gfxSettingsPath.clear();
    m_pm->updateProfile(*p);
    m_result.issuesFixed++;
  }

  // Check invalid custom GW2 path
  if (p->useCustomGw2Path && !p->customGw2Path.isEmpty()) {
    QFileInfo fi(p->customGw2Path);
    if (!fi.exists()) {
      m_result.issuesFound++;
      qInfo() << "DataIntegrityChecker: Custom GW2 path invalid for"
              << p->nickname << ":" << p->customGw2Path
              << "- disabling useCustomGw2Path";
      p->useCustomGw2Path = false;
      m_pm->updateProfile(*p);
      m_result.issuesFixed++;
    }
  }

  // Summary
  if (m_result.issuesFound == 0) {
    qInfo() << "DataIntegrityChecker: Imported profile" << p->nickname
            << "- all clear";
  } else {
    qInfo() << "DataIntegrityChecker: Imported profile" << p->nickname << "-"
            << m_result.issuesFound << "issues found," << m_result.issuesFixed
            << "fixed";
  }

  return m_result;
}

// =============================================================================
// Check 1: Missing UUIDs
// =============================================================================

void DataIntegrityChecker::checkMissingUUIDs() {
  const QList<AccountProfile> profiles = m_pm->profiles();

  for (int i = 0; i < profiles.size(); ++i) {
    if (profiles[i].id.isEmpty()) {
      m_result.issuesFound++;

      // Generate a new UUID
      QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);

      AccountProfile *p = m_pm->profile(profiles[i].id);
      if (p) {
        p->id = newId;
        m_pm->updateProfile(*p);
        m_result.issuesFixed++;
        qInfo() << "DataIntegrityChecker: Generated UUID for profile"
                << p->nickname << "->" << newId;
      }
    }
  }
}

// =============================================================================
// Check 2: Duplicate UUIDs
// =============================================================================

void DataIntegrityChecker::checkDuplicateUUIDs() {
  const QList<AccountProfile> profiles = m_pm->profiles();
  QSet<QString> seen;

  for (int i = 0; i < profiles.size(); ++i) {
    const QString &id = profiles[i].id;
    if (id.isEmpty()) {
      continue; // Already handled by checkMissingUUIDs
    }

    if (seen.contains(id)) {
      m_result.issuesFound++;

      // Regenerate UUID for the duplicate
      QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);

      AccountProfile *p = m_pm->profile(id);
      if (p) {
        qInfo() << "DataIntegrityChecker: Duplicate UUID" << id << "for profile"
                << p->nickname << "-> regenerated" << newId;
        p->id = newId;
        m_pm->updateProfile(*p);
        m_result.issuesFixed++;
      }
    } else {
      seen.insert(id);
    }
  }
}

// =============================================================================
// Check 3: Orphaned profile files (in profiles dir but not in manifest)
// =============================================================================

void DataIntegrityChecker::checkOrphanedProfileFiles() {
  QString profilesDir = m_sb->profilesDir();
  if (profilesDir.isEmpty()) {
    return;
  }

  // Collect all known profile IDs
  QSet<QString> knownIds;
  const QList<AccountProfile> profiles = m_pm->profiles();
  for (const auto &p : profiles) {
    knownIds.insert(p.id);
  }

  // Scan for .json files in profiles dir
  QDirIterator it(profilesDir, {"*.json"}, QDir::Files);
  while (it.hasNext()) {
    it.next();
    QString filename = it.fileName();
    if (filename == "manifest.json") {
      continue;
    }

    QString fileId = filename.chopped(5); // Remove .json
    if (!knownIds.contains(fileId)) {
      m_result.issuesFound++;
      m_result.warnings.append(
          QString("Orphaned profile file: %1 (not in manifest)").arg(filename));
      // Don't delete — user may want to reimport
    }
  }
}

// =============================================================================
// Check 4: Missing profile files (in manifest but no .json file)
// Already handled by ProfileManager::load() which skips missing files,
// but we log it as a warning for visibility.
// =============================================================================

void DataIntegrityChecker::checkMissingProfileFiles() {
  QString profilesDir = m_sb->profilesDir();
  if (profilesDir.isEmpty()) {
    return;
  }

  const QList<AccountProfile> profiles = m_pm->profiles();
  for (const auto &p : profiles) {
    QString filePath = QDir(profilesDir).filePath(p.id + ".json");
    if (!QFileInfo::exists(filePath)) {
      // Also check for backup
      QString backupPath = filePath + ".bak";
      if (!QFileInfo::exists(backupPath)) {
        m_result.issuesFound++;
        m_result.warnings.append(
            QString("Profile '%1' (ID: %2) loaded in memory but no file on "
                    "disk (possible data loss)")
                .arg(p.nickname, p.id));
      }
    }
  }
}

// =============================================================================
// Check 5 & 6: Stale GFX and Local.dat paths
// Note: GFX path is also checked in ProfileManager::loadProfileFromFile()
// (defense in depth — this is a second-pass safety net)
// =============================================================================

void DataIntegrityChecker::checkStalePaths() {
  const QList<AccountProfile> profiles = m_pm->profiles();

  for (const auto &profile : profiles) {
    // Check Local.dat path
    if (!profile.localDatPath.isEmpty() &&
        !QFileInfo::exists(profile.localDatPath)) {
      m_result.issuesFound++;

      AccountProfile *p = m_pm->profile(profile.id);
      if (p) {
        qInfo() << "DataIntegrityChecker: Clearing stale localDatPath for"
                << p->nickname << ":" << p->localDatPath;
        p->localDatPath.clear();
        m_pm->updateProfile(*p);
        m_result.issuesFixed++;
      }
    }

    // Check GFX settings path
    if (!profile.gfxSettingsPath.isEmpty() &&
        !QFileInfo::exists(profile.gfxSettingsPath)) {
      m_result.issuesFound++;

      AccountProfile *p = m_pm->profile(profile.id);
      if (p) {
        qInfo() << "DataIntegrityChecker: Clearing stale gfxSettingsPath for"
                << p->nickname << ":" << p->gfxSettingsPath;
        p->gfxSettingsPath.clear();
        m_pm->updateProfile(*p);
        m_result.issuesFixed++;
      }
    }
  }
}

// =============================================================================
// Check 7: Invalid custom GW2 paths
// =============================================================================

void DataIntegrityChecker::checkInvalidCustomGw2Paths() {
  const QList<AccountProfile> profiles = m_pm->profiles();

  for (const auto &profile : profiles) {
    if (profile.useCustomGw2Path && !profile.customGw2Path.isEmpty()) {
      QFileInfo fi(profile.customGw2Path);
      if (!fi.exists()) {
        m_result.issuesFound++;

        AccountProfile *p = m_pm->profile(profile.id);
        if (p) {
          qInfo() << "DataIntegrityChecker: Custom GW2 path invalid for"
                  << p->nickname << ":" << p->customGw2Path
                  << "- disabling useCustomGw2Path";
          p->useCustomGw2Path = false;
          m_pm->updateProfile(*p);
          m_result.issuesFixed++;
        }
      }
    }
  }
}

// =============================================================================
// Check 8 & 9: Orphaned .dat and .xml files
// =============================================================================

void DataIntegrityChecker::checkOrphanedDataFiles() {
  const QList<AccountProfile> profiles = m_pm->profiles();

  // Collect all known profile IDs
  QSet<QString> knownIds;
  for (const auto &p : profiles) {
    knownIds.insert(p.id);
  }

  // Check savedDatsDir for orphaned .dat files
  QString datsDir = m_sb->savedDatsDir();
  if (!datsDir.isEmpty() && QDir(datsDir).exists()) {
    QDirIterator datIt(datsDir, {"*.dat"}, QDir::Files);
    while (datIt.hasNext()) {
      datIt.next();
      QString fileId = QFileInfo(datIt.fileName()).baseName();
      if (!knownIds.contains(fileId)) {
        m_result.issuesFound++;
        if (QFile::remove(datIt.filePath())) {
          qInfo() << "DataIntegrityChecker: Removed orphaned Local.dat:"
                  << datIt.fileName();
          m_result.issuesFixed++;
        } else {
          m_result.warnings.append(
              QString("Failed to remove orphaned Local.dat: %1")
                  .arg(datIt.fileName()));
        }
      }
    }
  }

  // Check savedGfxDir for orphaned .xml files
  QString gfxDir = m_sb->savedGfxDir();
  if (!gfxDir.isEmpty() && QDir(gfxDir).exists()) {
    QDirIterator gfxIt(gfxDir, {"*.xml"}, QDir::Files);
    while (gfxIt.hasNext()) {
      gfxIt.next();
      // GFX files are named {uuid}_GFX.xml
      QString baseName = QFileInfo(gfxIt.fileName()).baseName();
      QString fileId = baseName.endsWith("_GFX")
                           ? baseName.chopped(4) // Remove _GFX
                           : baseName;
      if (!knownIds.contains(fileId)) {
        m_result.issuesFound++;
        if (QFile::remove(gfxIt.filePath())) {
          qInfo() << "DataIntegrityChecker: Removed orphaned GFX file:"
                  << gfxIt.fileName();
          m_result.issuesFixed++;
        } else {
          m_result.warnings.append(
              QString("Failed to remove orphaned GFX file: %1")
                  .arg(gfxIt.fileName()));
        }
      }
    }
  }

  // Check savedHotkeysDir for orphaned hotkey files
  QString hotkeysDir = m_sb->savedHotkeysDir();
  if (!hotkeysDir.isEmpty() && QDir(hotkeysDir).exists()) {
    QDirIterator hkIt(hotkeysDir, {"*_hotkeys.json"}, QDir::Files);
    while (hkIt.hasNext()) {
      hkIt.next();
      // Hotkey files are named {uuid}_hotkeys.json
      QString baseName = QFileInfo(hkIt.fileName()).baseName();
      QString fileId = baseName.endsWith("_hotkeys")
                           ? baseName.chopped(8) // Remove _hotkeys
                           : baseName;
      if (!knownIds.contains(fileId)) {
        m_result.issuesFound++;
        if (QFile::remove(hkIt.filePath())) {
          qInfo() << "DataIntegrityChecker: Removed orphaned hotkey file:"
                  << hkIt.fileName();
          m_result.issuesFixed++;
        }
        // Also remove .bak and .tmp
        QFile::remove(hkIt.filePath() + ".bak");
        QFile::remove(hkIt.filePath() + ".tmp");
      }
    }
  }

  // Check ProfileData for orphaned profile folders
  QString profileDataDir = m_sb->profileDataDir();
  if (!profileDataDir.isEmpty() && QDir(profileDataDir).exists()) {
    QDir pdDir(profileDataDir);
    for (const QFileInfo &dirInfo :
         pdDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      QString folderName = dirInfo.fileName();
      if (folderName == "_default" || folderName == "marker_state") {
        continue; // Preserve non-profile structural directories
      }
      if (!knownIds.contains(folderName)) {
        m_result.issuesFound++;
        if (QDir(dirInfo.absoluteFilePath()).removeRecursively()) {
          qInfo()
              << "DataIntegrityChecker: Removed orphaned ProfileData folder:"
              << folderName;
          m_result.issuesFixed++;
        } else {
          m_result.warnings.append(
              QString("Failed to remove orphaned ProfileData folder: %1")
                  .arg(folderName));
        }
      }
    }
  }
}

// =============================================================================
// Check 10: Schema version
// Currently at v1. Future migrations would go here.
// =============================================================================

void DataIntegrityChecker::checkSchemaVersions() {
  // Currently all profiles are at schema version 1.
  // When we bump to v2+, migration logic goes here.
  // ProfileManager::fromJson() already handles v0->v1 migration
  // (useSteam -> accountProvider).
  //
  // Future pattern:
  //   if (version < 2) { migrateV1toV2(profile); }
  //   if (version < 3) { migrateV2toV3(profile); }
}
