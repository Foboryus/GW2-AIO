#pragma once

/**
 * @file DataIntegrityChecker.h
 * @brief Centralized startup validator for profile data integrity
 *
 * Runs once at startup after ProfileManager::load().
 * Validates all loaded data and auto-fixes known issues.
 * All fixes go through ProfileManager's atomic save pipeline.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Backup/recovery (belongs in ProfileManager)
 * - Launch logic (belongs in LaunchManager)
 */

#include <QString>
#include <QStringList>

class ProfileManager;
class StorageBackend;

/**
 * @brief Result of an integrity check run
 */
struct CheckResult {
  int issuesFound = 0;
  int issuesFixed = 0;
  QStringList warnings; // Issues logged but not auto-fixed
};

/**
 * @brief Validates profile data integrity at startup
 *
 * Receives dependencies via constructor injection.
 * Does not own any data — operates on ProfileManager's loaded state.
 */
class DataIntegrityChecker {
public:
  DataIntegrityChecker(ProfileManager *pm, StorageBackend *sb);

  /// @brief Run all integrity checks. Call once after ProfileManager::load().
  CheckResult runAllChecks();

  /// @brief Run focused checks on a single profile (e.g., after import).
  /// Validates stale paths, invalid custom GW2 paths, and schema version.
  CheckResult validateSingleProfile(const QString &profileId);

private:
  // Individual checks
  void checkMissingUUIDs();
  void checkDuplicateUUIDs();
  void checkOrphanedProfileFiles();
  void checkMissingProfileFiles();
  void checkStalePaths();
  void checkInvalidCustomGw2Paths();
  void checkOrphanedDataFiles();
  void checkSchemaVersions();

  ProfileManager *m_pm;
  StorageBackend *m_sb;
  CheckResult m_result;
};
