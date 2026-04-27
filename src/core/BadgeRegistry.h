#pragma once

/**
 * @brief Static registry of all available profile badge types
 *
 * Defines badge metadata (ID, label, icon, API source) and provides
 * formatting functions to convert raw API data into display strings.
 *
 * Includes static lookup tables for GW2 world names and PvP rank tiers.
 *
 * DO NOT ADD:
 * - Network logic (belongs in GW2APIClient/BadgeDataProvider)
 * - UI code (belongs in AccountTabWidget/LauncherWidget)
 * - Mutable state (this is a pure static registry)
 */

#include <QList>
#include <QMap>
#include <QString>

class QJsonDocument;

struct BadgeDefinition {
  QString id;          // e.g., "account_age"
  QString label;       // e.g., "Account Age"
  QString iconName;    // SVG name without path, e.g., "badge-shield"
  QString apiEndpoint; // e.g., "account", "account/mastery/points"
  QString scope;       // Required API permission scope
};

class BadgeRegistry {
public:
  /// @brief Get all available badge definitions
  static QList<BadgeDefinition> allBadges();

  /// @brief Look up a single badge by ID
  /// @return Badge definition, or empty definition if not found
  static BadgeDefinition badge(const QString &id);

  /// @brief Format a raw API cache value into a display string for a badge
  /// @param badgeId The badge type ID
  /// @param data The cached API response JSON
  /// @return Formatted display string, e.g., "42,150 AP" or "—" on failure
  static QString formatValue(const QString &badgeId,
                             const QJsonDocument &data);

  /// @brief Get the list of unique API endpoints needed for a set of badge IDs
  static QStringList requiredEndpoints(const QStringList &badgeIds);

  /// @brief Look up a GW2 world name by ID
  /// @return World name or "Unknown" if not found
  static QString worldName(int worldId);

  /// @brief Get PvP rank tier name from numeric rank
  /// @return Tier name, e.g., "Dragon", "Phoenix"
  static QString pvpRankTier(int rank);
};
