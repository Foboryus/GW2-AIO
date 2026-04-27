#pragma once

/**
 * @brief Fetches and aggregates API badge data for profiles
 *
 * Reads from existing APICache for basic badges (account, characters),
 * triggers additional endpoint fetches for extended badges (mastery,
 * legendaryarmory, dyes, mounts/skins, pvp/stats).
 *
 * Emits badgeDataReady() with formatted badge values per profile.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Direct network calls (uses GW2APIClient via DataService)
 */

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>

class APICache;
class GW2APIClient;

class BadgeDataProvider : public QObject {
  Q_OBJECT

public:
  explicit BadgeDataProvider(APICache *cache, GW2APIClient *apiClient,
                             QObject *parent = nullptr);

  /**
   * @brief Refresh badge data for a single profile
   * @param profileId Profile UUID
   * @param apiKey The API key for this profile
   * @param selectedBadges List of badge IDs the user has selected
   *
   * Reads from cache for basic endpoints. Fetches extended endpoints
   * if needed. Emits badgeDataReady when all data is aggregated.
   */
  void refreshProfile(const QString &profileId, const QString &apiKey,
                      const QStringList &selectedBadges);

  /**
   * @brief Get cached badge values for a profile (no network call)
   * @return Map of badgeId -> formatted display string
   */
  QMap<QString, QString> cachedValues(const QString &profileId) const;

signals:
  /// @brief Emitted when all badge values for a profile are ready
  void badgeDataReady(const QString &profileId,
                      const QMap<QString, QString> &badgeValues);

  /// @brief Emitted when badge refresh fails
  void badgeDataFailed(const QString &profileId, const QString &error);

private:
  /// @brief Read cached data and format badge values from it
  QMap<QString, QString>
  aggregateFromCache(const QString &profileId,
                     const QStringList &selectedBadges);

  /// @brief Track pending extended endpoint fetches per profile
  struct PendingRefresh {
    QStringList selectedBadges;
    QSet<QString> pendingEndpoints;
    QString apiKey;
  };

  APICache *m_cache;
  GW2APIClient *m_apiClient;

  /// @brief In-memory cache of latest formatted badge values per profile
  QMap<QString, QMap<QString, QString>> m_badgeValues;

  /// @brief Pending refresh tracking (profileId -> state)
  QMap<QString, PendingRefresh> m_pending;

private slots:
  /// @brief Handle API response for an extended endpoint
  void onApiResponse(const QString &endpoint, const QJsonDocument &data);
};
