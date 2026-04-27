#pragma once

/**
 * @brief Simple disk cache for GW2 API responses
 *
 * Stores JSON responses per profile with configurable TTL.
 * Used to avoid redundant API calls and provide offline access.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Network logic (belongs in GW2APIClient)
 * - Inline implementations (use APICache.cpp)
 */

#include <QJsonDocument>
#include <QString>

class APICache {
public:
  explicit APICache(const QString &cacheDir);

  /**
   * @brief Check if a cached response exists and is still valid
   * @param profileId Profile UUID
   * @param endpoint API endpoint (e.g., "account", "characters")
   * @param ttlSeconds Maximum age in seconds before cache is stale
   * @return true if cache exists and is within TTL
   */
  bool isValid(const QString &profileId, const QString &endpoint,
               int ttlSeconds) const;

  /**
   * @brief Read a cached response
   * @return The cached JSON document, or null document if not found
   */
  QJsonDocument read(const QString &profileId, const QString &endpoint) const;

  /**
   * @brief Write a response to cache
   * @return true if written successfully
   */
  bool write(const QString &profileId, const QString &endpoint,
             const QJsonDocument &data);

  /**
   * @brief Delete all cached data for a profile
   */
  void clearProfile(const QString &profileId);

  /**
   * @brief Delete a specific cached endpoint for a profile
   */
  void clearEndpoint(const QString &profileId, const QString &endpoint);

  // --- TTL constants (seconds) ---
  static constexpr int TTL_ACCOUNT = 300;      // 5 minutes
  static constexpr int TTL_CHARACTERS = 300;   // 5 minutes
  static constexpr int TTL_WALLET = 120;       // 2 minutes
  static constexpr int TTL_ACHIEVEMENTS = 600; // 10 minutes
  static constexpr int TTL_MOUNTS = 3600;      // 1 hour (rarely changes)
  static constexpr int TTL_TOKEN_INFO = -1;    // Never expires (manual only)

private:
  /**
   * @brief Get the file path for a cached endpoint
   */
  QString cachePath(const QString &profileId, const QString &endpoint) const;

  QString m_cacheDir;
};
